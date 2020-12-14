#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

enum { COL_BACKGROUND, NCOLOURS };

struct game_params {
    int w;
    int h;
};

static const struct game_params knight_presets[] = {{6, 6}, {7, 7}};

struct pair {
    int x;
    int y;
};
typedef struct pair pair;

static const pair knight_moves[9] = {{1, -2},  {2, -1},  {2, 1},
                                     {1, 2},   {-1, 2},  {-2, 1},
                                     {-2, -1}, {-1, -2}, {0, 0}};

/* static const int all_conns[36][2] = {
    {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {1, 2}, {1, 3},
    {1, 4}, {1, 5}, {1, 6}, {1, 7}, {2, 3}, {2, 4}, {2, 5}, {2, 6}, {2, 7},
    {3, 4}, {3, 5}, {3, 6}, {3, 7}, {4, 5}, {4, 6}, {4, 7}, {5, 6}, {5, 7},
    {6, 7}, {8, 0}, {8, 1}, {8, 2}, {8, 3}, {8, 4}, {8, 5}, {8, 6}, {8, 7}};
 */
struct game_state {
    int w, h, size; /* width, height, size = w * h */
    int nunvisited; /* Number of cells not visited in the tour */
    int ncells;     /* ncells = w * h - nunvisited */
    int ends[2];    /* start and ending cells of tour */

    /**
     * A WxH array with values:
     *    9. Initialization value (means nothing)
     *    0. Unvisited cell
     *    1. An endpoint, there are only two in the grid
     *    2. A cell where an orthogonal turn was made
     *    3. A cell where a non-orthogonal turn was made
     */
    unsigned char* grid;

    int* moves;

    random_state* rs;
};

static game_params* default_params(void) {
    game_params* ret = snew(game_params);

    ret->w = ret->h = 6;

    return ret;
}

static bool game_fetch_preset(int i, char** name, game_params** params) {
    if (i < 0 || i >= lenof(knight_presets))
        return false;

    game_params* preset = snew(game_params);
    *preset = knight_presets[i];

    char str[100];
    sprintf(str, "%dx%d", preset->w, preset->h);

    *name = dupstr(str);
    *params = preset;
    return true;
}

static void free_params(game_params* params) {
    sfree(params);
}

static game_params* dup_params(const game_params* params) {
    game_params* ret = snew(game_params);
    *ret = *params; /* structure copy */
    return ret;
}

static void decode_params(game_params* params, char const* string) {
    char const* p = string;

    params->w = atoi(p);
    while (*p && isdigit((unsigned char)*p))
        p++;
    if (*p == 'x') {
        p++;
        params->h = atoi(p);
    }
}

static char* encode_params(const game_params* params, bool full) {
    char str[100];
    sprintf(str, "%dx%d", params->w, params->h);
    return dupstr(str);
}

static config_item* game_configure(const game_params* params) {
    return NULL;
}

static game_params* custom_params(const config_item* cfg) {
    return NULL;
}

static const char* validate_params(const game_params* params, bool full) {
    return NULL;
}

int attempt_move(int pos, pair move, int w, int h) {
    int newx = (pos % w) + move.x;
    int newy = (pos / w) + move.y;
    if (0 <= newx && newx < w && 0 <= newy && newy < h)
        return newy * w + newx;
    return -1;
}

int num_neighbors(unsigned char* grid, int pos, int w, int h) {
    int count = 0, i;
    for (i = 0; i < 8; i++) {
        pair move = knight_moves[i];
        int neighbor = attempt_move(pos, move, w, h);
        if (neighbor >= 0 && grid[neighbor] == 9)
            count++;
    }
    return count;
}

int* unique_random_upto(random_state* rs, int length, int max) {
    int* arr = snewn(length, int);

    int i, j;
    for (i = 0; i < length; i++) {
        arr[i] = random_upto(rs, max);
        for (j = 0; j < i; j++)
            if (arr[i] == arr[j]) {
                i--;
                break;
            }
    }

    return arr;
}

static void free_game(game_state* state) {
    if (state) {
        sfree(state->grid);
        sfree(state->moves);
        sfree(state);
    }
}

struct game_state* fill_grid(int w, int h) {
    assert(w > 5 && h > 5);
    struct game_state* gs;
    random_state* rs = random_new("654123", 6);

generate_grid:
    gs = snew(game_state);
    gs->rs = rs;
    gs->w = w;
    gs->h = h;
    gs->size = w * h;

    gs->nunvisited = random_upto(gs->rs, w + h);
    gs->ends[0] = random_upto(gs->rs, gs->size);
    gs->ncells = gs->size - gs->nunvisited;

    gs->grid = snewn(w * h, unsigned char);
    gs->moves = snewn(gs->ncells - 1, int);

    int i;
    for (i = 0; i < w * h; i++)
        gs->grid[i] = 9;

    for (i = 0; i < gs->ncells - 1; i++)
        gs->moves[i] = -1;

    gs->grid[gs->ends[0]] = 1;
    int pos = gs->ends[0];

    int moves_i[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int move_i = -1, prev_move_i;

    int cells_left = gs->ncells;

    while (cells_left > 0) {
        shuffle(moves_i, 8, sizeof(int), gs->rs);
        int min_neigh = 8, next_pos = -1;
        prev_move_i = move_i;

        int i;
        for (i = 0; i < 8; i++) {
            int tmp = attempt_move(pos, knight_moves[moves_i[i]], gs->w, gs->h);

            if (tmp >= 0 && gs->grid[tmp] == 9) {
                int num = num_neighbors(gs->grid, tmp, gs->w, gs->h);

                if (num < min_neigh) {
                    min_neigh = num;
                    next_pos = tmp;
                    move_i = moves_i[i];
                }
            }
        }

        if (next_pos == -1) {
            /* Warnsdorff heuristic failed, just restart */
            free_game(gs);
            goto generate_grid;
        }

        if (prev_move_i != -1)
            /* 2=orthogonal turns, 3=non-orthogonal turns */
            gs->grid[pos] = (prev_move_i + move_i) % 2 + 2;

        pos = next_pos;
        gs->grid[pos] = 1;
        gs->moves[gs->ncells - cells_left] = move_i;
        cells_left--;
    }

    gs->ends[1] = pos;

    for (i = 0; i < gs->size; i++)
        if (gs->grid[i] == 9)
            gs->grid[i] = 0;

    return gs;
}

void print_grid(unsigned char* grid, int w, int h) {
    int i;
    for (i = 0; i < w * h; i++) {
        printf("%d", grid[i]);
        if (i % w == w - 1)
            printf("\n");
    }
}

static char* new_game_desc(const game_params* params,
                           random_state* rs,
                           char** aux,
                           bool interactive) {
    game_state* gs = fill_grid(params->w, params->h);
    printf("%d %d %d\n", gs->ends[0], gs->ends[1], gs->nunvisited);
    print_grid(gs->grid, gs->w, gs->h);
    sfree(gs);
    return dupstr("FIXME");
}

static const char* validate_desc(const game_params* params, const char* desc) {
    return NULL;
}

static game_state* new_game(midend* me,
                            const game_params* params,
                            const char* desc) {
    return NULL;
}

static game_state* dup_game(const game_state* state) {
    game_state* ret = snew(game_state);

    return ret;
}

static char* solve_game(const game_state* state,
                        const game_state* currstate,
                        const char* aux,
                        const char** error) {
    return NULL;
}

static bool game_can_format_as_text_now(const game_params* params) {
    return true;
}

static char* game_text_format(const game_state* state) {
    return NULL;
}

static game_ui* new_ui(const game_state* state) {
    return NULL;
}

static void free_ui(game_ui* ui) {}

static char* encode_ui(const game_ui* ui) {
    return NULL;
}

static void decode_ui(game_ui* ui, const char* encoding) {}

static void game_changed_state(game_ui* ui,
                               const game_state* oldstate,
                               const game_state* newstate) {}

struct game_drawstate {
    int tilesize;
    int FIXME;
};

static char* interpret_move(const game_state* state,
                            game_ui* ui,
                            const game_drawstate* ds,
                            int x,
                            int y,
                            int button) {
    return NULL;
}

static game_state* execute_move(const game_state* state, const char* move) {
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params* params,
                              int tilesize,
                              int* x,
                              int* y) {
    *x = *y = 10 * tilesize; /* FIXME */
}

static void game_set_size(drawing* dr,
                          game_drawstate* ds,
                          const game_params* params,
                          int tilesize) {
    ds->tilesize = tilesize;
}

static float* game_colours(frontend* fe, int* ncolours) {
    float* ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate* game_new_drawstate(drawing* dr,
                                          const game_state* state) {
    struct game_drawstate* ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->FIXME = 0;

    return ds;
}

static void game_free_drawstate(drawing* dr, game_drawstate* ds) {
    sfree(ds);
}

static void game_redraw(drawing* dr,
                        game_drawstate* ds,
                        const game_state* oldstate,
                        const game_state* state,
                        int dir,
                        const game_ui* ui,
                        float animtime,
                        float flashtime) {
    /*
     * The initial contents of the window are not guaranteed and
     * can vary with front ends. To be on the safe side, all games
     * should start by drawing a big background-colour rectangle
     * covering the whole window.
     */
    draw_rect(dr, 0, 0, 10 * ds->tilesize, 10 * ds->tilesize, COL_BACKGROUND);
    draw_update(dr, 0, 0, 10 * ds->tilesize, 10 * ds->tilesize);
}

static float game_anim_length(const game_state* oldstate,
                              const game_state* newstate,
                              int dir,
                              game_ui* ui) {
    return 0.0F;
}

static float game_flash_length(const game_state* oldstate,
                               const game_state* newstate,
                               int dir,
                               game_ui* ui) {
    return 0.0F;
}

static int game_status(const game_state* state) {
    return 0;
}

static bool game_timing_state(const game_state* state, game_ui* ui) {
    return true;
}

static void game_print_size(const game_params* params, float* x, float* y) {}

static void game_print(drawing* dr, const game_state* state, int tilesize) {}

#ifdef COMBINED
#define thegame knight
#endif

const struct game thegame = {
    "Knight",
    "games.knight",
    "knight",
    default_params,
    game_fetch_preset,
    NULL,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    true,
    game_configure,
    custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    true,
    solve_game,
    true,
    game_can_format_as_text_now,
    game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
    interpret_move,
    execute_move,
    32,
    game_compute_size,
    game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    false,
    false,
    game_print_size,
    game_print,
    false, /* wants_statusbar */
    false,
    game_timing_state,
    0, /* flags */
};

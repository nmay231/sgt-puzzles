#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

#define BORDER 10
#define PREFERRED_TILE_SIZE 20

struct pair {
    int a;
    int b;
};
typedef struct pair pair;
struct point {
    int x;
    int y;
};
typedef struct point point;

/**
 * All of the knight's moves ordered clockwise.
 * knight_moves[0] is approximately 1:15 o'clock.
 *
 * Note: since moves that are orthogonal to each other are 2, 4,
 * or 6 apart in the list, it will be common to add the indexes
 * of two moves (mod 2) to check if they are orthogonal (or not).
 *
 * Also, knight_moves[8]={0, 0} helps with finding unique solutions.
 */
static const point knight_moves[9] = {{1, -2},  {2, -1},  {2, 1},
                                      {1, 2},   {-1, 2},  {-2, 1},
                                      {-2, -1}, {-1, -2}, {0, 0}};

/* (Almost) all combinations of choosing 2 of 9 ints, i.e. 9 choose 2.
 * The values are indexes of knight_moves in and out of the cell.
 * {8, 8} is missing because at least one move must go out of the cell.
 */
pair all_conns[36] = {
    {8, 7}, {8, 6}, {8, 5}, {8, 4}, {8, 3}, {8, 2}, {8, 1}, {8, 0}, {7, 6},
    {7, 5}, {6, 5}, {7, 4}, {6, 4}, {5, 4}, {7, 3}, {6, 3}, {5, 3}, {4, 3},
    {7, 2}, {6, 2}, {5, 2}, {4, 2}, {3, 2}, {7, 1}, {6, 1}, {5, 1}, {4, 1},
    {3, 1}, {2, 1}, {7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}};
pair PAIR_DNE = {9, 9};

struct game_params {
    int w;
    int h;
    /* bool full_board;
    int missing; */
};

static const struct game_params knight_presets[] = {{6, 6}, {7, 7}};

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
    int* grid;

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
    while (*p && isdigit((int)*p))
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

int attempt_move(int pos, point move, int w, int h) {
    int newx = (pos % w) + move.x;
    int newy = (pos / w) + move.y;
    if (0 <= newx && newx < w && 0 <= newy && newy < h)
        return newy * w + newx;
    return -1;
}

int num_neighbors(int* grid, int pos, int w, int h) {
    int count = 0, i;
    for (i = 0; i < 8; i++) {
        point move = knight_moves[i];
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
    random_state* rs = random_new("123456", 6);

generate_grid:
    gs = snew(game_state);
    gs->rs = rs;
    gs->w = w;
    gs->h = h;
    gs->size = w * h;

    gs->nunvisited = random_upto(gs->rs, w + h);
    gs->ends[0] = random_upto(gs->rs, gs->size);
    gs->ncells = gs->size - gs->nunvisited;

    gs->grid = snewn(w * h, int);
    memset(gs->grid, 0, w * h * sizeof(int));
    gs->moves = snewn(gs->ncells - 1, int);

    int i;
    for (i = 0; i < gs->ncells - 1; i++)
        gs->moves[i] = -1;

    gs->grid[gs->ends[0]] = 1;
    int pos = gs->ends[0];

    int moves_i[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int move_i = -1, prev_move_i;

    int cells_left = gs->ncells - 1;

    while (cells_left > 0) {
        shuffle(moves_i, 8, sizeof(int), gs->rs);
        int min_neigh = 8, next_pos = -1;
        prev_move_i = move_i;

        int i;
        for (i = 0; i < 8; i++) {
            int tmp = attempt_move(pos, knight_moves[moves_i[i]], gs->w, gs->h);

            if (tmp >= 0 && !gs->grid[tmp]) {
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
        /* TODO: Memory issue caught by valgrind? */
        gs->moves[gs->ncells - cells_left - 1] = move_i;
        cells_left--;
    }

    gs->ends[1] = pos;

    return gs;
}

void print_grid(int* grid, int w, int h) {
    int i;
    for (i = 0; i < w * h; i++) {
        printf("%d", grid[i]);
        if (i % w == w - 1)
            printf("\n");
    }
}

/*
 * Remove pairs of numbers where neither are equal to required
 * and shift the remaining pairs to the beginning of the array.
 * Additionally, synchronize sync_index to point to the same pair
 * (or the one after it if it was removed).
 */
void shift_to_beginning(pair* array, int len, int required, int* sync_index) {
    int old_index, new_index = 0;
    for (old_index = 0; old_index < len; old_index++)
        if (required == array[old_index].a || required == array[old_index].b) {
            array[new_index] = array[old_index];
            new_index++;

        } else if (*sync_index > new_index)
            *sync_index -= 1;
    array[new_index] = PAIR_DNE;
}

static char* new_game_desc(const game_params* params,
                           random_state* rs,
                           char** aux,
                           bool interactive) {
    game_state* gs = fill_grid(params->w, params->h);
    printf("%d %d %d\n", gs->ends[0], gs->ends[1], gs->nunvisited);
    print_grid(gs->grid, gs->w, gs->h);
    int s = gs->size;

    /* size=w*h*(28-6+1) */
    pair** possible_conns = snewn(s * 23, pair*);

    int pos;
    for (pos = 0; pos < s; pos++) {
        int i = 0, index = 0;
        for (i = 0; i < 36; i++) {
            if (!gs->grid[pos])
                break;
            pair c = all_conns[i];
            if ((gs->grid[pos] == 1 && i < 8) ||
                (gs->grid[pos] > 1 && i >= 8 &&
                 (gs->grid[pos] + c.a + c.b) % 2 == 0)) {
                int pos1 = attempt_move(pos, knight_moves[c.a], gs->w, gs->h),
                    pos2 = attempt_move(pos, knight_moves[c.b], gs->w, gs->h);
                if (pos1 != -1 && pos2 != -1 && gs->grid[pos1] &&
                    gs->grid[pos2])
                    possible_conns[pos * 23 + index++] = &(all_conns[i]);
            }
        }

        while (index < 23)
            possible_conns[pos * 23 + (index++)] = &PAIR_DNE;
    }

    int* unique_solution = NULL;
    int conns_index[s];
    /* memset(conns_index, 0, s * sizeof(int)); */
    int i;
    for (i = 0; i < s; i++)
        conns_index[i] = 0;

    pos = 0;
    /*
     * - possible_conns: The possible knights moves from a pos
     *   * first index: which cell
     *   * second index: which of all_conns (made of two knight_moves)
     *   * third (0 or 1): the two knight moves
     * - conns_index: the current second index of possible_conns.
     *   They are incremented independently of each other
     * - pos: which of conns_index are we incrementing
     */

    while (0 <= pos) {
        if (pos < s) {
            if (!gs->grid[pos]) {
                pos++;
                continue;
            }

            while (possible_conns[pos * 23 + conns_index[pos]]->a == 9) {
                conns_index[pos] = 0;
                conns_index[--pos]++;
                if (pos < 0)
                    goto break_outer_loop;
            }

            pair conns = *possible_conns[pos * 23 + conns_index[pos]];
            point move0 = knight_moves[conns.a], move1 = knight_moves[conns.b];
            int pos0 = pos + move0.y * gs->w + move0.x,
                pos1 = pos + move1.y * gs->w + move1.x;

            pair conns0 = *possible_conns[pos0 * 23 + conns_index[pos0]],
                 conns1 = *possible_conns[pos1 * 23 + conns_index[pos1]];

            if ((pos < pos0 || conns.a == (conns0.a + 4) % 8 ||
                 conns.a == (conns0.b + 4) % 8 || conns.a == 8) &&
                (pos <= pos1 || conns.b == (conns1.a + 4) % 8 ||
                 conns.b == (conns1.b + 4) % 8))
                /* pos makes valid connections, validate next pos */
                pos++;
            else
                /* connections not valid, try next conn */
                conns_index[pos]++;
        } else {
            /*
             * All connections have been validated (connections go both
             * ways). Now walk through the tour to make sure there are no
             * unreached cells due to loops.
             */
            int i, cell = gs->ends[0];
            int conn = possible_conns[cell * 23 + conns_index[cell]]->b;
            for (i = 0; i < gs->ncells; i++) {
                cell = attempt_move(cell, knight_moves[conn], gs->w, gs->h);
                pair next_conns =
                    *possible_conns[cell * 23 + conns_index[cell]];
                conn = (next_conns.a + 4) % 8 == conn ? next_conns.b
                                                      : next_conns.a;
                if (next_conns.a == 8)
                    break;
            }

            /* If there are loops */
            if (i < gs->ncells - 2) {
                while (!gs->grid[--pos])
                    ;
                conns_index[pos]++;
            } else if (!unique_solution) {
                /* Found first solution! */
                unique_solution = snewn(s, int);
                memcpy(unique_solution, conns_index, s * sizeof(int));
                while (!gs->grid[--pos])
                    ;
                conns_index[pos]++;
            } else {
                /* New solution found. Add restrictions to remove differences */
                int different_conn = -1;
                for (i = 0; i < s; i++) {
                    if (unique_solution[i] == conns_index[i]) {
                        continue;
                    } else if (all_conns[unique_solution[i]].a !=
                               all_conns[conns_index[i]].a) {
                        different_conn = all_conns[unique_solution[i]].a;
                    } else {
                        different_conn = all_conns[unique_solution[i]].b;
                    }
                    break;
                }

                if (different_conn > -1)
                    shift_to_beginning(possible_conns[i], s, different_conn,
                                       &conns_index[i]);
                /* TODO: Add to gs or something */
            }
        }
    }

break_outer_loop:
    printf("\n");
    assert(unique_solution != NULL);
    print_grid(unique_solution, gs->w, gs->h);
    free_game(gs);
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

enum { COL_BACKGROUND, COL_OUTLINE, COL_PATH, NCOLOURS };

static void game_compute_size(const game_params* params,
                              int tilesize,
                              int* x,
                              int* y) {
    *x = params->w * tilesize + 2 * BORDER;
    *y = params->h * tilesize + 2 * BORDER;
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

    ret[COL_OUTLINE * 3 + 0] = 0.5F;
    ret[COL_OUTLINE * 3 + 1] = 0.5F;
    ret[COL_OUTLINE * 3 + 2] = 0.5F;

    ret[COL_PATH * 3 + 0] = 0.0F;
    ret[COL_PATH * 3 + 1] = 0.0F;
    ret[COL_PATH * 3 + 2] = 0.0F;

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
    int w = 6, h = 6;
    /* int w = state->w, h = state->h; */

    draw_rect(dr, 0, 0, w * ds->tilesize + 2 * BORDER,
              h * ds->tilesize + 2 * BORDER, COL_BACKGROUND);

    int x, y;
    for (x = BORDER; x <= w * ds->tilesize + BORDER; x += ds->tilesize) {
        draw_line(dr, x, BORDER, x, BORDER + h * ds->tilesize, COL_OUTLINE);
    }
    for (y = BORDER; y <= h * ds->tilesize + BORDER; y += ds->tilesize) {
        draw_line(dr, BORDER, y, BORDER + w * ds->tilesize, y, COL_OUTLINE);
    }

    draw_update(dr, 0, 0, w * ds->tilesize + 2 * BORDER,
                h * ds->tilesize + 2 * BORDER);
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
    PREFERRED_TILE_SIZE,
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

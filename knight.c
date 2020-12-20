#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

#define BORDER 10
#define PREFERRED_TILE_SIZE 30

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
    int w, h;       /* width, height */
    int nunvisited; /* Number of cells not visited in the tour */
    int ncells;     /* ncells = w * h - nunvisited */
    int ends[2];    /* Start and ending cells of tour */

    /*
     * A WxH array with values:
     *    0. Unvisited cell
     *    1. An endpoint, there are only two in the grid
     *    2. A cell where an orthogonal turn was made
     *    3. A cell where a non-orthogonal turn was made
     */
    int* grid;

    /*
     * Pairs of connections, where cell i uses indexes 2*i and 2*i+1, in the
     * range '0'-'8'. '8' means connection is unused.
     */
    char* conn_pairs;

    /* Flags for if a conn_pairs was included in the puzzle hints */
    bool* start_pairs;
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

game_state* init_game_state(int w, int h) {
    game_state* gs = snew(game_state);
    gs->w = w;
    gs->h = h;
    gs->ends[0] = gs->ends[1] = gs->nunvisited = -1;
    gs->ncells = w * h;
    /* FIXME: should I convert gs->grid to be a string? */
    gs->grid = snewn(w * h, int);
    gs->conn_pairs = snewn(2 * w * h, char);
    gs->start_pairs = snewn(2 * w * h, bool);
    int i;
    for (i = 0; i < w * h; i++) {
        gs->grid[i] = 0;
        gs->conn_pairs[2 * i] = gs->conn_pairs[2 * i + 1] = '8';
        gs->start_pairs[2 * i] = gs->start_pairs[2 * i + 1] = false;
    }

    return gs;
}

static void free_game(game_state* state) {
    if (state) {
        sfree(state->grid);
        sfree(state->conn_pairs);
        sfree(state->start_pairs);
        sfree(state);
    }
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

static char* solve_game(const game_state* state,
                        const game_state* currstate,
                        const char* aux,
                        const char** error);
static game_state* execute_move(const game_state* state, const char* move);

/* Modify gs in place to  */
void unique_solution(game_state* gs, bool guaranteed) {}

static char* new_game_desc(const game_params* params,
                           random_state* rs,
                           char** aux,
                           bool interactive) {
    int w = params->w, h = params->h;
    assert(w > 5 && h > 5);
    struct game_state* gs;
    rs = random_new("123456", 6);

generate_grid:
    gs = init_game_state(w, h);
    gs->nunvisited = random_upto(rs, w + h);
    gs->ends[0] = random_upto(rs, w * h);
    gs->ncells = w * h - gs->nunvisited;

    int* moves = snewn(gs->ncells - 1, int);

    int i;
    for (i = 0; i < gs->ncells - 1; i++)
        moves[i] = -1;

    int pos = gs->ends[0];
    gs->grid[pos] = 1;

    int moves_i[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int move_i = -1, prev_move_i;

    int cells_left;
    for (cells_left = gs->ncells - 1; cells_left > 0; cells_left--) {
        /* Shuffle moves to remove bias when multiple moves are possible */
        shuffle(moves_i, 8, sizeof(int), rs);
        int min_neigh = 8, next_pos = -1;
        prev_move_i = move_i;

        int i;
        for (i = 0; i < 8; i++) {
            int neighbor =
                attempt_move(pos, knight_moves[moves_i[i]], gs->w, gs->h);

            if (neighbor >= 0 && !gs->grid[neighbor]) {
                int num = num_neighbors(gs->grid, neighbor, gs->w, gs->h);

                if (num < min_neigh) {
                    min_neigh = num;
                    next_pos = neighbor;
                    move_i = moves_i[i];
                }
            }
        }

        if (next_pos == -1) {
            /* Warnsdorff heuristic failed or a tour is impossible, restart */
            free_game(gs);
            goto generate_grid;
        }

        if (prev_move_i != -1)
            /* 2=orthogonal turns, 3=non-orthogonal turns */
            gs->grid[pos] = (prev_move_i + move_i) % 2 + 2;

        pos = next_pos;
        gs->grid[pos] = 1;
        /* FIXME: Memory issue caught by valgrind? */
        moves[gs->ncells - cells_left - 1] = move_i;
    }

    gs->ends[1] = pos;

    /* Overload solve_game() to restrict a solvable
     * grid to have a *unique* solution */
    /* game_state* old = gs;
    char* starting_moves = solve_game(old, NULL, "uniquify", NULL);
    gs = execute_move(old, starting_moves);
    free_game(old);
    sfree(starting_moves); */

    /* ==== Convert to string ==== */
    char *string = snewn(w * h + 1, char), *p = string;
    for (i = 0; i < w * h; i++)
        sprintf(p++, "%d", gs->grid[i]);
    *p = '\0';
    free_game(gs);
    return string;
}

static const char* validate_desc(const game_params* params, const char* desc) {
    return NULL;
}

static game_state* new_game(midend* me,
                            const game_params* params,
                            const char* desc) {
    int w = params->w, h = params->h;
    game_state* gs = init_game_state(w, h);

    int i, grid = atoi(desc);
    printf("'%s'\n", desc);
    for (i = 0; i < w * h; i++) {
        int c = desc[i] - '0';
        gs->grid[i] = c;
        if (c == 1) {
            gs->ends[gs->ends > 0 ? 1 : 0] = i;
        } else if (c == 0) {
            gs->nunvisited++;
        }
        grid = grid / 10;
    }
    print_grid(gs->grid, w, h);
    gs->ncells = w * h - gs->nunvisited;
    return gs;
}

static game_state* dup_game(const game_state* state) {
    game_state* ret = init_game_state(state->w, state->h);
    ret->w = state->w;
    ret->h = state->h;
    ret->ncells = state->ncells;
    ret->nunvisited = state->nunvisited;
    ret->ends[0] = state->ends[0];
    ret->ends[1] = state->ends[1];

    int i;
    for (i = 0; i < ret->w * ret->h; i++) {
        ret->grid[i] = state->grid[i];
        ret->conn_pairs[2 * i] = state->conn_pairs[2 * i];
        ret->conn_pairs[2 * i + 1] = state->conn_pairs[2 * i + 1];
        ret->start_pairs[2 * i] = state->start_pairs[2 * i + 1];
        ret->start_pairs[2 * i] = state->start_pairs[2 * i + 1];
    }

    return ret;
}

static char* solve_game(const game_state* state,
                        const game_state* currstate,
                        const char* aux,
                        const char** error) {
    char* solution = snewn(100, char);
    solution = dupstr("20.30");
    return solution;
}

static bool game_can_format_as_text_now(const game_params* params) {
    return true;
}

static char* game_text_format(const game_state* state) {
    return NULL;
}

struct game_ui {
    int cx, cy;
    bool selected;
};

static game_ui* new_ui(const game_state* state) {
    game_ui* ui = snew(game_ui);
    ui->cx = ui->cy = 0;
    ui->selected = false;
    return ui;
}

static void free_ui(game_ui* ui) {
    sfree(ui);
}

static char* encode_ui(const game_ui* ui) {
    return NULL;
}

static void decode_ui(game_ui* ui, const char* encoding) {}

static void game_changed_state(game_ui* ui,
                               const game_state* oldstate,
                               const game_state* newstate) {
    ui->selected = false;
}

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
    int w = state->w, h = state->h;
    x = (x - BORDER) / ds->tilesize;
    y = (y - BORDER) / ds->tilesize;

    if (x < 0 || x >= w || y < 0 || y >= h || !state->grid[y * w + x])
        return NULL;

    if (!ui->selected) {
        ui->selected = true;
        ui->cx = x;
        ui->cy = y;
        return UI_UPDATE;
    }

    /* Not a knights move away, change position instead of moving */
    int dx = x - ui->cx, dy = y - ui->cy;
    if (min(abs(dx), abs(dy)) != 1 || max(abs(dx), abs(dy)) != 2) {
        ui->cx = x;
        ui->cy = y;
        return UI_UPDATE;
    }

    int pos = y * w + x;
    char offsetx = state->conn_pairs[pos * 2],
         offsety = state->conn_pairs[pos * 2 + 1];
    if (state->grid[pos] &&
        ((offsetx == '8' && offsety == '8') ||
         ((offsetx == '8' || offsety == '8') &&
          (state->grid[pos] + offsetx + offsety) % 2 == 0))) {
        char* buffer = snewn(15, char);
        sprintf(buffer, "%d%d%d", dx + 2, dy + 2, ui->cy * w + ui->cx);
        ui->cx = x;
        ui->cy = y;
        return buffer;
    }

    return NULL;
}

static game_state* execute_move(const game_state* state, const char* move) {
    int size = state->w * state->h;
    game_state* gs = dup_game(state);
    char* s = dupstr(move);
    int dx = *s - '0' - 2, dy = *(++s) - '0' - 2, pos = atoi(++s);
    if (min(abs(dx), abs(dy)) != 1 || max(abs(dx), abs(dy)) != 2 || pos < 0 ||
        pos >= size) {
        return NULL;
    }

    /* This ugly formula turns {dx, dy} of knight_moves[i] into the index i */
    int i = (dx > 0 ? 2 : 5) + (dx / abs(dx)) * (dy + (dy > 0 ? -1 : 0));

    gs->conn_pairs[2 * pos + (gs->conn_pairs[2 * pos] == '8')] = i + '0';
    pos += dy * gs->w + dx;
    i = (i + 4) % 8;
    if (gs->conn_pairs[2 * pos] < '8' && gs->conn_pairs[2 * pos + 1] < '8')
        return gs;
    gs->conn_pairs[2 * pos + (gs->conn_pairs[2 * pos] < '8')] = i + '0';

    return gs;
}

/* ================ Drawing routines ================ */

enum {
    COL_BACKGROUND,
    COL_OUTLINE,
    COL_PATH,
    COL_HIGHLIGHT,
    COL_SELECTED,
    NCOLOURS
};

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

    ret[COL_HIGHLIGHT * 3 + 0] = ret[COL_BACKGROUND * 3] * 0.8;
    ret[COL_HIGHLIGHT * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.8;
    ret[COL_HIGHLIGHT * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 1.2;

    ret[COL_SELECTED * 3 + 0] = ret[COL_BACKGROUND * 3] * 0.6;
    ret[COL_SELECTED * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.6;
    ret[COL_SELECTED * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 1.3;

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
    int w = state->w, h = state->h;

    /* Background */
    draw_rect(dr, 0, 0, w * ds->tilesize + 2 * BORDER,
              h * ds->tilesize + 2 * BORDER, COL_BACKGROUND);

    /* Grid lines */
    int x, y;
    for (x = BORDER; x <= w * ds->tilesize + BORDER; x += ds->tilesize) {
        draw_line(dr, x, BORDER, x, BORDER + h * ds->tilesize, COL_OUTLINE);
    }
    for (y = BORDER; y <= h * ds->tilesize + BORDER; y += ds->tilesize) {
        draw_line(dr, BORDER, y, BORDER + w * ds->tilesize, y, COL_OUTLINE);
    }

    /* Mark unvisited cells and cells with orthogonal turns */
    int i;
    for (i = 0; i < w * h; i++)
        if (!state->grid[i]) {
            int x = i % w, y = i / w;
            draw_rect(dr, BORDER + x * ds->tilesize, BORDER + y * ds->tilesize,
                      ds->tilesize, ds->tilesize, COL_OUTLINE);
        } else if (state->grid[i] == 2) {
            int x = i % w, y = i / w;
            draw_rect(dr, BORDER + (x + 0.4) * ds->tilesize,
                      BORDER + (y + 0.4) * ds->tilesize, ds->tilesize * 0.2 + 1,
                      ds->tilesize * 0.2 + 1, COL_OUTLINE);
        }

    /* Cursor and Available moves */
    if (ui->selected) {
        draw_rect(dr, BORDER + ui->cx * ds->tilesize + 1,
                  BORDER + ui->cy * ds->tilesize + 1, ds->tilesize - 1,
                  ds->tilesize - 1, COL_SELECTED);
        draw_rect_outline(dr, BORDER + ui->cx * ds->tilesize + 1,
                          BORDER + ui->cy * ds->tilesize + 1, ds->tilesize - 1,
                          ds->tilesize - 1, COL_HIGHLIGHT);

        int i, cur_pos = (ui->cy * w + ui->cx);
        char* conns = state->conn_pairs + 2 * cur_pos;
        for (i = 0; i < 8; i++) {
            int neighbor = attempt_move(cur_pos, knight_moves[i], w, h);
            if (neighbor < 0 || !state->grid[neighbor])
                continue;
            if ((conns[0] == '8' && conns[1] == '8') ||
                (i + conns[0] + conns[1] + state->grid[cur_pos]) % 2 == 0)
                draw_rect_corners(dr,
                                  BORDER + (neighbor % w + 0.5) * ds->tilesize,
                                  BORDER + (neighbor / w + 0.5) * ds->tilesize,
                                  ds->tilesize / 4, COL_SELECTED);
        }
    }

    /* Tour path */
    for (i = 0; i < 2 * w * h; i++) {
        char c = state->conn_pairs[i];
        if (c != '8') {
            point move = knight_moves[c - '0'];
            int a = i / 2, b = a + move.y * w + move.x;

            int ax = (a % w + 0.5) * ds->tilesize + BORDER,
                ay = (a / w + 0.5) * ds->tilesize + BORDER,
                bx = (b % w + 0.5) * ds->tilesize + BORDER,
                by = (b / w + 0.5) * ds->tilesize + BORDER;

            if (a < b)
                draw_line(dr, ax, ay, bx, by, COL_PATH);

            if (i % 2 == 0 && state->conn_pairs[i] < '8' &&
                state->conn_pairs[i + 1] < '8')
                draw_circle(dr, ax, ay, 10, COL_PATH, COL_PATH);
        }
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

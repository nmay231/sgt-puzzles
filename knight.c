#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

/* ======== Parameters ======== */
#define MAX_UNVISITED (w + h)

#define BORDER 10
#define PREFERRED_TILE_SIZE 30

#define NO_MOD(pos) pos & ~(MOD_LOOP | MOD_ANGLE)
#define MOD_LOOP 8
#define MOD_ANGLE 16

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
 * knight_moves[0] is slightly less than "1:00 o'clock".
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

static const struct game_params knight_presets[] = {{6, 6},
                                                    {7, 7},
                                                    {8, 8},
                                                    {10, 8}};

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

    /*
     * A mapping for opposite endpoints of disjoint paths. Used for
     * detecting loops or if the board is finished. Each item is:
     *   i: if the value is its own index i, it is not part of a path
     *  -1: An unvisited cell or part of a path
     *  -2: A cell with two connections at the wrong angle
     *  -3: A cell that's part of a loop. -3 has higher priority than -2
     * a,b: Any other value is the opposite endpoint of the path.
     *      This means a cannot connect to b if a == opposite_ends[b]
     *      if i >= 0, then opposite_ends[opposite_ends[i]] == i
     */
    int* opposite_ends;

    /* The ending cursor position after the last move. Used to update
     * game_ui */
    int cx, cy;
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
    gs->ends[0] = gs->ends[1] = gs->nunvisited = gs->cx = gs->cy = -1;
    gs->ncells = w * h;
    /* FIXME: should I convert gs->grid to be a string? */
    gs->grid = snewn(w * h, int);
    gs->opposite_ends = snewn(w * h + 2, int);
    gs->conn_pairs = snewn(2 * w * h, char);
    gs->start_pairs = snewn(2 * w * h, bool);
    int i;
    for (i = 0; i < w * h; i++) {
        gs->grid[i] = 0;
        gs->opposite_ends[i] = i;
        gs->conn_pairs[2 * i] = gs->conn_pairs[2 * i + 1] = '8';
        gs->start_pairs[2 * i] = gs->start_pairs[2 * i + 1] = false;
    }
    gs->opposite_ends[i] = i;
    gs->opposite_ends[i + 1] = i + 1;

    return gs;
}

static void free_game(game_state* state) {
    if (state) {
        sfree(state->grid);
        sfree(state->opposite_ends);
        sfree(state->conn_pairs);
        sfree(state->start_pairs);
        sfree(state);
    }
}

void stdq_add(tdq* tdq, int k, int max) {
    if (0 <= k && k < max)
        tdq_add(tdq, k);
}

void connect_ends(int* opposite_ends, int a, int b) {
    int a_end = opposite_ends[a], b_end = opposite_ends[b];
    opposite_ends[a_end] = b_end;
    opposite_ends[b_end] = a_end;

    /* If a or b are in the middle of a path (not endpoints), set them to -1 */
    if (opposite_ends[opposite_ends[a]] != a)
        opposite_ends[a] = -1;
    if (opposite_ends[opposite_ends[b]] != b)
        opposite_ends[b] = -1;
}

/* unique_solution() uses a recursive algorithm to backtrack. This holds the
 * state for each function call. */
struct unique_solution_ctx {
    /* Which knights moves are valid to make from each cell */
    bool* can_connect;
    /* Which moves are part of the tour, aka the solution */
    bool* connected;
    /* A dictionary mapping endpoints of disjoint paths */
    int* opposite_ends;
    /* Precomputed neighbors of cells */
    int* neighbors;
    /* Which cells of the grid have yet to be restricted, i.e. which cells
     * have connections that (might) be disqualified */
    tdq* todo;
    /* Index of connected and can_connect that are permanent */
    int* permanent_conns;
    /* Reference counter for neighbors and rs */
    int ref_count;
};

/* Modify gs in-place to give a unique solution. guaranteed is whether a
 * solution is guaranteed to exist, i.e. did we generate the puzzle or was
 * it user input. */
bool unique_solution(game_state* gs,
                     bool guaranteed,
                     random_state* rs,
                     struct unique_solution_ctx* ctx) {
    int w = gs->w, h = gs->h;
    int i, j;
    bool final_answer = false;

    if (!ctx) {
        ctx = snew(struct unique_solution_ctx);
        ctx->opposite_ends = snewn(w * h + 2, int);
        ctx->can_connect = snewn(8 * w * h, bool);
        ctx->connected = snewn(8 * w * h, bool);
        ctx->permanent_conns = NULL;
        ctx->ref_count = 0;
        ctx->neighbors = snewn(8 * w * h, int);
        ctx->todo = tdq_new(w * h);
        tdq_fill(ctx->todo);

        for (i = 0; i < w * h; i++) {
            ctx->opposite_ends[i] = gs->grid[i] ? i : -1;

            for (j = 0; j < 8; j++) {
                ctx->neighbors[8 * i + j] =
                    attempt_move(i, knight_moves[j], w, h);
                ctx->can_connect[8 * i + j] =
                    ctx->neighbors[8 * i + j] > -1 && gs->grid[i] > 0;
                ctx->connected[8 * i + j] = false;
            }
        }
        ctx->opposite_ends[w * h] = w * h;
        ctx->opposite_ends[w * h + 1] = w * h + 1;
        connect_ends(ctx->opposite_ends, gs->ends[0], w * h);
        connect_ends(ctx->opposite_ends, gs->ends[1], w * h + 1);
    }

    int pos = tdq_remove(ctx->todo);
    while (pos > -1) {
        if (!gs->grid[pos]) {
            pos = tdq_remove(ctx->todo);
            continue;
        }

        int* neigh = ctx->neighbors + 8 * pos;
        bool* ccon = ctx->can_connect + 8 * pos;
        for (i = 0; i < 8; i++)
            if (ccon[i] && !ctx->connected[8 * pos + i] &&
                (ctx->opposite_ends[neigh[i]] == pos ||
                 ctx->opposite_ends[neigh[i]] == -1))
                ccon[i] = false;

        int even = ccon[0] + ccon[2] + ccon[4] + ccon[6];
        int odd = ccon[1] + ccon[3] + ccon[5] + ccon[7];

        if (gs->grid[pos] != 1 &&
            ((gs->grid[pos] == 2 && even == 1 && odd == 1) ||
             (gs->grid[pos] == 3 && (!even || !odd)) || even + odd < 2)) {
            final_answer = false;
            goto cleanup_and_return;
        }

        if (ctx->opposite_ends[pos] > -1 && gs->grid[pos] == 1) {
            if (even + odd == 1)
                for (i = 0; i < 8; i++)
                    if (ccon[i]) {
                        ctx->connected[8 * pos + i] = true;
                        ctx->connected[8 * neigh[i] + (i + 4) % 8] = true;
                        connect_ends(ctx->opposite_ends, pos, neigh[i]);
                        break;
                    }

        } else if (ctx->opposite_ends[pos] == pos && gs->grid[pos] == 2) {
            int min = min(even, odd), max = max(even, odd);
            if (min < 2 && max == 2) {
                for (i = 0; i < 8; i++) {
                    if (!ccon[i]) {
                        continue;
                    } else if (i % 2 == (even == min)) {
                        ctx->connected[8 * pos + i] = true;
                        ctx->connected[8 * neigh[i] + (i + 4) % 8] = true;
                        stdq_add(ctx->todo, ctx->opposite_ends[neigh[i]],
                                 w * h);
                        connect_ends(ctx->opposite_ends, pos, neigh[i]);
                    } else {
                        ctx->can_connect[8 * pos + i] = false;
                        ctx->can_connect[8 * neigh[i] + (i + 4) % 8] = false;
                    }
                    stdq_add(ctx->todo, neigh[i], w * h);
                }
            } else if (min == 1) {
                for (i = (min == odd); i < 8; i += 2)
                    if (ccon[i]) {
                        ctx->can_connect[8 * pos + i] = false;
                        ctx->can_connect[8 * neigh[i] + (i + 4) % 8] = false;
                        stdq_add(ctx->todo, neigh[i], w * h);
                        break;
                    }
            }

        } else if (ctx->opposite_ends[pos] == pos && gs->grid[pos] == 3) {
            int even_odd[2] = {even, odd};
            for (j = 0; j < 2; j++)
                if (even_odd[j] == 1) {
                    stdq_add(ctx->todo, pos, w * h);
                    for (i = j; i < 8; i += 2) {
                        stdq_add(ctx->todo, neigh[i], w * h);
                        if (ccon[i]) {
                            ctx->connected[8 * pos + i] = true;
                            ctx->connected[8 * neigh[i] + (i + 4) % 8] = true;
                            stdq_add(ctx->todo, ctx->opposite_ends[neigh[i]],
                                     w * h);
                            connect_ends(ctx->opposite_ends, pos, neigh[i]);
                        }
                    }
                }

        } else if (ctx->opposite_ends[pos] != pos &&
                   ctx->opposite_ends[pos] > -1) {
            int which = 8;
            for (i = 0; i < 8; i++)
                if (ctx->connected[8 * pos + i]) {
                    which = i;
                    break;
                }

            assert(which < 8);

            for (i = 0; i < 8; i++) {
                if (!ccon[i] || i == which)
                    continue;
                else if ((which + gs->grid[pos] + i) % 2 == 1 ||
                         (ctx->opposite_ends[pos] == neigh[i] &&
                          even + odd > 2)) {
                    ctx->can_connect[8 * pos + i] = false;
                    ctx->can_connect[8 * neigh[i] + (i + 4) % 8] = false;
                    stdq_add(ctx->todo, neigh[i], w * h);
                    if (ctx->opposite_ends[pos] == neigh[i])
                        stdq_add(ctx->todo, pos, w * h);
                } else if (ctx->opposite_ends[pos] == neigh[i]) {
                    assert(even + odd == 2);
                    final_answer = false;
                    goto cleanup_and_return;
                } else if (even + odd == 2) {
                    ctx->connected[8 * pos + i] = true;
                    ctx->connected[8 * neigh[i] + (i + 4) % 8] = true;
                    stdq_add(ctx->todo, neigh[i], w * h);
                    stdq_add(ctx->todo, ctx->opposite_ends[pos], w * h);
                    stdq_add(ctx->todo, ctx->opposite_ends[neigh[i]], w * h);
                    connect_ends(ctx->opposite_ends, pos, neigh[i]);
                }
            }
        }

        if (ctx->opposite_ends[pos] == -1) {
            for (i = 0; i < 8; i++)
                if (ccon[i] && !ctx->connected[8 * pos + i]) {
                    ctx->can_connect[8 * pos + i] = false;
                    ctx->can_connect[8 * neigh[i] + (i + 4) % 8] = false;
                    stdq_add(ctx->todo, neigh[i], w * h);
                }
        }

        pos = tdq_remove(ctx->todo);
    }

    int start = random_upto(rs, w * h);
    pos = (start + 1) % (w * h);
    for (; pos != start; pos = (pos + 1) % (w * h))
        if (ctx->opposite_ends[pos] != -1)
            break;

    if (pos == start) {
        /* All cells are part of the tour.
         * This is a uniquely solvable puzzle! */
        final_answer = true;
        if (guaranteed) {
            for (i = 0; i < ctx->ref_count; i++) {
                int perm = ctx->permanent_conns[i];
                int pos = perm / 8, move = perm % 8, start = pos;
                int index = 2 * pos + (gs->conn_pairs[2 * pos] != '8');

                gs->conn_pairs[index] = move + '0';

                pos = pos + knight_moves[move].y * w + knight_moves[move].x;
                move = (move + 4) % 8;
                index = 2 * pos + (gs->conn_pairs[2 * pos] != '8');

                gs->conn_pairs[index] = move + '0';

                connect_ends(gs->opposite_ends, start, pos);
            }
        } else {
            /* FIXME: The user wants their grid solved. Encode
             * the whole solved gamestate. */
        }
        goto cleanup_and_return;
    }

    int index, *neigh = ctx->neighbors + 8 * pos;
    for (index = 0; index < 8; index++) {
        if (!ctx->can_connect[8 * pos + index] ||
            ctx->connected[8 * pos + index])
            continue;

        /* Duplicate ctx for recursive call */
        struct unique_solution_ctx* new_ctx = snew(struct unique_solution_ctx);
        new_ctx->opposite_ends = snewn(w * h + 2, int);
        new_ctx->can_connect = snewn(8 * w * h, bool);
        new_ctx->connected = snewn(8 * w * h, bool);
        new_ctx->ref_count = ctx->ref_count + 1;
        new_ctx->permanent_conns = snewn(new_ctx->ref_count, int);
        new_ctx->neighbors = ctx->neighbors;
        new_ctx->todo = tdq_new(w * h);

        for (i = 0; i < w * h; i++) {
            new_ctx->opposite_ends[i] = ctx->opposite_ends[i];

            for (j = 0; j < 8; j++) {
                new_ctx->can_connect[8 * i + j] = ctx->can_connect[8 * i + j];
                new_ctx->connected[8 * i + j] = ctx->connected[8 * i + j];
            }
        }
        new_ctx->opposite_ends[w * h] = ctx->opposite_ends[w * h];
        new_ctx->opposite_ends[w * h + 1] = ctx->opposite_ends[w * h + 1];

        for (i = 0; i < new_ctx->ref_count - 1; i++)
            new_ctx->permanent_conns[i] = ctx->permanent_conns[i];

        /* Assume a connection and check if solvable */
        new_ctx->connected[8 * pos + index] = true;
        new_ctx->connected[8 * neigh[index] + (index + 4) % 8] = true;
        stdq_add(new_ctx->todo, pos, w * h);
        stdq_add(new_ctx->todo, neigh[index], w * h);
        stdq_add(new_ctx->todo, new_ctx->opposite_ends[pos], w * h);
        stdq_add(new_ctx->todo, new_ctx->opposite_ends[neigh[index]], w * h);
        connect_ends(new_ctx->opposite_ends, pos, neigh[index]);
        new_ctx->permanent_conns[new_ctx->ref_count - 1] = 8 * pos + index;

        for (i = (index + gs->grid[pos] + 1) % 2; i < 8; i += 2)
            if (i != index && new_ctx->can_connect[8 * pos + i]) {
                new_ctx->can_connect[8 * pos + i] = false;
                new_ctx->can_connect[8 * neigh[i] + (i + 4) % 8] = false;
            }
        for (i = (index + gs->grid[neigh[index]] + 1) % 2; i < 8; i += 2)
            if (i != (index + 4) % 8 &&
                new_ctx->can_connect[8 * neigh[index] + i]) {
                new_ctx->can_connect[8 * neigh[index] + i] = false;
                new_ctx->can_connect[8 * ctx->neighbors[8 * neigh[index] + i] +
                                     (i + 4) % 8] = false;
            }

        for (i = 0; i < 8; i++) {
            stdq_add(new_ctx->todo, neigh[i], w * h);
            stdq_add(new_ctx->todo, new_ctx->neighbors[8 * neigh[index] + i],
                     w * h);
        }

        final_answer = unique_solution(gs, guaranteed, rs, new_ctx);

        if (final_answer)
            break;
    }

cleanup_and_return:

    sfree(ctx->connected);
    sfree(ctx->opposite_ends);
    sfree(ctx->can_connect);
    sfree(ctx->permanent_conns);
    tdq_free(ctx->todo);
    if (ctx->ref_count == 0)
        sfree(ctx->neighbors);
    sfree(ctx);
    return final_answer;
}

static char* new_game_desc(const game_params* params,
                           random_state* rs,
                           char** aux,
                           bool interactive) {
    int w = params->w, h = params->h;
    assert(w > 5 && h > 5);
    struct game_state* gs;

generate_grid:
    gs = init_game_state(w, h);
    gs->nunvisited = random_upto(rs, MAX_UNVISITED);
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
            /* Warnsdorff heuristic failed or a tour is impossible, restart
             */
            free_game(gs);
            sfree(moves);
            goto generate_grid;
        }

        if (prev_move_i != -1)
            /* 2=orthogonal turns, 3=non-orthogonal turns */
            gs->grid[pos] = (prev_move_i + move_i) % 2 + 2;

        pos = next_pos;
        gs->grid[pos] = 1;
        moves[gs->ncells - cells_left - 1] = move_i;
    }

    gs->ends[1] = pos;

    unique_solution(gs, true, rs, NULL);

    /* ==== Convert to string ==== */
    char *string = snewn(6 * w * h, char), *p = string;
    for (i = 0; i < w * h; i++)
        sprintf(p++, "%d", gs->grid[i]);
    *(p++) = '.';
    for (i = 0; i < 2 * w * h; i++) {
        int move = gs->conn_pairs[i] - '0';
        if (move < 8 && i / 2 < attempt_move(i / 2, knight_moves[move], w, h)) {
            sprintf(p, "%d%d.", move, i / 2);
            while (isdigit(*(++p)))
                ;
            p++;
        }
    }

    *(--p) = '\0'; /* Replace last period with null character */
    free_game(gs);
    sfree(moves);
    return string;
}

static const char* validate_desc(const game_params* params, const char* desc) {
    return NULL;
}

static game_state* new_game(midend* me,
                            const game_params* params,
                            const char* desc) {
    const char* p = desc;
    int w = params->w, h = params->h;
    game_state* gs = init_game_state(w, h);

    int i;
    for (i = 0; i < w * h; i++) {
        int c = *p - '0';
        p++;
        gs->grid[i] = c;
        if (c == 1) {
            gs->ends[gs->ends > 0 ? 1 : 0] = i;
        } else if (c == 0) {
            gs->nunvisited++;
            gs->opposite_ends[i] = -1;
        }
    }
    gs->ncells = w * h - gs->nunvisited;

    while (*p) {
        int i = *(++p) - '0', pos, n;
        sscanf(++p, "%d%n", &pos, &n);
        int index = 2 * pos + (gs->conn_pairs[2 * pos] != '8');
        gs->conn_pairs[index] = i + '0';
        gs->start_pairs[index] = true;

        int start = pos;
        pos = pos + knight_moves[i].y * w + knight_moves[i].x;
        index = 2 * pos + (gs->conn_pairs[2 * pos] != '8');

        gs->conn_pairs[index] = (i + 4) % 8 + '0';
        gs->start_pairs[index] = true;

        connect_ends(gs->opposite_ends, start, pos);

        p += n;
    }

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
        ret->opposite_ends[i] = state->opposite_ends[i];
        ret->conn_pairs[2 * i] = state->conn_pairs[2 * i];
        ret->conn_pairs[2 * i + 1] = state->conn_pairs[2 * i + 1];
        ret->start_pairs[2 * i] = state->start_pairs[2 * i];
        ret->start_pairs[2 * i + 1] = state->start_pairs[2 * i + 1];
    }

    return ret;
}

static char* solve_game(const game_state* state,
                        const game_state* currstate,
                        const char* aux,
                        const char** error) {
    /* FIXME: Actually solve the game for the user. */
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
    bool visible;

    /* 0=don't show destinations, 1,2,3=show destinations. Using 2 and 3
     * disambiguates the first move of a cell when using arrow key controls
     * while 1 shows all. With 2 or 3, the moves shown are slanted left or
     * right from the main axis. */
    int show_dests;
};

static game_ui* new_ui(const game_state* state) {
    game_ui* ui = snew(game_ui);
    ui->cx = ui->cy = 0;
    ui->visible = false;
    ui->show_dests = 0;
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
    if (0 <= newstate->cx && 0 <= newstate->cy) {
        ui->visible = true;
        ui->cx = newstate->cx;
        ui->cy = newstate->cy;
        char* conns =
            newstate->conn_pairs + 2 * (ui->cy * newstate->w + ui->cx);
        if (conns[0] < '8' && conns[1] < '8') {
            if (ui->show_dests == 1)
                ui->visible = false;
            else
                ui->show_dests = 0;
        }
    } else {
        if (ui->show_dests == 1)
            ui->visible = false;
        else
            ui->show_dests = 0;
    }
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
    button = button & ~MOD_MASK;
    if (button == 'w' || button == 'W')
        button = CURSOR_UP;
    else if (button == 'd' || button == 'D')
        button = CURSOR_RIGHT;
    else if (button == 's' || button == 'S')
        button = CURSOR_DOWN;
    else if (button == 'a' || button == 'A')
        button = CURSOR_LEFT;

    if (button != CURSOR_UP && button != CURSOR_DOWN && button != CURSOR_LEFT &&
        button != CURSOR_RIGHT && button != CURSOR_SELECT &&
        button != CURSOR_SELECT2 && button != '\b' && button != LEFT_RELEASE)
        return NULL;

    int w = state->w, h = state->h;
    x = (x - BORDER) / ds->tilesize;
    y = (y - BORDER) / ds->tilesize;

    if (button == LEFT_RELEASE) {
        if (x < 0 || x >= w || y < 0 || y >= h || !state->grid[y * w + x])
            return NULL;

        if (!ui->visible) {
            ui->visible = true;
            ui->show_dests = 1;
            ui->cx = x;
            ui->cy = y;
            return UI_UPDATE;
        }

        if (ui->cx == x && ui->cy == y) {
            if (ui->show_dests != 1)
                ui->show_dests = 1;
            else
                ui->visible = false;
            return UI_UPDATE;
        }
        ui->show_dests = 1;

        int dx = x - ui->cx, dy = y - ui->cy;
        if (min(abs(dx), abs(dy)) != 1 || max(abs(dx), abs(dy)) != 2) {
            /* Not a knights move away, change position
             * instead of making a move */
            ui->cx = x;
            ui->cy = y;
            return UI_UPDATE;
        }

        /* This ugly formula finds the index i of {dx,dy} in knight_moves */
        int i = (dx > 0 ? 2 : 5) + (dx / abs(dx)) * (dy + (dy > 0 ? -1 : 0));

        int cur_pos = ui->cy * w + ui->cx;
        char* cur_conns = state->conn_pairs + 2 * cur_pos;
        bool* start_conns = state->start_pairs + 2 * cur_pos;

        if ((start_conns[0] && cur_conns[0] - '0' == i) ||
            (start_conns[1] && cur_conns[1] - '0' == i)) {
            ui->cx = x;
            ui->cy = y;
            return UI_UPDATE;
        }

        int new_pos = y * w + x;
        char* new_conns = state->conn_pairs + 2 * new_pos;

        if (state->grid[new_pos] &&
            (new_conns[0] == '8' || new_conns[1] == '8' ||
             (new_conns[0] - '0' + 4) % 8 == i ||
             (new_conns[1] - '0' + 4) % 8 == i)) {
            char* buffer = snewn(15, char);
            sprintf(buffer, "%d%d%d", dx + 2, dy + 2, ui->cy * w + ui->cx);
            ui->cx = x;
            ui->cy = y;
            return buffer;
        }

        return NULL;
    }

    if (!ui->visible) {
        ui->visible = true;
        ui->show_dests = 0;
        return UI_UPDATE;
    }

    if (button == '\b') {
        printf("Deleting not implemented!\n");
        return NULL;
    }

    int cur_pos = ui->cy * w + ui->cx;
    char* cur_conns = state->conn_pairs + 2 * cur_pos;

    if (!ui->show_dests) {
        if (button == CURSOR_UP) {
            ui->cy = max(ui->cy - 1, 0);
        } else if (button == CURSOR_DOWN) {
            ui->cy = min(ui->cy + 1, state->h - 1);
        } else if (button == CURSOR_LEFT) {
            ui->cx = max(ui->cx - 1, 0);
        } else if (button == CURSOR_RIGHT) {
            ui->cx = min(ui->cx + 1, state->w - 1);
        } else if (state->grid[cur_pos]) {
            ui->show_dests = 2;
        }
        return UI_UPDATE;
    }

    if (button == CURSOR_SELECT || button == CURSOR_SELECT2) {
        if (cur_conns[0] == '8' && cur_conns[1] == '8')
            ui->show_dests = (ui->show_dests + 1) % 4;
        else
            ui->show_dests = (ui->show_dests ? 0 : 2);
        return UI_UPDATE;
    }

    int moves[2];
    if (button == CURSOR_RIGHT) {
        moves[0] = 1;
        moves[1] = 2;
    } else if (button == CURSOR_DOWN) {
        moves[0] = 3;
        moves[1] = 4;
    } else if (button == CURSOR_LEFT) {
        moves[0] = 5;
        moves[1] = 6;
    } else {
        moves[0] = 7;
        moves[1] = 0;
    }

    int i;
    for (i = 0; i < 2; i++) {
        int new = attempt_move(cur_pos, knight_moves[moves[i]], w, h);
        char* new_conns = state->conn_pairs + 2 * new;

        if (new == -1 || !state->grid[cur_pos] || !state->grid[new])
            moves[i] = -1;
        else if (((cur_conns[0] < '8' && cur_conns[1] < '8') ||
                  (new_conns[0] < '8' && new_conns[1] < '8')) &&
                 (i + 4) % 8 + '0' != new_conns[0] &&
                 (i + 4) % 8 + '0' != new_conns[1])
            moves[i] = -1;
        else if ((cur_conns[0] < '8' || cur_conns[1] < '8') &&
                 (cur_conns[0] + cur_conns[1] + state->grid[cur_pos] + i + 1) %
                     2)
            moves[i] = -1;
    }

    if (moves[0] == -1 && moves[1] == -1)
        return NULL;

    if (moves[0] > -1 && moves[1] > -1) {
        if (ui->show_dests == 1) {
            ui->show_dests = 2;
            return UI_UPDATE;
        } else if (ui->show_dests == 3) {
            moves[0] = moves[1];
        }
    } else if (moves[0] == -1) {
        moves[0] = moves[1];
    }

    char* buffer = snewn(50, char);
    point move = knight_moves[moves[0]];
    int new_pos = cur_pos + move.y * w + move.x;

    sprintf(buffer, "%d%d%d", move.x + 2, move.y + 2, cur_pos);
    ui->cx = new_pos % w;
    ui->cy = new_pos / w;
    return buffer;
}

/* Helper for execute_move(). Find the cell at the end of a path. */
int follow_path(game_state* gs, int start) {
    char* conns = gs->conn_pairs + 2 * start;
    int pos = start, i = conns[conns[0] == '8'] - '0';
    do {
        pos = pos + knight_moves[i].y * gs->w + knight_moves[i].x;
        conns = gs->conn_pairs + 2 * pos;
        i = conns[conns[0] - '0' == (i + 4) % 8] - '0';
        if (conns[0] == '8' || conns[1] == '8') {
            return pos;
        }
    } while (pos != start);
    assert(!"follow_path() looped back to start");
}

static game_state* execute_move(const game_state* state, const char* move) {
    int size = state->w * state->h;
    game_state* gs = dup_game(state);
    const char* s = move;
    int dx = *s - '0' - 2, dy = *(++s) - '0' - 2, pos = atoi(++s);
    if (min(abs(dx), abs(dy)) != 1 || max(abs(dx), abs(dy)) != 2 || pos < 0 ||
        pos >= size) {
        return NULL;
    }

    /* This ugly formula finds the index i of {dx, dy} in knight_moves */
    int i = (dx > 0 ? 2 : 5) + (dx / abs(dx)) * (dy + (dy > 0 ? -1 : 0));

    int start = pos;
    char* start_conns = gs->conn_pairs + 2 * start;
    if (i != start_conns[0] - '0' && i != start_conns[1] - '0') {
        /* This is a new connection, not a backtrack. */
        start_conns[start_conns[1] == '8'] = i + '0';
        pos = pos + dy * gs->w + dx;
        char* conns = gs->conn_pairs + 2 * pos;
        i = (i + 4) % 8;
        conns[conns[1] == '8'] = i + '0';

        if (gs->opposite_ends[start] == pos) {
            /* This connection creates a loop, mark each cell with -3 */
            gs->opposite_ends[start] = -3;
            gs->opposite_ends[pos] = -3;
            int new_pos = pos;
            while (new_pos != start) {
                i = (conns[0] - '0' == i ? conns[1] : conns[0]) - '0';
                new_pos =
                    new_pos + knight_moves[i].y * gs->w + knight_moves[i].x;
                conns = gs->conn_pairs + 2 * new_pos;
                i = (i + 4) % 8;
                gs->opposite_ends[new_pos] = -3;
            }
        } else {
            connect_ends(gs->opposite_ends, start, pos);

            if (gs->opposite_ends[pos] == -1 &&
                (conns[0] + conns[1] + gs->grid[pos]) % 2 == 1)
                gs->opposite_ends[pos] = -2;

            if (gs->opposite_ends[start] == -1 &&
                (start_conns[0] + start_conns[1] + gs->grid[start]) % 2 == 1)
                gs->opposite_ends[start] = -2;
        }
    } else {
        /* The user is backtracking, remove the connection */
        start_conns[i == start_conns[1] - '0'] = '8';
        pos = pos + dy * gs->w + dx;
        char* conns = gs->conn_pairs + 2 * pos;
        i = (i + 4) % 8;
        conns[i == conns[1] - '0'] = '8';

        if (gs->opposite_ends[start] == -3) {
            /* This move removes an edge from a loop. Remove all error marks
             * unless the connection angles are incorrect (in that case, set
             * them to -2). */
            int new_pos = pos;
            while (new_pos != start) {
                i = conns[i == conns[0] - '0'] - '0';
                new_pos =
                    new_pos + knight_moves[i].y * gs->w + knight_moves[i].x;
                conns = gs->conn_pairs + 2 * new_pos;
                i = (i + 4) % 8;
                if ((conns[0] + conns[1] + gs->grid[new_pos]) % 2 == 0)
                    gs->opposite_ends[new_pos] = -1;
                else
                    gs->opposite_ends[new_pos] = -2;
            }
            gs->opposite_ends[start] = pos;
            gs->opposite_ends[pos] = start;
        } else {
            if (gs->opposite_ends[start] < 0) {
                gs->opposite_ends[start] = follow_path(gs, start);
                gs->opposite_ends[gs->opposite_ends[start]] = start;
            } else {
                gs->opposite_ends[start] = start;
            }

            if (gs->opposite_ends[pos] < 0) {
                gs->opposite_ends[pos] = follow_path(gs, pos);
                gs->opposite_ends[gs->opposite_ends[pos]] = pos;
            } else {
                gs->opposite_ends[pos] = pos;
            }
        }
    }

    gs->cx = pos % gs->w;
    gs->cy = pos / gs->w;

    return gs;
}

/* ================ Drawing routines ================ */

enum {
    COL_BACKGROUND,
    COL_OUTLINE,
    COL_PATH,
    COL_SELECTED,
    COL_ERROR,
    NCOLOURS,
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

    ret[COL_OUTLINE * 3 + 0] = 0.5;
    ret[COL_OUTLINE * 3 + 1] = 0.5;
    ret[COL_OUTLINE * 3 + 2] = 0.5;

    ret[COL_PATH * 3 + 0] = 0.0;
    ret[COL_PATH * 3 + 1] = 0.0;
    ret[COL_PATH * 3 + 2] = 0.0;

    ret[COL_SELECTED * 3 + 0] = 0.4;
    ret[COL_SELECTED * 3 + 1] = 0.4;
    ret[COL_SELECTED * 3 + 2] = 1.0;

    ret[COL_ERROR * 3 + 0] = 1.0;
    ret[COL_ERROR * 3 + 1] = 0.2;
    ret[COL_ERROR * 3 + 2] = 0.2;

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
    for (i = 0; i < w * h; i++) {
        int x = i % w, y = i / w;
        if (!state->grid[i]) {
            draw_rect(dr, BORDER + x * ds->tilesize, BORDER + y * ds->tilesize,
                      ds->tilesize, ds->tilesize, COL_OUTLINE);
        } else if (state->grid[i] == 2) {
            draw_rect(dr, BORDER + (x + 0.4) * ds->tilesize,
                      BORDER + (y + 0.4) * ds->tilesize, ds->tilesize * 0.2 + 1,
                      ds->tilesize * 0.2 + 1, COL_OUTLINE);
        } else if (state->grid[i] == 1) {
            draw_circle(dr, BORDER + (x + 0.5) * ds->tilesize,
                        BORDER + (y + 0.5) * ds->tilesize, 0.2 * ds->tilesize,
                        COL_BACKGROUND, COL_OUTLINE);
        }
    }

    /* Cursor and Available moves */
    if (ui->visible) {
        int i; /* I kinda like this fancy, if oddlooking, cursor */
        for (i = 0; i < 3; i++)
            draw_rect_corners(dr, BORDER + (ui->cx + 0.5) * ds->tilesize,
                              BORDER + (ui->cy + 0.5) * ds->tilesize,
                              ds->tilesize / 4 + i, COL_SELECTED);

        if (ui->show_dests) {
            int i, dest[8], cur_pos = (ui->cy * w + ui->cx);
            char* cur_conns = state->conn_pairs + 2 * cur_pos;
            bool cur_filled = cur_conns[0] < '8' && cur_conns[1] < '8';

            /* Find all possible moves */
            for (i = 0; i < 8; i++) {
                int new = attempt_move(cur_pos, knight_moves[i], w, h);
                if (new == -1) {
                    dest[i] = -1;
                    continue;
                }
                char* new_conns = state->conn_pairs + 2 * new;
                bool new_filled = new_conns[0] < '8' && new_conns[1] < '8';

                if (!state->grid[cur_pos] || !state->grid[new])
                    dest[i] = -1;
                else if (cur_filled || new_filled)
                    dest[i] = -1;
                else if ((cur_conns[0] + cur_conns[1] + state->grid[cur_pos] +
                          i) %
                             2 &&
                         (cur_conns[0] < '8' || cur_conns[1] < '8'))
                    dest[i] = -1;
                else
                    dest[i] = new;
            }

            /* Disambiguate moves for keyboard controls */
            if (ui->show_dests > 1)
                for (i = 0; i < 4; i++)
                    if (dest[2 * i] > -1 && dest[(2 * i + 7) % 8] > -1) {
                        if (ui->show_dests == 3)
                            dest[(2 * i + 7) % 8] = -1;
                        else
                            dest[2 * i] = -1;
                    }

            /* Outline reachable destinations */
            for (i = 0; i < 8; i++)
                if (dest[i] > -1)
                    draw_rect_corners(
                        dr, BORDER + (dest[i] % w + 0.5) * ds->tilesize,
                        BORDER + (dest[i] / w + 0.5) * ds->tilesize,
                        ds->tilesize / 4, COL_SELECTED);
        }
    }

    /* Tour path */
    for (i = 0; i < 2 * w * h; i++) {
        if (state->conn_pairs[i] != '8') {
            point move = knight_moves[state->conn_pairs[i] - '0'];
            int a = i / 2, b = a + move.y * w + move.x;

            int ax = (a % w + 0.5) * ds->tilesize + BORDER,
                ay = (a / w + 0.5) * ds->tilesize + BORDER,
                bx = (b % w + 0.5) * ds->tilesize + BORDER,
                by = (b / w + 0.5) * ds->tilesize + BORDER,
                color = (state->opposite_ends[a] < -1
                             ? COL_ERROR
                             : state->start_pairs[i] ? COL_OUTLINE : COL_PATH),
                dx = bx - ax, dy = by - ay;

            draw_line(dr, ax, ay, bx - dx / 2, by - dy / 2, color);
        }
    }

    /* Cell Bulbs */
    for (i = 0; i < w * h; i++)
        if (state->opposite_ends[i] < 0 && state->conn_pairs[2 * i] < '8') {
            int color = state->opposite_ends[i] == -1 ? COL_PATH : COL_ERROR,
                x = (i % w + 0.5) * ds->tilesize + BORDER,
                y = (i / w + 0.5) * ds->tilesize + BORDER;
            draw_circle(dr, x, y, 0.1 * ds->tilesize, COL_PATH, color);
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

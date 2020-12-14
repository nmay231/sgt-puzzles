#include <assert.h>
#include "puzzles.h"

void _gen_puzzle(struct game_state* gs, int* unvisited);
void print_grid(unsigned char* grid, int w, int h);

struct P {
    int a;
    int b;
};

/**
 * All of the knight's moves ordered clockwise.
 * knight_moves[0] is approximately 1:15 o'clock.
 *
 * Note: since moves that are orthogonal to each other are 2, 4,
 * or 6 apart in the list, it will be common to add the indexes
 * of two moves (mod 2) to check if they are orthogonal (or not).
 */
const struct P knight_moves[8] = {
    {1, -2}, {2, -1}, {2, 1}, {1, 2}, {-1, 2}, {-2, 1}, {-2, -1}, {-1, -2},
};

/**
 * all_moves has all of the knights moves and a "move
 * nowhere" that helps with the solving algorithm.
 */
const struct P all_moves[9] = {
    {1, -2}, {2, -1},  {2, 1},   {1, 2}, {-1, 2},
    {-2, 1}, {-2, -1}, {-1, -2}, {0, 0},
};

struct game_state {
    /** 0=initialized, 1=generated, 2=solved */
    int status;
    /** width, height, width*height, number unvisited */
    int w, h, size, unvisitedn;
    /** start/end of knight's tour */
    int ends[2];

    /**
     * A WxH array with values:
     *    9. Initialization value (means nothing)
     *    0. Unvisited cell
     *    1. An endpoint, there are only two in the grid
     *    2. A cell where an orthogonal turn was made
     *    3. A cell where a non-orthogonal turn was made
     */
    unsigned char* grid;

    /** An array containing indexes of knight_moves, length=w*h-unvisitedn-1 */
    unsigned int* moves;

    random_state* rs;

    struct solutions* solutions;
};

/**
 * A linked list containing:
 *
 *  - solution: an array of int pairs
 *       -1: The "moves" of an unvisited cell (aka invalid)
 *      0-7: index of knight's moves in and out
 *        8: There are only two cells with this, one for each endpoint
 *
 *  - endpoints: A dictionary linking endpoints of unfinished
 *    paths in the solution. Length is same as solution.
 */
struct solutions {
    struct solutions* next;
    struct P* solution;
    unsigned int* endpoints;
};

struct game_state* new_game_state(int w, int h) {
    struct game_state* gs = snew(struct game_state);
    gs->w = w;
    gs->h = h;
    gs->size = w * h;

    gs->status = 0;
    gs->ends[0] = gs->ends[1] = -1;
    gs->rs = random_new("123456", 6);
    gs->unvisitedn = random_upto(gs->rs, w + h);

    gs->grid = snewn(gs->size, unsigned char);
    gs->moves = snewn(gs->size - 1, unsigned int);

    for (int i = 0; i < gs->size; i++)
        gs->grid[i] = gs->moves[i] = 9;

    return gs;
}

int* unique_random_upto(random_state* rs, int length, int max) {
    int* arr = snewn(length, int);

    for (int i = 0; i < length; i++) {
        arr[i] = random_upto(rs, max);
        for (int j = 0; j < i; j++)
            if (arr[i] == arr[j]) {
                i--;
                break;
            }
    }

    return arr;
}

struct game_state* gen_puzzle(int w, int h) {
    assert(w > 5 && h > 5);
    struct game_state* gs = new_game_state(w, h);

    while (gs->status == 0) {
        gs = new_game_state(w, h);

        int* unvisited =
            unique_random_upto(gs->rs, gs->unvisitedn + 1, gs->size);
        gs->ends[0] = unvisited[gs->unvisitedn];
        // srealloc(unvisited, (w + h) * sizeof(int));

        _gen_puzzle(gs, unvisited);
        sfree(unvisited);
    }

    return gs;
}

int attempt_move(int pos, struct P move, int w, int h) {
    int newx = (pos % w) + move.b, new_pos = pos + (move.b * w) + move.a;
    if ((0 <= newx) && newx < w && (0 <= new_pos) && new_pos < w * h)
        return new_pos;
    return -1;
}

int num_neighbors(unsigned char* grid, int pos, int w, int h) {
    int count = 0;
    for (int i = 0; i < 8; i++) {
        struct P move = knight_moves[i];
        int neighbor = attempt_move(pos, move, w, h);
        if (neighbor >= 0 && grid[neighbor] == 9)
            count++;
    }
    return count;
}

void _gen_puzzle(struct game_state* gs, int* unvisited) {
    gs->grid[gs->ends[0]] = 1;
    int pos = gs->ends[0];

    for (int i = 0; i < gs->unvisitedn; i++)
        gs->grid[unvisited[i]] = 0;

    int moves_i[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int move_i = -1, prev_move_i;

    int cells_left = gs->size - gs->unvisitedn - 1;
    int total_cells = cells_left;

    while (cells_left > 0) {
        shuffle(moves_i, 8, sizeof(int), gs->rs);
        int min_neigh = 8, next_pos = -1;
        prev_move_i = move_i;

        for (int i = 0; i < 8; i++) {
            int tmp = attempt_move(pos, knight_moves[moves_i[i]], gs->w, gs->h);

            if (tmp >= 0 && gs->grid[tmp]) {
                int num = num_neighbors(gs->grid, tmp, gs->w, gs->h);

                if (num < min_neigh) {
                    min_neigh = num;
                    next_pos = tmp;
                    move_i = moves_i[i];
                }
            }
        }

        if (next_pos == -1) {
            return;
        }

        if (prev_move_i != -1)
            // 2=orthogonal turns, 3=non-orthogonal turns
            gs->grid[pos] = (prev_move_i + move_i) % 2 + 2;

        pos = next_pos;
        gs->grid[pos] = 1;
        gs->moves[total_cells - cells_left] = move_i;
        cells_left--;
    }

    gs->status = 1;
}

struct solutions* new_solutions(int length) {
    struct solutions* s = snew(struct solutions);
    s->next = NULL;
    s->solution = snewn(length, struct P);
    s->endpoints = snewn(length, int);

    for (int i = 0; i < length; i++) {
        s->solution[i].a = -1;
        s->solution[i].b = -1;
        s->endpoints[i] = 0;
    }

    return s;
}

int main() {
    const int W = 6, H = 6;
    struct game_state* gs = gen_puzzle(W, H);

    print_grid(gs->grid, W, H);
}

int my_rand(int end) {
    return end * random() / RAND_MAX;
}

int* my_rand_n(int end, int len) {
    int* ints = calloc(len, sizeof(int));
    for (int i = 0; i < len; i++)
        ints[i] = my_rand(8);
    return ints;
}

struct P* get_moves(int len) {
    struct P* moves = calloc(len, sizeof(struct P));
    for (int i = 0; i < len; i++)
        moves[i] = knight_moves[my_rand(8)];

    return moves;
}

struct cell_info {
    bool is_conn[8];
    int conns[2];
};

void init_cell_info(struct cell_info* info) {
    info->conns[0] = info->conns[1] = false;
    for (int i = 0; i < 8; i++)
        info->is_conn[i] = 0;
}

struct linked {
    struct linked* prev;
    struct linked* next;
    void* val;
};

static int pairs[28][2] = {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6},
                           {0, 7}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6},
                           {1, 7}, {2, 3}, {2, 4}, {2, 5}, {2, 6}, {2, 7},
                           {3, 4}, {3, 5}, {3, 6}, {3, 7}, {4, 5}, {4, 6},
                           {4, 7}, {5, 6}, {5, 7}, {6, 7}};

int valid_move(int pos, struct P move) {
    return 1;
}

int solve(int* grid, int w) {}

void print_grid(unsigned char* grid, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        printf("%d", grid[i]);
        if (i % w == w - 1)
            printf("\n");
    }
}

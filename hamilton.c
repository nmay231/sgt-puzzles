/*
 * Library code to find a Hamilton cycle of a graph, in a randomised
 * enough way that the resulting cycle could be used as the solution
 * to a puzzle.
 */

/*
 * Requirements
 * ============
 *
 * The problem of finding _whether_ a Hamilton cycle exists is
 * NP-complete. This code doesn't attempt to do that efficiently, or
 * even at all: if you give it a borderline graph in which there's
 * only one cycle, it may very well not find it within reasonable time
 * (_even_ by the standards of algorithms for NP-complete problems),
 * and if there isn't a cycle at all, it won't even terminate. The aim
 * here is more modest: in the kind of graph where Hamilton cycles are
 * _plentiful_, pick one at random with a reasonably even
 * distribution. An example is choosing a knight's tour of a
 * chessboard.
 *
 * (What do I mean by 'a reasonably even distribution'? Definitely not
 * 'make every possible cycle equiprobable', or anything else as
 * precisely defined as that. But I would at least like there not to
 * be any kind of a qualitative bias. For example, if you were to
 * construct a cycle by starting at an arbitrary point, following
 * edges according to some heuristic, and hoping to get back to where
 * you started, then you might well find that the initial part of the
 * cycle, where all outgoing edges from each vertex were still
 * available, was generated with a noticeably different probability
 * distribution from the final part where you're constrained to use
 * the only edges still unused. The algorithm we actually use avoids
 * privileging any particular point within the cycle, so it should
 * avoid any bias of that kind.)
 *
 * Algorithm
 * =========
 *
 * The approach we use here is a heuristic neural-net algorithm, as
 * described in
 *
 *   Y. Takefuji, K. C. Lee. "Neural network computing for knight's
 *   tour problems." Neurocomputing, 4(5):249–254, 1992.
 *
 * which I originally found via Wikipedia:
 *
 *   https://en.wikipedia.org/wiki/Knight's_tour
 *
 * Paraphrased in my own words for people who don't speak fluent
 * neural net (including myself): the basic idea is that our working
 * state is an arbitrary subset of the graph's edges, and we attempt
 * to evolve that subset iteratively (see below) until every vertex
 * has degree exactly 2. If we're successful in doing that, then we've
 * achieved a vertex cover of the graph consisting of some number of
 * disjoint cycles - but not necessarily _one_ single cycle containing
 * all the vertices. So we do a last-minute check to see if there is a
 * single length-n cycle, and if there isn't, just try again with a
 * re-randomised initial edge subset.
 *
 * Details
 * -------
 *
 * The iteration works by computing a value at every edge (of the
 * whole graph, not just our current subset) that indicates which
 * direction we'd like to adjust the edge in. Treating edges not in
 * the subset as having value 0 and edges that are in the subset as 1,
 * this means we want a negative number for edges we'd like to
 * consider removing, and a positive number for edges we'd like to
 * consider adding. Our aim is that every vertex should have degree 2,
 * so our adjustment value for a given edge is given by adding the
 * degrees of its two endpoint vertices and subtracting that from the
 * expected total of 4.
 *
 * For each edge, we keep a cumulative total of all the adjustment
 * values we've computed in the whole run. When that total gets above
 * a certain threshold (indicating that edges in that vicinity have
 * been over-plentiful for some time), we turn the edge off (if it was
 * previously on); when it gets below a certain threshold (indicating
 * that we've been wanting more edges around here), we turn it off (if
 * it was on).
 *
 * The key point is that there's a gap between those two thresholds,
 * so when we've _only just_ turned an edge on, it will take a few
 * iterations before we get unhappy enough about it to try turning it
 * off again, and in the intervening iterations, we hope that some
 * other edge in the same area of the graph will be turned off first.
 * In this way the algorithm tries to avoid flip-flopping back and
 * forth between the same pair of unsatisfactory states, and instead
 * tends to propagate the effects of a change to other parts of the
 * graph and hope the effects meet up again elsewhere and cancel each
 * other out.
 *
 * I haven't mentioned neural nets in that explanation at all. But
 * apparently among the simplified models of a neuron used in neural
 * net theory is a thing called a "hysteresis McCulloch-Pitts neuron",
 * which has this general behaviour of 'switch on when your cumulative
 * state gets above _this_ threshold, and then don't switch off again
 * until it drops below _that_ rather lower threshold'. So you can
 * regard this algorithm (and its authors do) as having one of those
 * neurons per graph edge.
 *
 * Termination
 * -----------
 *
 * If, in a given iteration, we find that _all_ the adjustment values
 * we computed were exactly zero, that means that every edge has the
 * property that the degrees of its endpoint vertices sum to 4. That's
 * our condition for terminating the neural-net iteration and doing
 * last-minute checks to see if we've really found a Hamilton cycle.
 *
 * The first of those checks is to make sure every individual vertex
 * _actually_ has degree 2! It is possible to find a graph in which
 * every edge has sum(endpoint degrees) = 4, but the vertices don't
 * all have degree 2. For example, suppose the input graph was a
 * Y-shape, with three outlying vertices (with degree 1) all connected
 * to a central one (with degree 3). If our algorithm chooses the
 * improper subset consisting of all three edges of that graph, then
 * it will satisfy the termination condition despite not containing
 * any kind of cycle at all, let alone a Hamilton cycle.
 *
 * Then we check that there's exactly one cycle covering all the
 * edges, and we're done.
 *
 * If these checks fail, then we re-randomise the initial state, and
 * start all over again. There's no attempt to skew the main algorithm
 * towards producing a single long cycle; that would require
 * evaluating some kind of global condition during the iteration
 * rather than a purely local condition at each edge. We just keep
 * trying until we happen to get lucky.
 *
 * Non-convergence
 * ---------------
 *
 * There's no guarantee that this algorithm will converge _at all_ to
 * a stable configuration. Often it doesn't, or doesn't do so quickly
 * enough to suit us.
 *
 * So we have to layer another bodgy heuristic on top of the neural
 * net: we'd like to run it for a reasonable number of iterations
 * (enough that it has at least a good chance of converging), and
 * then, if it failed, reset to a fresh random starting state and
 * retry. This prevents non-termination because we got stuck in a
 * closed readjustment loop, or something like one.
 *
 * Unfortunately, the 'reasonable number of iterations' limit isn't a
 * constant: for different graphs, you might need to set it
 * differently. So we don't hard-code one here. Instead, we start it
 * off reasonably small, and increase it if we detect that not many of
 * our attempts are converging (specifically, if under 1/3 of them
 * are).
 *
 * Mysterious constants / future possible work
 * ===========================================
 *
 * Wikipedia's description of this algorithm sets the neuron
 * activation and deactivation thresholds to 3 and 0 respectively
 * (with all neurons' cumulative adjustment levels starting out at 0).
 * But I've found that setting them to 12,0 causes a much higher
 * proportion of successful convergence, in the initial use case of
 * generating knight's tours for boards of sizes (say) 5-10.
 *
 * However, there's a size of knight's tour (about 24x24) beyond which
 * the algorithm either suddenly stops converging at all, or becomes
 * suddenly too slow to wait for. It's possible that adjusting this
 * magic constant might help. (Wikipedia shows an example of a
 * successful generation at 24x24, so presumably _some_ interpretation
 * of this algorithm is capable of succeeding at that size!)
 *
 * The original paper describes the algorithm in a more continuous
 * fashion, so that the adjustment score is not a _delta_ between
 * discrete iterations, but a _derivative_ as the neurons' states
 * continuously evolve over time. That suggests an alternative
 * event-driven implementation strategy: compute the delta _per unit
 * time_ for each neuron (based on the current set of active neurons /
 * included edges), and then find the smallest t > 0 such that
 * advancing time by that much will cause some neuron to have just
 * crossed one of its thresholds. Then update every neuron's level by
 * t*delta, enact the set of flip(s) that result, and recompute the
 * deltas. However, I haven't tried this version (if nothing else, I'm
 * a bit scared by it wanting to use floating point).
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "puzzles.h"

typedef struct hamilton_private hamilton_private;
typedef struct hamilton_edge hamilton_edge;

struct hamilton_edge {
    int level;     /* cumulative adjustment value for this 'neuron' */
    bool active;   /* whether the edge is in our subset right now */

    /* List of edges that are neighbours of this one, i.e. share
     * exactly one vertex with it. 'neighbours' here is a pointer into
     * the larger 'neighbourlists' array in hamilton_private, so that
     * that can be deallocated all in one go. */
    size_t nneighbours;
    hamilton_edge **neighbours;

    /* The vertices at each end of this edge. Not used in the main
     * algorithm, but it's how the user provides the edges to us in
     * the first place, and used again to generate our output. */
    unsigned ends[2];
};

/* Typedef for an array of two edges, to keep complex type
 * declarations less unreadable */
typedef hamilton_edge *edge_ptr_pair[2];

struct hamilton_private {
    size_t nvertices;
    unsigned start_vertex; /* which vertex the user wants us to start
                            * the output cycle at */

    /* Store the edges of the graph, with all the per-edge data */
    hamilton_edge *edges;
    size_t nedges, edgesize;

    /* Whether we've run hamilton_prepare(). Before this, the user can
     * add edges to the graph. Afterwards, it's too late to change
     * it. */
    bool ready;

    /* Whether we're generating a Hamilton cycle or a Hamilton path. */
    bool is_path;

    /* Storage for the neighbours[] list in each edge. */
    hamilton_edge **neighbourlists;

    /* Enough scratch space to store some data for each vertex of the
     * graph. Used during setup and output */
    unsigned *vdegree;
    edge_ptr_pair *vedges;

    /* The sub-structure that our end user gets a pointer to */
    struct hamilton public;
};

/* Activation thresholds for our 'neuron'. We turn an edge on when its
 * level exceeds ON_THRESHOLD, and off when its level is less than
 * OFF_THRESHOLD. */
#define ON_THRESHOLD 12
#define OFF_THRESHOLD 0

struct hamilton *hamilton_cycle_new(unsigned nvertices, unsigned start_vertex)
{
    hamilton_private *h = snew(hamilton_private);
    h->nvertices = nvertices;
    h->start_vertex = start_vertex;
    h->nedges = h->edgesize = 0;
    h->edges = NULL;
    h->neighbourlists = NULL;
    h->public.output_vertices = snewn(nvertices, unsigned);
    h->vdegree = snewn(nvertices, unsigned);
    h->vedges = snewn(nvertices, edge_ptr_pair);
    h->ready = false;
    h->is_path = false;
    return &h->public;
}

static hamilton_private *hamilton_to_private(struct hamilton *public)
{
    return (hamilton_private *)
        ((char *)public - offsetof(struct hamilton_private, public));
}

struct hamilton *hamilton_path_new(unsigned nvertices)
{
    /*
     * To find a non-cyclic Hamilton _path_ in a graph, we add an
     * imaginary extra vertex that connects to all the user-provided
     * vertices. Then a Hamilton cycle in the augmented graph can be
     * turned back into a Hamilton path in the original one by
     * removing the extra vertex.
     *
     * So all we do here is to call the main init function
     * hamilton_cycle_new with slightly adjusted parameters, add all
     * the graph edges to the extra vertex, and set a flag reminding
     * us to exclude the extra vertex from our output later.
     */
    struct hamilton *public = hamilton_cycle_new(nvertices+1, nvertices);
    hamilton_private *h = hamilton_to_private(public);
    for (int i = 0; i < nvertices; i++)
        hamilton_add_edge(public, i, nvertices);
    h->is_path = true;
    return public;
}

void hamilton_free(struct hamilton *public)
{
    hamilton_private *h = hamilton_to_private(public);
    sfree(h->public.output_vertices);
    sfree(h->edges);
    sfree(h->neighbourlists);
    sfree(h->vdegree);
    sfree(h->vedges);
    sfree(h);
}

void hamilton_add_edge(struct hamilton *public, unsigned v1, unsigned v2)
{
    hamilton_private *h = hamilton_to_private(public);
    assert(!h->ready && "Can't call hamilton_add_edge after hamilton_run");
    if (h->nedges >= h->edgesize) {
        h->edgesize = h->nedges * 5 / 4 + 128;
        h->edges = sresize(h->edges, h->edgesize, hamilton_edge);
    }
    hamilton_edge *e = &h->edges[h->nedges++];
    e->ends[0] = v1;
    e->ends[1] = v2;
}

static void hamilton_prepare(hamilton_private *h)
{
    if (h->ready)
        return;
    h->ready = true;

    /*
     * Count up the degree of each vertex.
     */
    for (size_t i = 0; i < h->nvertices; i++)
        h->vdegree[i] = 0;
    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        for (size_t i = 0; i < 2; i++) {
            unsigned v = e->ends[i];
            h->vdegree[v]++;
        }
    }

    /*
     * The length of each edge's neighbour list equals the sum of its
     * ends' degrees, minus 2 (because that sum counts the edge itself
     * twice).
     */
    size_t neighbourlist_size = 0;
    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        e->nneighbours = 0;
        for (size_t i = 0; i < 2; i++)
            e->nneighbours += h->vdegree[e->ends[i]];
        e->nneighbours -= 2;
        neighbourlist_size += e->nneighbours;
    }

    /*
     * Allocate a block of space for the neighbour lists, and set up
     * all their start pointers to point into that block.
     */
    h->neighbourlists = snewn(neighbourlist_size, hamilton_edge *);
    size_t neighbourlist_pos = 0;
    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        e->neighbours = h->neighbourlists + neighbourlist_pos;
        neighbourlist_pos += e->nneighbours;

        /* Reset nneighbours back to zero so we can use it as a
         * counter while we're filling in the lists in the next loop. */
        e->nneighbours = 0;
    }

    /*
     * Make a list of the edges neighbouring each vertex. We only need
     * this within this function, which only gets run once during the
     * life cycle of a 'struct hamilton', so we'll just allocate and
     * free it locally rather than keeping it in h itself.
     */
    size_t degree_sum = 0;
    for (size_t i = 0; i < h->nvertices; i++)
        degree_sum += h->vdegree[i];
    unsigned *vpos = snewn(h->nvertices, unsigned);
    hamilton_edge **edgelist_store = snewn(degree_sum, hamilton_edge *);
    hamilton_edge ***edgelists = snewn(h->nvertices, hamilton_edge **);
    size_t edgelist_pos = 0;
    for (size_t i = 0; i < h->nvertices; i++) {
        edgelists[i] = edgelist_store + edgelist_pos;
        edgelist_pos += h->vdegree[i];
        vpos[i] = 0;
    }
    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        for (size_t i = 0; i < 2; i++) {
            unsigned v = e->ends[i];
            edgelists[v][vpos[v]++] = e;
        }
    }

    /*
     * Fill in the edges' neighbour lists, by iterating over all pairs
     * of edges adjacent to the same vertex.
     */
    for (size_t i = 0; i < h->nvertices; i++) {
        for (size_t j = 0; j+1 < h->vdegree[i]; j++) {
            hamilton_edge *ej = edgelists[i][j];
            for (size_t k = j+1; k < h->vdegree[i]; k++) {
                hamilton_edge *ek = edgelists[i][k];
                ej->neighbours[ej->nneighbours++] = ek;
                ek->neighbours[ek->nneighbours++] = ej;
            }
        }
    }

    /*
     * Phew! We're done. Free temporaries.
     */
    sfree(vpos);
    sfree(edgelist_store);
    sfree(edgelists);
}

static bool hamilton_iteration(hamilton_private *h)
{
    bool stable = true;

    /*
     * Update level for all edges.
     */
    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        /*
         * We want to compute (2 - deg(V)) for each vertex at the ends
         * of this edge, and add those two values.
         *
         * So we start with a score of 4. Any edge _neighbouring_ e
         * subtracts 1 (because it must meet exactly one of e's two
         * endpoints). But e itself, if active, subtracts two (because
         * it meets both its endpoints).
         */
        int delta = 4;
        for (size_t i = 0; i < e->nneighbours; i++)
            delta -= e->neighbours[i]->active;
        delta -= 2 * e->active;

        if (delta)
            stable = false;

        e->level += delta;
    }

    /*
     * Turn edges on and off. This is done in a separate pass so that
     * all the work above was computed as if in parallel, all based on
     * the previous value of all the edges.
     */
    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        if (e->level > ON_THRESHOLD*4)
            e->active = true;
        else if (e->level < OFF_THRESHOLD)
            e->active = false;
    }

    /*
     * Return true if no levels had to be adjusted at all, i.e. we
     * have converged.
     */
    return stable;
}

static size_t hamilton_try_converge(hamilton_private *h, size_t iter_limit,
                                    random_state *rs)
{
    /*
     * Initialise the state randomly.
     */
    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        e->level = 0;
        e->active = random_upto(rs, 2);
    }

    /*
     * Try the specified number of iterations. If successful, return
     * how many iterations we actually needed. (That must be at least
     * 1, so the value 0 is left free to indicate failure.)
     *
     * (That return value isn't currently used. But it was useful to
     * put in print statements during debugging, and it could be
     * useful in future if we try to make the adaptive iteration limit
     * more sophisticated.)
     */
    for (size_t iter = 0; iter < iter_limit; iter++) {
        if (hamilton_iteration(h))
            return iter + 1;
    }

    /*
     * If we get here, we didn't converge at all.
     */
    return 0;
}

static bool hamilton_check_result(hamilton_private *h)
{
    /*
     * Check that each vertex have degree exactly 2, and record the
     * two edges meeting there (for tracing round the cycle in the
     * next loop).
     */
    for (size_t i = 0; i < h->nvertices; i++)
        h->vdegree[i] = 0;

    for (hamilton_edge *e = h->edges; e < h->edges + h->nedges; e++) {
        if (!e->active)
            continue;

        for (size_t i = 0; i < 2; i++) {
            unsigned v = e->ends[i];

            if (h->vdegree[v] >= 2)
                return false;              /* vertex has too-high degree */
            h->vedges[v][h->vdegree[v]++] = e;
        }
    }

    for (size_t i = 0; i < h->nvertices; i++)
        if (h->vdegree[i] != 2)
            return false;              /* vertex has wrong degree */

    /*
     * Now we're sure that we've covered the graph's vertex set with a
     * collection of vertex-disjoint cycles. But there might be more
     * than one of them, in which case this attempt is still
     * unsuccessful.
     *
     * Trace around a single cycle of our collection and count its
     * length. While we're at it, we may as well write the output.
     */
    unsigned vertex = h->start_vertex;
    hamilton_edge *edge = h->vedges[vertex][0];

    size_t outpos = 0;
    for (size_t i = 0; i < h->nvertices; i++) {
        if (i != 0 && vertex == h->start_vertex)
            return false;              /* cycle was too short */
        if (!(h->is_path && i == 0))   /* omit the extra vertex in path mode */
            h->public.output_vertices[outpos++] = vertex;
        vertex = edge->ends[0] + edge->ends[1] - vertex;
        edge = (h->vedges[vertex][0] == edge ? h->vedges[vertex][1] :
                h->vedges[vertex][0]);
    }

    if (vertex != h->start_vertex)
        return false;                  /* cycle was too long (?!) */

    return true;
}

void hamilton_run(struct hamilton *public, random_state *rs)
{
    hamilton_private *h = hamilton_to_private(public);

    hamilton_prepare(h);

    /*
     * Initial iteration limit.
     */
    size_t iter_limit = 100;
    size_t nfail = 0, nok = 0;

    while (true) {
        size_t niter = hamilton_try_converge(h, iter_limit, rs);
        if (niter) {
#ifdef DEBUG_CONVERGENCE
            printf("converged after %zu (limit %zu)\n", niter, iter_limit);
#endif
            nok++;
        } else {
#ifdef DEBUG_CONVERGENCE
            printf("failed at limit %zu\n", iter_limit);
#endif
            nfail++;
            if (nok < nfail / 2) {
                /* If we've had twice as many convergence failures as
                 * successes with this iteration limit, then probably
                 * it's set too low. Increase it, and reset the
                 * failure count (on the grounds that a failure at the
                 * _old_ iter limit tells us nothing about the new
                 * one). */
                iter_limit = iter_limit * 3 / 2;
#ifdef DEBUG_CONVERGENCE
                printf("%zu successes, %zu fails, going to %zu\n",
                       nok, nfail, iter_limit);
#endif
                nfail = 0;
            }
            /* Now loop round again. */
            continue;
        }

        /*
         * Now our neural net has converged to something it's happy
         * with. See if it's found what we were actually looking for.
         */
        if (hamilton_check_result(h)) {
            /* Successfully generated a cycle! The final thing we have
             * to do is to reverse it with probability 1/2, to prevent
             * directional bias from the order of edges given as input
             * to the algorithm. */
            if (random_upto(rs, 2)) {
                size_t i, j;
                if (h->is_path) {
                    /* h->nvertices counts one more vertex than we're
                     * outputting, so we want to reverse all the first
                     * (h->nvertices-1) elements of the array. */
                    i = 0;
                    j = h->nvertices - 2;
                } else {
                    /* We want to reverse all but the first element of
                     * the array, because that's the one the user
                     * asked us to start the cycle at. */
                    i = 1;
                    j = h->nvertices - 1;
                }
                while (j > i) {
                    unsigned tmp = h->public.output_vertices[i];
                    h->public.output_vertices[i] =
                        h->public.output_vertices[j];
                    h->public.output_vertices[j] = tmp;
                    i++, j--;
                }
            }

            return;
        }
    }
}

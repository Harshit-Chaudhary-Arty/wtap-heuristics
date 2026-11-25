#ifndef LOCAL_SEARCH_THRESHOLD_H
#define LOCAL_SEARCH_THRESHOLD_H

#include "wtap.h"

#include "local_search_circular.h"

/* Default minimum objective improvement required to apply a move. */
#define DEFAULT_MIN_IMPROVEMENT 0.01

/* Smallest k-cycle this solver will run. k = 2 is one pairwise rotation. */
#define THRESHOLD_MIN_CYCLE_SIZE 2

/*
 * Phased local search with configurable k-cycles and minimum improvement threshold.
 *
 * Same structure as improve_with_circular_local_search, but each phase stops when
 * the best move improves the objective by at most min_improvement.
 *
 * cycle_size may be 2. Values below 2 are raised to 2. Values above
 * MAX_CYCLE_SIZE are clamped.
 * min_improvement = 0.0 accepts any strictly improving move.
 */
void improve_with_threshold_circular_local_search(WTAPInstance *inst, int cycle_size,
                                                  double min_improvement);

#endif /* LOCAL_SEARCH_THRESHOLD_H */

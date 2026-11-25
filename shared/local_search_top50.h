#ifndef LOCAL_SEARCH_TOP50_H
#define LOCAL_SEARCH_TOP50_H

#include "wtap.h"

#include <stdint.h>

typedef struct {
    double initial_objective;
    double final_objective;
    int rounds;
    int relocation_moves;
    int swap_moves;
    int cycle_moves;
    uint64_t cycle_candidates;
    uint64_t relocation_evaluations;
    uint64_t swap_evaluations;
    uint64_t cycle_evaluations;
} Top50SearchStats;

/*
 * Run global-greedy MMR followed by best-improvement relocation, swap,
 * and probability-ranked top-50% 3-cycle phases.
 *
 * Returns 1 on success and 0 if cycle-candidate allocation fails.
 */
int optimize_with_top50_local_search(WTAPInstance *inst, Top50SearchStats *stats);

#endif /* LOCAL_SEARCH_TOP50_H */

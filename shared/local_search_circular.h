#ifndef LOCAL_SEARCH_CIRCULAR_H
#define LOCAL_SEARCH_CIRCULAR_H

#include "wtap.h"

/* Maximum weapons in one circular k-cycle move. */
#define MAX_CYCLE_SIZE 16

/* Default k for n-choose-3 style circular moves. */
#define DEFAULT_CYCLE_SIZE 3

/*
 * Phased local search with configurable k-cycles.
 *
 * Each cycle:
 *   1. Relocate phase — exhaust best improving single-weapon relocates.
 *   2. Swap phase — exhaust best improving pair swaps (k = 2).
 *   3. k-cycle phase — for every C(n, k) weapon group, try shifts 1..k-1
 *      (weapon i receives the target of weapon (i + shift) mod k).
 *
 * cycle_size must be >= 3. Values above MAX_CYCLE_SIZE are clamped.
 * Repeat until a full cycle produces no improvement.
 */
void improve_with_circular_local_search(WTAPInstance *inst, int cycle_size);

#endif /* LOCAL_SEARCH_CIRCULAR_H */

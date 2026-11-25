#ifndef LOCAL_SEARCH_PHASED_H
#define LOCAL_SEARCH_PHASED_H

#include "wtap.h"

/*
 * Phased local search: alternate relocate and swap phases.
 *
 * Each cycle:
 *   1. Relocate phase — apply the best improving relocate move repeatedly
 *      until no relocate move improves the objective.
 *   2. Swap phase — apply the best improving swap move repeatedly
 *      until no swap move improves the objective.
 *
 * Repeat until a full cycle produces no improvement.
 */
void improve_with_phased_local_search(WTAPInstance *inst);

#endif /* LOCAL_SEARCH_PHASED_H */

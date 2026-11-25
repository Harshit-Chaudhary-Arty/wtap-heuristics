#ifndef LOCAL_SEARCH_FAST_H
#define LOCAL_SEARCH_FAST_H

#include "wtap.h"

/*
 * Faster local search using cached per-target survival factors
 * and first-improvement (accept first improving move, restart).
 */
void improve_with_local_search_fast(WTAPInstance *inst);

#endif /* LOCAL_SEARCH_FAST_H */

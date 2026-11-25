#ifndef LOCAL_SEARCH_H
#define LOCAL_SEARCH_H

#include "wtap.h"

/*
 * Improve an assignment with relocate and swap moves.
 * Uses best-improvement: each pass applies the single best
 * improving move found, then repeats until none remain.
 */
void improve_with_local_search(WTAPInstance *inst);

#endif /* LOCAL_SEARCH_H */

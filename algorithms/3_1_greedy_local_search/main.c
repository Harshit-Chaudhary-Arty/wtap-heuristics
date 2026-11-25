/*
 * MMR + fast local search (cached survival, first-improvement)
 *
 * 1. Build an initial assignment with MMR.
 * 2. Improve with cached relocate/swap using first-improvement.
 */

#include "local_search_fast.h"
#include "mmr.h"
#include "runner.h"

static void solve(WTAPInstance *inst) {
    mmr_solve(inst);
    improve_with_local_search_fast(inst);
}

int main(int argc, char *argv[]) {
    return run_solver("3.1 - MMR + Fast Local Search (Cached, First-Improvement)", "results/algorithms/mmr-fast-local-search.csv",
                      solve, argc, argv);
}

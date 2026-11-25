/*
 * MMR + local search (relocate and swap)
 *
 * 1. Build an initial assignment with MMR.
 * 2. Improve it with relocate and swap moves until no better move remains.
 */

#include "local_search.h"
#include "mmr.h"
#include "runner.h"

static void solve(WTAPInstance *inst) {
    mmr_solve(inst);
    improve_with_local_search(inst);
}

int main(int argc, char *argv[]) {
    return run_solver("3 - MMR + Local Search (Relocate/Swap)", "results/algorithms/greedy-local-search.csv", solve, argc, argv);
}

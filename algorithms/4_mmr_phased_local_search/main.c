/*
 * MMR + phased local search (relocate-then-swap cycles)
 *
 * 1. Build an initial assignment with MMR (global selection).
 * 2. Improve with phased local search:
 *      a. Relocate phase — exhaust all improving relocates (best each step).
 *      b. Swap phase — exhaust all improving swaps (best each step).
 *      c. Repeat until a full cycle finds no improvement.
 */

#include "local_search_phased.h"
#include "mmr.h"
#include "runner.h"

static void solve(WTAPInstance *inst) {
    mmr_solve(inst);
    improve_with_phased_local_search(inst);
}

int main(int argc, char *argv[]) {
    return run_solver("4 - MMR + Phased Local Search (Relocate then Swap)", "results/algorithms/mmr-phased-local-search.csv", solve, argc, argv);
}

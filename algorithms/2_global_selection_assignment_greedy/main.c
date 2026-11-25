/*
 * Global selection assignment greedy (MMR)
 */

#include "mmr.h"
#include "runner.h"

static void solve(WTAPInstance *inst) {
    mmr_solve(inst);
}

int main(int argc, char *argv[]) {
    return run_solver("2 - Global Selection Assignment Greedy (MMR)", "results/algorithms/global-selection-greedy.csv", solve, argc, argv);
}

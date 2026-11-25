/*
 * MMR + phased local search with configurable k-cycles.
 *
 * 1. Build an initial assignment with MMR (global selection).
 * 2. Improve with phased local search:
 *      a. Relocate phase — exhaust best improving relocates.
 *      b. Swap phase — exhaust best improving pair swaps.
 *      c. k-cycle phase — for every C(n, k) weapon group, try shifts 1..k-1.
 *      d. Repeat until a full cycle finds no improvement.
 *
 * Usage:
 *   ./build/4_1_circular_local_search [cycle_size] [--save-csv]
 *
 * Uses data/Instances_for_GA.txt. cycle_size defaults to 3. Valid range: 3..16.
 */

#include "local_search_circular.h"
#include "mmr.h"
#include "output.h"
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>

static int cycle_size = DEFAULT_CYCLE_SIZE;

static void solve(WTAPInstance *inst) {
    mmr_solve(inst);
    improve_with_circular_local_search(inst, cycle_size);
}

int main(int argc, char *argv[]) {
    wtap_take_save_csv_flag(&argc, argv);

    if (argc > 1) {
        int parsed = atoi(argv[1]);
        if (parsed < DEFAULT_CYCLE_SIZE) {
            fprintf(stderr,
                    "Warning: cycle_size %d is below minimum %d; using %d.\n",
                    parsed, DEFAULT_CYCLE_SIZE, DEFAULT_CYCLE_SIZE);
            cycle_size = DEFAULT_CYCLE_SIZE;
        } else if (parsed > MAX_CYCLE_SIZE) {
            fprintf(stderr,
                    "Warning: cycle_size %d exceeds maximum %d; using %d.\n",
                    parsed, MAX_CYCLE_SIZE, MAX_CYCLE_SIZE);
            cycle_size = MAX_CYCLE_SIZE;
        } else {
            cycle_size = parsed;
        }
    }

    char algorithm_name[96];
    snprintf(algorithm_name, sizeof(algorithm_name),
             "4.1 - MMR + Circular LS (k=%d, shifts 1..%d)", cycle_size, cycle_size - 1);

    /* argc=1 so run_solver uses the default data/Instances_for_GA.txt */
    return run_solver(algorithm_name, "results/algorithms/mmr-circular-local-search.csv", solve, 1, argv);
}

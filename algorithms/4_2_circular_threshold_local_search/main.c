/*
 * MMR + phased circular local search with minimum improvement threshold.
 *
 * 1. Build an initial assignment with MMR (global selection).
 * 2. Improve with phased local search (relocate, swap, k-cycle), stopping each
 *    phase when the best move improves the objective by at most min_improvement.
 *
 * Usage:
 *   ./build/4_2_circular_threshold_local_search [cycle_size] [min_improvement] [--save-csv]
 *
 * Uses data/Instances_for_GA.txt.
 *   cycle_size defaults to 2 (valid range 2..16)
 *   min_improvement defaults to 0.01 (use 0 for strict convergence)
 */

#include "local_search_threshold.h"
#include "mmr.h"
#include "output.h"
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>

static int cycle_size = THRESHOLD_MIN_CYCLE_SIZE;
static double min_improvement = DEFAULT_MIN_IMPROVEMENT;

static void solve(WTAPInstance *inst) {
    mmr_solve(inst);
    improve_with_threshold_circular_local_search(inst, cycle_size, min_improvement);
}

int main(int argc, char *argv[]) {
    wtap_take_save_csv_flag(&argc, argv);

    if (argc > 1) {
        int parsed_k = atoi(argv[1]);
        if (parsed_k < THRESHOLD_MIN_CYCLE_SIZE) {
            fprintf(stderr,
                    "Warning: cycle_size %d is below minimum %d; using %d.\n",
                    parsed_k, THRESHOLD_MIN_CYCLE_SIZE, THRESHOLD_MIN_CYCLE_SIZE);
            cycle_size = THRESHOLD_MIN_CYCLE_SIZE;
        } else if (parsed_k > MAX_CYCLE_SIZE) {
            fprintf(stderr,
                    "Warning: cycle_size %d exceeds maximum %d; using %d.\n",
                    parsed_k, MAX_CYCLE_SIZE, MAX_CYCLE_SIZE);
            cycle_size = MAX_CYCLE_SIZE;
        } else {
            cycle_size = parsed_k;
        }
    }

    if (argc > 2) {
        min_improvement = atof(argv[2]);
        if (min_improvement < 0.0) {
            fprintf(stderr,
                    "Warning: min_improvement %.6f is negative; using 0.0.\n",
                    min_improvement);
            min_improvement = 0.0;
        }
    }

    char algorithm_name[128];
    snprintf(algorithm_name, sizeof(algorithm_name),
             "4.2 - MMR + Circular LS (k=%d, min_improvement=%.4f)",
             cycle_size, min_improvement);

    return run_solver(algorithm_name, "results/algorithms/mmr-circular-threshold.csv", solve, 1, argv);
}

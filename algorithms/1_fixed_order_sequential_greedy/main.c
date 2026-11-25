/*
 * Fixed-order sequential greedy
 *
 * Process weapons in index order (0, 1, 2, ...).
 * For each weapon, assign it to the target with the largest
 * marginal reduction at that moment:
 *   marginal_reduction = V[t] * survival_factor[t] * p[w][t]
 */

#include "runner.h"

static void solve(WTAPInstance *inst) {
    double survival_factor[MAX_TARGETS];

    for (int t = 0; t < inst->num_targets; t++) {
        survival_factor[t] = 1.0;
    }

    for (int w = 0; w < inst->num_weapons; w++) {
        int best_target = 0;
        double best_marginal = -1.0;

        for (int t = 0; t < inst->num_targets; t++) {
            double marginal = inst->target_values[t] * survival_factor[t] * inst->prob[w][t];

            if (marginal > best_marginal) {
                best_marginal = marginal;
                best_target = t;
            }
        }

        inst->assignment[w] = best_target;
        survival_factor[best_target] *= (1.0 - inst->prob[w][best_target]);
    }
}

int main(int argc, char *argv[]) {
    return run_solver("1 - Fixed-Order Sequential Greedy", "results/algorithms/fixed-order-sequential-greedy.csv", solve, argc, argv);
}

#include "objective.h"

/*
 * Expected surviving value of target t:
 *   E(V[t]) = V[t] * product of (1 - p[w][t]) for all weapons w on t
 *
 * Objective: minimize sum of E(V[t]) over all targets.
 */
double calculate_objective(const WTAPInstance *inst) {
    double total = 0.0;

    for (int t = 0; t < inst->num_targets; t++) {
        double survival_factor = 1.0;

        for (int w = 0; w < inst->num_weapons; w++) {
            if (inst->assignment[w] == t) {
                survival_factor *= (1.0 - inst->prob[w][t]);
            }
        }

        total += inst->target_values[t] * survival_factor;
    }

    return total;
}

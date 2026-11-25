#include "mmr.h"

/*
 * Repeatedly pick the unassigned weapon-target pair with the
 * largest marginal reduction:
 *   marginal_reduction = V[t] * survival_factor[t] * p[w][t]
 *
 * If forbidden_weapon >= 0, that weapon is never assigned to forbidden_target.
 */
void mmr_solve_forbidding(WTAPInstance *inst, int forbidden_weapon, int forbidden_target) {
    double survival_factor[MAX_TARGETS];
    int assigned[MAX_WEAPONS];

    for (int t = 0; t < inst->num_targets; t++) {
        survival_factor[t] = 1.0;
    }

    for (int w = 0; w < inst->num_weapons; w++) {
        assigned[w] = 0;
        inst->assignment[w] = -1;
    }

    for (int step = 0; step < inst->num_weapons; step++) {
        int best_weapon = 0;
        int best_target = 0;
        double best_marginal = -1.0;
        int found = 0;

        for (int w = 0; w < inst->num_weapons; w++) {
            if (assigned[w]) {
                continue;
            }

            for (int t = 0; t < inst->num_targets; t++) {
                if (w == forbidden_weapon && t == forbidden_target) {
                    continue;
                }

                double marginal = inst->target_values[t] * survival_factor[t] * inst->prob[w][t];

                if (!found || marginal > best_marginal) {
                    best_marginal = marginal;
                    best_weapon = w;
                    best_target = t;
                    found = 1;
                }
            }
        }

        if (!found) {
            break;
        }

        inst->assignment[best_weapon] = best_target;
        assigned[best_weapon] = 1;
        survival_factor[best_target] *= (1.0 - inst->prob[best_weapon][best_target]);
    }
}

void mmr_solve(WTAPInstance *inst) {
    mmr_solve_forbidding(inst, -1, -1);
}

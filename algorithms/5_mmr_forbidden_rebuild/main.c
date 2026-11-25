/*
 * MMR with one forbidden assignment (eminent-domain style)
 *
 * 1. Solve with MMR and keep that assignment.
 * 2. On the best assignment so far, find the weapon whose current
 *    shot reduces the objective the least, and that has not been tried.
 * 3. Forbid that weapon-target pair, wipe the assignment, and run MMR again.
 * 4. Keep whichever assignment has the smaller objective.
 * 5. Repeat until every weapon has been the forbidden one once.
 *
 * Each trial forbids only one pair. Forbidding a weak shot lets MMR
 * rebuild every later choice, which a single relocate cannot do.
 */

#include "mmr.h"
#include "objective.h"
#include "runner.h"

#include <string.h>

/* How much the objective would rise if this weapon were removed. */
static double shot_reduction(const WTAPInstance *inst, int weapon) {
    int target = inst->assignment[weapon];
    double survival_without = 1.0;

    for (int w = 0; w < inst->num_weapons; w++) {
        if (w == weapon) {
            continue;
        }
        if (inst->assignment[w] == target) {
            survival_without *= (1.0 - inst->prob[w][target]);
        }
    }

    return inst->target_values[target] * survival_without * inst->prob[weapon][target];
}

static void copy_assignment(int *dst, const int *src, int num_weapons) {
    memcpy(dst, src, (size_t)num_weapons * sizeof(int));
}

static void solve(WTAPInstance *inst) {
    int best_assignment[MAX_WEAPONS];
    int tried[MAX_WEAPONS];

    mmr_solve(inst);
    double best_objective = calculate_objective(inst);
    copy_assignment(best_assignment, inst->assignment, inst->num_weapons);

    for (int w = 0; w < inst->num_weapons; w++) {
        tried[w] = 0;
    }

    /* One forbid pass per weapon. */
    for (int pass = 0; pass < inst->num_weapons; pass++) {
        copy_assignment(inst->assignment, best_assignment, inst->num_weapons);

        int weakest = -1;
        double weakest_reduction = 0.0;

        for (int w = 0; w < inst->num_weapons; w++) {
            if (tried[w]) {
                continue;
            }

            double reduction = shot_reduction(inst, w);
            if (weakest < 0 || reduction < weakest_reduction) {
                weakest = w;
                weakest_reduction = reduction;
            }
        }

        tried[weakest] = 1;

        /* A weapon must still have somewhere to go. */
        if (inst->num_targets < 2) {
            break;
        }

        mmr_solve_forbidding(inst, weakest, best_assignment[weakest]);
        double objective = calculate_objective(inst);
        if (objective < best_objective) {
            best_objective = objective;
            copy_assignment(best_assignment, inst->assignment, inst->num_weapons);
        }
    }

    copy_assignment(inst->assignment, best_assignment, inst->num_weapons);
}

int main(int argc, char *argv[]) {
    return run_solver("5 - MMR Forbidden Rebuild", "results/algorithms/mmr-forbidden-rebuild.csv", solve, argc, argv);
}

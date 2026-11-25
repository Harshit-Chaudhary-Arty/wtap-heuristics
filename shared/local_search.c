#include "local_search.h"

#include "objective.h"

typedef enum {
    MOVE_NONE = 0,
    MOVE_RELOCATE,
    MOVE_SWAP
} MoveType;

typedef struct {
    MoveType type;
    int weapon;
    int target;
    int weapon_a;
    int weapon_b;
} Move;

static double target_survival_excluding(const WTAPInstance *inst, int t, int exclude_weapon) {
    double survival_factor = 1.0;

    for (int w = 0; w < inst->num_weapons; w++) {
        if (w == exclude_weapon) {
            continue;
        }
        if (inst->assignment[w] == t) {
            survival_factor *= (1.0 - inst->prob[w][t]);
        }
    }

    return survival_factor;
}

static double target_contribution(const WTAPInstance *inst, int t) {
    return inst->target_values[t] * target_survival_excluding(inst, t, -1);
}

static double evaluate_relocate(const WTAPInstance *inst, int weapon, int new_target, double current_total) {
    int old_target = inst->assignment[weapon];

    if (old_target == new_target) {
        return current_total;
    }

    double old_contribution_old_target = target_contribution(inst, old_target);
    double old_contribution_new_target = target_contribution(inst, new_target);

    double new_contribution_old_target =
        inst->target_values[old_target] * target_survival_excluding(inst, old_target, weapon);

    double new_contribution_new_target =
        inst->target_values[new_target] *
        target_survival_excluding(inst, new_target, -1) *
        (1.0 - inst->prob[weapon][new_target]);

    return current_total
           - old_contribution_old_target
           - old_contribution_new_target
           + new_contribution_old_target
           + new_contribution_new_target;
}

static double evaluate_swap(const WTAPInstance *inst, int weapon_a, int weapon_b, double current_total) {
    int target_a = inst->assignment[weapon_a];
    int target_b = inst->assignment[weapon_b];

    if (target_a == target_b) {
        return current_total;
    }

    double old_contribution_a = target_contribution(inst, target_a);
    double old_contribution_b = target_contribution(inst, target_b);

    double new_contribution_a =
        inst->target_values[target_a] *
        target_survival_excluding(inst, target_a, weapon_a) *
        (1.0 - inst->prob[weapon_b][target_a]);

    double new_contribution_b =
        inst->target_values[target_b] *
        target_survival_excluding(inst, target_b, weapon_b) *
        (1.0 - inst->prob[weapon_a][target_b]);

    return current_total
           - old_contribution_a
           - old_contribution_b
           + new_contribution_a
           + new_contribution_b;
}

static void apply_move(WTAPInstance *inst, const Move *move) {
    if (move->type == MOVE_RELOCATE) {
        inst->assignment[move->weapon] = move->target;
        return;
    }

    if (move->type == MOVE_SWAP) {
        int target_a = inst->assignment[move->weapon_a];
        int target_b = inst->assignment[move->weapon_b];
        inst->assignment[move->weapon_a] = target_b;
        inst->assignment[move->weapon_b] = target_a;
    }
}

void improve_with_local_search(WTAPInstance *inst) {
    for (;;) {
        double current_objective = calculate_objective(inst);
        double best_objective = current_objective;
        Move best_move = {MOVE_NONE, 0, 0, 0, 0};

        for (int w = 0; w < inst->num_weapons; w++) {
            int current_target = inst->assignment[w];

            for (int t = 0; t < inst->num_targets; t++) {
                if (t == current_target) {
                    continue;
                }

                double objective = evaluate_relocate(inst, w, t, current_objective);
                if (objective < best_objective) {
                    best_objective = objective;
                    best_move.type = MOVE_RELOCATE;
                    best_move.weapon = w;
                    best_move.target = t;
                }
            }
        }

        for (int w1 = 0; w1 < inst->num_weapons; w1++) {
            for (int w2 = w1 + 1; w2 < inst->num_weapons; w2++) {
                double objective = evaluate_swap(inst, w1, w2, current_objective);
                if (objective < best_objective) {
                    best_objective = objective;
                    best_move.type = MOVE_SWAP;
                    best_move.weapon_a = w1;
                    best_move.weapon_b = w2;
                }
            }
        }

        if (best_move.type == MOVE_NONE) {
            break;
        }

        apply_move(inst, &best_move);
    }
}

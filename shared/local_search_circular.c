#include "local_search_circular.h"

#include "objective.h"

typedef enum {
    MOVE_NONE = 0,
    MOVE_RELOCATE,
    MOVE_SWAP,
    MOVE_K_CYCLE
} MoveType;

typedef struct {
    MoveType type;
    int weapon;
    int target;
    int weapon_a;
    int weapon_b;
    int cycle_weapons[MAX_CYCLE_SIZE];
    int cycle_size;
    int shift;
} Move;

static int clamp_cycle_size(int cycle_size) {
    if (cycle_size < DEFAULT_CYCLE_SIZE) {
        return DEFAULT_CYCLE_SIZE;
    }
    if (cycle_size > MAX_CYCLE_SIZE) {
        return MAX_CYCLE_SIZE;
    }
    return cycle_size;
}

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

static int weapon_override_target(const WTAPInstance *inst, int weapon,
                                  const int *cycle_weapons, const int *new_targets, int cycle_size) {
    for (int i = 0; i < cycle_size; i++) {
        if (cycle_weapons[i] == weapon) {
            return new_targets[i];
        }
    }
    return inst->assignment[weapon];
}

static double target_contribution_with_cycle_overrides(const WTAPInstance *inst, int t,
                                                       const int *cycle_weapons,
                                                       const int *new_targets, int cycle_size) {
    double survival_factor = 1.0;

    for (int w = 0; w < inst->num_weapons; w++) {
        int assigned_target = weapon_override_target(inst, w, cycle_weapons, new_targets, cycle_size);
        if (assigned_target == t) {
            survival_factor *= (1.0 - inst->prob[w][t]);
        }
    }

    return inst->target_values[t] * survival_factor;
}

static int target_in_list(int target, const int *targets, int count) {
    for (int i = 0; i < count; i++) {
        if (targets[i] == target) {
            return 1;
        }
    }
    return 0;
}

static void add_unique_target(int target, int *targets, int *count) {
    if (!target_in_list(target, targets, *count)) {
        targets[*count] = target;
        (*count)++;
    }
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

static void build_shifted_targets(const WTAPInstance *inst, const int *cycle_weapons, int cycle_size,
                                  int shift, int *old_targets, int *new_targets) {
    for (int i = 0; i < cycle_size; i++) {
        old_targets[i] = inst->assignment[cycle_weapons[i]];
    }

    for (int i = 0; i < cycle_size; i++) {
        new_targets[i] = old_targets[(i + shift) % cycle_size];
    }
}

static int is_identity_shift(const int *old_targets, const int *new_targets, int cycle_size) {
    for (int i = 0; i < cycle_size; i++) {
        if (old_targets[i] != new_targets[i]) {
            return 0;
        }
    }
    return 1;
}

static double evaluate_k_cycle(const WTAPInstance *inst, const int *cycle_weapons, int cycle_size,
                               int shift, double current_total) {
    int old_targets[MAX_CYCLE_SIZE];
    int new_targets[MAX_CYCLE_SIZE];

    build_shifted_targets(inst, cycle_weapons, cycle_size, shift, old_targets, new_targets);

    if (is_identity_shift(old_targets, new_targets, cycle_size)) {
        return current_total;
    }

    int affected_targets[MAX_CYCLE_SIZE * 2];
    int affected_count = 0;

    for (int i = 0; i < cycle_size; i++) {
        add_unique_target(old_targets[i], affected_targets, &affected_count);
        add_unique_target(new_targets[i], affected_targets, &affected_count);
    }

    double new_total = current_total;

    for (int i = 0; i < affected_count; i++) {
        int t = affected_targets[i];
        new_total -= target_contribution(inst, t);
        new_total += target_contribution_with_cycle_overrides(
            inst, t, cycle_weapons, new_targets, cycle_size
        );
    }

    return new_total;
}

static void apply_relocate(WTAPInstance *inst, int weapon, int target) {
    inst->assignment[weapon] = target;
}

static void apply_swap(WTAPInstance *inst, int weapon_a, int weapon_b) {
    int target_a = inst->assignment[weapon_a];
    int target_b = inst->assignment[weapon_b];
    inst->assignment[weapon_a] = target_b;
    inst->assignment[weapon_b] = target_a;
}

static void apply_k_cycle(WTAPInstance *inst, const int *cycle_weapons, int cycle_size, int shift) {
    int old_targets[MAX_CYCLE_SIZE];
    int new_targets[MAX_CYCLE_SIZE];

    build_shifted_targets(inst, cycle_weapons, cycle_size, shift, old_targets, new_targets);

    for (int i = 0; i < cycle_size; i++) {
        inst->assignment[cycle_weapons[i]] = new_targets[i];
    }
}

static int find_best_relocate(const WTAPInstance *inst, double current_objective, Move *best_move,
                              double *best_objective) {
    int found = 0;
    best_move->type = MOVE_NONE;
    *best_objective = current_objective;

    for (int w = 0; w < inst->num_weapons; w++) {
        int current_target = inst->assignment[w];

        for (int t = 0; t < inst->num_targets; t++) {
            if (t == current_target) {
                continue;
            }

            double objective = evaluate_relocate(inst, w, t, current_objective);
            if (objective < *best_objective) {
                *best_objective = objective;
                best_move->type = MOVE_RELOCATE;
                best_move->weapon = w;
                best_move->target = t;
                found = 1;
            }
        }
    }

    return found;
}

static int find_best_swap(const WTAPInstance *inst, double current_objective, Move *best_move,
                          double *best_objective) {
    int found = 0;
    best_move->type = MOVE_NONE;
    *best_objective = current_objective;

    for (int w1 = 0; w1 < inst->num_weapons; w1++) {
        for (int w2 = w1 + 1; w2 < inst->num_weapons; w2++) {
            double objective = evaluate_swap(inst, w1, w2, current_objective);
            if (objective < *best_objective) {
                *best_objective = objective;
                best_move->type = MOVE_SWAP;
                best_move->weapon_a = w1;
                best_move->weapon_b = w2;
                found = 1;
            }
        }
    }

    return found;
}

static int find_best_k_cycle(const WTAPInstance *inst, int cycle_size, double current_objective,
                             Move *best_move, double *best_objective) {
    int found = 0;
    int combo[MAX_CYCLE_SIZE];

    best_move->type = MOVE_NONE;
    *best_objective = current_objective;

    if (cycle_size > inst->num_weapons) {
        return 0;
    }

    for (int i = 0; i < cycle_size; i++) {
        combo[i] = i;
    }

    for (;;) {
        for (int shift = 1; shift < cycle_size; shift++) {
            double objective = evaluate_k_cycle(inst, combo, cycle_size, shift, current_objective);
            if (objective < *best_objective) {
                *best_objective = objective;
                best_move->type = MOVE_K_CYCLE;
                best_move->cycle_size = cycle_size;
                best_move->shift = shift;
                for (int i = 0; i < cycle_size; i++) {
                    best_move->cycle_weapons[i] = combo[i];
                }
                found = 1;
            }
        }

        int idx = cycle_size - 1;
        while (idx >= 0 && combo[idx] == inst->num_weapons - cycle_size + idx) {
            idx--;
        }

        if (idx < 0) {
            break;
        }

        combo[idx]++;
        for (int j = idx + 1; j < cycle_size; j++) {
            combo[j] = combo[j - 1] + 1;
        }
    }

    return found;
}

static int run_relocate_phase(WTAPInstance *inst) {
    int improved = 0;

    for (;;) {
        double current_objective = calculate_objective(inst);
        Move best_move = {MOVE_NONE, 0, 0, 0, 0, {0}, 0, 0};
        double best_objective = current_objective;

        if (!find_best_relocate(inst, current_objective, &best_move, &best_objective)) {
            break;
        }

        apply_relocate(inst, best_move.weapon, best_move.target);
        improved = 1;
    }

    return improved;
}

static int run_swap_phase(WTAPInstance *inst) {
    int improved = 0;

    for (;;) {
        double current_objective = calculate_objective(inst);
        Move best_move = {MOVE_NONE, 0, 0, 0, 0, {0}, 0, 0};
        double best_objective = current_objective;

        if (!find_best_swap(inst, current_objective, &best_move, &best_objective)) {
            break;
        }

        apply_swap(inst, best_move.weapon_a, best_move.weapon_b);
        improved = 1;
    }

    return improved;
}

static int run_k_cycle_phase(WTAPInstance *inst, int cycle_size) {
    int improved = 0;

    for (;;) {
        double current_objective = calculate_objective(inst);
        Move best_move = {MOVE_NONE, 0, 0, 0, 0, {0}, 0, 0};
        double best_objective = current_objective;

        if (!find_best_k_cycle(inst, cycle_size, current_objective, &best_move, &best_objective)) {
            break;
        }

        apply_k_cycle(inst, best_move.cycle_weapons, best_move.cycle_size, best_move.shift);
        improved = 1;
    }

    return improved;
}

void improve_with_circular_local_search(WTAPInstance *inst, int cycle_size) {
    int k = clamp_cycle_size(cycle_size);

    for (;;) {
        int relocate_improved = run_relocate_phase(inst);
        int swap_improved = run_swap_phase(inst);
        int cycle_improved = 0;

        if (k <= inst->num_weapons) {
            cycle_improved = run_k_cycle_phase(inst, k);
        }

        if (!relocate_improved && !swap_improved && !cycle_improved) {
            break;
        }
    }
}

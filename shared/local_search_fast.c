#include "local_search_fast.h"

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

typedef struct {
    double survival[MAX_TARGETS];
    double contribution[MAX_TARGETS];
    double total;
} ObjectiveCache;

static void init_cache(const WTAPInstance *inst, ObjectiveCache *cache) {
    cache->total = 0.0;

    for (int t = 0; t < inst->num_targets; t++) {
        cache->survival[t] = 1.0;

        for (int w = 0; w < inst->num_weapons; w++) {
            if (inst->assignment[w] == t) {
                cache->survival[t] *= (1.0 - inst->prob[w][t]);
            }
        }

        cache->contribution[t] = inst->target_values[t] * cache->survival[t];
        cache->total += cache->contribution[t];
    }
}

static void update_target_cache(const WTAPInstance *inst, ObjectiveCache *cache, int target) {
    cache->survival[target] = 1.0;

    for (int w = 0; w < inst->num_weapons; w++) {
        if (inst->assignment[w] == target) {
            cache->survival[target] *= (1.0 - inst->prob[w][target]);
        }
    }

    cache->contribution[target] = inst->target_values[target] * cache->survival[target];
}

static void recompute_total(ObjectiveCache *cache, int num_targets) {
    cache->total = 0.0;

    for (int t = 0; t < num_targets; t++) {
        cache->total += cache->contribution[t];
    }
}

static double evaluate_relocate(const WTAPInstance *inst, int weapon, int new_target,
                                const ObjectiveCache *cache) {
    int old_target = inst->assignment[weapon];

    if (old_target == new_target) {
        return cache->total;
    }

    double new_contribution_old_target =
        inst->target_values[old_target] *
        cache->survival[old_target] /
        (1.0 - inst->prob[weapon][old_target]);

    double new_contribution_new_target =
        inst->target_values[new_target] *
        cache->survival[new_target] *
        (1.0 - inst->prob[weapon][new_target]);

    return cache->total
           - cache->contribution[old_target]
           - cache->contribution[new_target]
           + new_contribution_old_target
           + new_contribution_new_target;
}

static double evaluate_swap(const WTAPInstance *inst, int weapon_a, int weapon_b,
                            const ObjectiveCache *cache) {
    int target_a = inst->assignment[weapon_a];
    int target_b = inst->assignment[weapon_b];

    if (target_a == target_b) {
        return cache->total;
    }

    double new_contribution_a =
        inst->target_values[target_a] *
        cache->survival[target_a] /
        (1.0 - inst->prob[weapon_a][target_a]) *
        (1.0 - inst->prob[weapon_b][target_a]);

    double new_contribution_b =
        inst->target_values[target_b] *
        cache->survival[target_b] /
        (1.0 - inst->prob[weapon_b][target_b]) *
        (1.0 - inst->prob[weapon_a][target_b]);

    return cache->total
           - cache->contribution[target_a]
           - cache->contribution[target_b]
           + new_contribution_a
           + new_contribution_b;
}

static void apply_relocate(WTAPInstance *inst, int weapon, int new_target, ObjectiveCache *cache) {
    int old_target = inst->assignment[weapon];

    inst->assignment[weapon] = new_target;
    update_target_cache(inst, cache, old_target);
    update_target_cache(inst, cache, new_target);
    recompute_total(cache, inst->num_targets);
}

static void apply_swap(WTAPInstance *inst, int weapon_a, int weapon_b, ObjectiveCache *cache) {
    int target_a = inst->assignment[weapon_a];
    int target_b = inst->assignment[weapon_b];

    inst->assignment[weapon_a] = target_b;
    inst->assignment[weapon_b] = target_a;
    update_target_cache(inst, cache, target_a);
    update_target_cache(inst, cache, target_b);
    recompute_total(cache, inst->num_targets);
}

static void apply_move(WTAPInstance *inst, const Move *move, ObjectiveCache *cache) {
    if (move->type == MOVE_RELOCATE) {
        apply_relocate(inst, move->weapon, move->target, cache);
        return;
    }

    if (move->type == MOVE_SWAP) {
        apply_swap(inst, move->weapon_a, move->weapon_b, cache);
    }
}

static int find_first_improving_move(const WTAPInstance *inst, const ObjectiveCache *cache,
                                     Move *move) {
    for (int w = 0; w < inst->num_weapons; w++) {
        int current_target = inst->assignment[w];

        for (int t = 0; t < inst->num_targets; t++) {
            if (t == current_target) {
                continue;
            }

            if (evaluate_relocate(inst, w, t, cache) < cache->total) {
                move->type = MOVE_RELOCATE;
                move->weapon = w;
                move->target = t;
                return 1;
            }
        }
    }

    for (int w1 = 0; w1 < inst->num_weapons; w1++) {
        for (int w2 = w1 + 1; w2 < inst->num_weapons; w2++) {
            if (evaluate_swap(inst, w1, w2, cache) < cache->total) {
                move->type = MOVE_SWAP;
                move->weapon_a = w1;
                move->weapon_b = w2;
                return 1;
            }
        }
    }

    return 0;
}

void improve_with_local_search_fast(WTAPInstance *inst) {
    ObjectiveCache cache;
    init_cache(inst, &cache);

    for (;;) {
        Move move = {MOVE_NONE, 0, 0, 0, 0};

        if (!find_first_improving_move(inst, &cache, &move)) {
            break;
        }

        apply_move(inst, &move, &cache);
    }
}

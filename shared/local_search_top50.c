#include "local_search_top50.h"

#include "mmr.h"
#include "objective.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOP50_EPS 1e-10
#define TOP50_MIN_ROUND_IMPROVEMENT 0.01
#define TOP50_MAX_PHASE_ITERATIONS 10000
#define TOP50_MAX_CYCLE_MOVES_PER_ROUND 2

typedef struct {
    int weapon[3];
    double score;
    uint64_t generation_order;
} CycleTriple;

typedef struct {
    double survival[MAX_TARGETS];
    double except_product[MAX_WEAPONS];
} SearchState;

static void build_state(const WTAPInstance *inst, SearchState *state) {
    int weapons[MAX_WEAPONS];
    double factors[MAX_WEAPONS];
    double prefix[MAX_WEAPONS + 1];
    double suffix[MAX_WEAPONS + 1];

    for (int t = 0; t < inst->num_targets; t++) {
        state->survival[t] = 1.0;
    }
    for (int w = 0; w < inst->num_weapons; w++) {
        state->except_product[w] = 1.0;
    }

    for (int t = 0; t < inst->num_targets; t++) {
        int count = 0;
        for (int w = 0; w < inst->num_weapons; w++) {
            if (inst->assignment[w] == t) {
                weapons[count] = w;
                factors[count] = 1.0 - inst->prob[w][t];
                count++;
            }
        }

        prefix[0] = 1.0;
        for (int i = 0; i < count; i++) {
            prefix[i + 1] = prefix[i] * factors[i];
        }
        state->survival[t] = prefix[count];

        suffix[count] = 1.0;
        for (int i = count - 1; i >= 0; i--) {
            suffix[i] = suffix[i + 1] * factors[i];
        }
        for (int i = 0; i < count; i++) {
            state->except_product[weapons[i]] = prefix[i] * suffix[i + 1];
        }
    }
}

static int compare_triples(const void *left_ptr, const void *right_ptr) {
    const CycleTriple *left = left_ptr;
    const CycleTriple *right = right_ptr;

    if (left->score > right->score) {
        return -1;
    }
    if (left->score < right->score) {
        return 1;
    }
    if (left->generation_order < right->generation_order) {
        return -1;
    }
    if (left->generation_order > right->generation_order) {
        return 1;
    }
    return 0;
}

static CycleTriple *select_cycle_candidates(const WTAPInstance *inst, uint64_t *kept_count) {
    int n = inst->num_weapons;
    *kept_count = 0;

    if (n < 3) {
        return NULL;
    }

    uint64_t total = (uint64_t)n * (uint64_t)(n - 1) * (uint64_t)(n - 2) / 6U;
    uint64_t keep = (total + 1U) / 2U;
    double weapon_max[MAX_WEAPONS];

    for (int w = 0; w < n; w++) {
        double maximum = inst->prob[w][0];
        for (int t = 1; t < inst->num_targets; t++) {
            if (inst->prob[w][t] > maximum) {
                maximum = inst->prob[w][t];
            }
        }
        weapon_max[w] = maximum;
    }

    if (total > SIZE_MAX / sizeof(CycleTriple)) {
        return NULL;
    }
    CycleTriple *triples = malloc((size_t)total * sizeof(*triples));
    if (triples == NULL) {
        return NULL;
    }

    uint64_t index = 0;
    for (int a = 0; a < n - 2; a++) {
        for (int b = a + 1; b < n - 1; b++) {
            for (int c = b + 1; c < n; c++) {
                triples[index].weapon[0] = a;
                triples[index].weapon[1] = b;
                triples[index].weapon[2] = c;
                triples[index].score =
                    weapon_max[a] + weapon_max[b] + weapon_max[c];
                triples[index].generation_order = index;
                index++;
            }
        }
    }

    qsort(triples, (size_t)total, sizeof(*triples), compare_triples);

    if (keep < total) {
        CycleTriple *smaller = realloc(triples, (size_t)keep * sizeof(*triples));
        if (smaller != NULL) {
            triples = smaller;
        }
    }

    *kept_count = keep;
    printf("  Total possible triples: %llu\n", (unsigned long long)total);
    printf("  Triples selected:       %llu\n", (unsigned long long)keep);
    printf("  Percentage retained:    %.2f%%\n", 100.0 * (double)keep / (double)total);
    printf("  Triple ranking:         descending probability score\n");
    return triples;
}

static void relocation_phase(WTAPInstance *inst, Top50SearchStats *stats) {
    SearchState state;

    for (int iteration = 0; iteration < TOP50_MAX_PHASE_ITERATIONS; iteration++) {
        build_state(inst, &state);

        double best_gain = -INFINITY;
        int best_weapon = -1;
        int best_target = -1;

        for (int w = 0; w < inst->num_weapons; w++) {
            int old_target = inst->assignment[w];
            double old_contribution =
                inst->target_values[old_target] * state.survival[old_target];
            double without_weapon =
                inst->target_values[old_target] * state.except_product[w];

            for (int t = 0; t < inst->num_targets; t++) {
                if (t == old_target) {
                    continue;
                }

                double target_contribution =
                    inst->target_values[t] * state.survival[t];
                double gain =
                    old_contribution + target_contribution - without_weapon
                    - target_contribution * (1.0 - inst->prob[w][t]);

                stats->relocation_evaluations++;
                if (gain > best_gain) {
                    best_gain = gain;
                    best_weapon = w;
                    best_target = t;
                }
            }
        }

        if (!isfinite(best_gain) || best_gain <= TOP50_EPS) {
            break;
        }

        inst->assignment[best_weapon] = best_target;
        stats->relocation_moves++;
    }
}

static void swap_phase(WTAPInstance *inst, Top50SearchStats *stats) {
    SearchState state;
    uint64_t pair_count =
        (uint64_t)inst->num_weapons * (uint64_t)(inst->num_weapons - 1) / 2U;

    if (inst->num_weapons < 2) {
        return;
    }

    for (int iteration = 0; iteration < TOP50_MAX_PHASE_ITERATIONS; iteration++) {
        build_state(inst, &state);

        double best_gain = -INFINITY;
        int best_a = -1;
        int best_b = -1;
        int has_distinct_targets = 0;

        for (int a = 0; a < inst->num_weapons - 1; a++) {
            int target_a = inst->assignment[a];
            for (int b = a + 1; b < inst->num_weapons; b++) {
                int target_b = inst->assignment[b];
                if (target_a == target_b) {
                    continue;
                }

                has_distinct_targets = 1;
                double gain =
                    inst->target_values[target_a] * state.survival[target_a]
                    + inst->target_values[target_b] * state.survival[target_b]
                    - inst->target_values[target_a] * state.except_product[a]
                      * (1.0 - inst->prob[b][target_a])
                    - inst->target_values[target_b] * state.except_product[b]
                      * (1.0 - inst->prob[a][target_b]);

                if (gain > best_gain) {
                    best_gain = gain;
                    best_a = a;
                    best_b = b;
                }
            }
        }

        if (!has_distinct_targets) {
            break;
        }
        stats->swap_evaluations += pair_count;

        if (!isfinite(best_gain) || best_gain <= TOP50_EPS) {
            break;
        }

        int temporary = inst->assignment[best_a];
        inst->assignment[best_a] = inst->assignment[best_b];
        inst->assignment[best_b] = temporary;
        stats->swap_moves++;
    }
}

static int cycle_phase(WTAPInstance *inst, const CycleTriple *candidates,
                       uint64_t candidate_count, Top50SearchStats *stats) {
    SearchState state;
    int accepted = 0;

    for (int move = 0; move < TOP50_MAX_CYCLE_MOVES_PER_ROUND; move++) {
        build_state(inst, &state);

        double best_gain = TOP50_EPS;
        int best_weapons[3] = {-1, -1, -1};
        int best_shift = 0;

        for (uint64_t candidate = 0; candidate < candidate_count; candidate++) {
            const int *weapons = candidates[candidate].weapon;
            int targets[3] = {
                inst->assignment[weapons[0]],
                inst->assignment[weapons[1]],
                inst->assignment[weapons[2]]
            };

            if (targets[0] == targets[1] || targets[0] == targets[2]
                || targets[1] == targets[2]) {
                continue;
            }

            double old_cost = 0.0;
            for (int position = 0; position < 3; position++) {
                int t = targets[position];
                old_cost += inst->target_values[t] * state.survival[t];
            }

            for (int shift = 1; shift <= 2; shift++) {
                double new_cost = 0.0;
                for (int position = 0; position < 3; position++) {
                    int incoming_position = (position - shift + 3) % 3;
                    int incoming_weapon = weapons[incoming_position];
                    int t = targets[position];
                    new_cost +=
                        inst->target_values[t] * state.except_product[weapons[position]]
                        * (1.0 - inst->prob[incoming_weapon][t]);
                }

                double gain = old_cost - new_cost;
                stats->cycle_evaluations++;
                if (gain > best_gain) {
                    best_gain = gain;
                    best_weapons[0] = weapons[0];
                    best_weapons[1] = weapons[1];
                    best_weapons[2] = weapons[2];
                    best_shift = shift;
                }
            }
        }

        if (best_weapons[0] < 0) {
            break;
        }

        int old_targets[3] = {
            inst->assignment[best_weapons[0]],
            inst->assignment[best_weapons[1]],
            inst->assignment[best_weapons[2]]
        };
        for (int position = 0; position < 3; position++) {
            inst->assignment[best_weapons[position]] =
                old_targets[(position + best_shift) % 3];
        }

        stats->cycle_moves++;
        accepted++;
    }

    return accepted;
}

int optimize_with_top50_local_search(WTAPInstance *inst, Top50SearchStats *stats) {
    memset(stats, 0, sizeof(*stats));

    mmr_solve(inst);
    stats->initial_objective = calculate_objective(inst);

    printf("\nInstance %d: %d weapons x %d targets\n",
           inst->instance_id, inst->num_weapons, inst->num_targets);
    printf("  Global greedy MMR objective: %.8f\n", stats->initial_objective);

    uint64_t candidate_count = 0;
    CycleTriple *candidates = select_cycle_candidates(inst, &candidate_count);
    if (inst->num_weapons >= 3 && candidates == NULL) {
        fprintf(stderr, "Error: unable to allocate 3-cycle candidates for instance %d\n",
                inst->instance_id);
        return 0;
    }
    stats->cycle_candidates = candidate_count;

    for (int round = 1; round <= TOP50_MAX_PHASE_ITERATIONS; round++) {
        double round_start = calculate_objective(inst);
        int relocation_before = stats->relocation_moves;
        int swap_before = stats->swap_moves;
        int cycle_before = stats->cycle_moves;

        relocation_phase(inst, stats);
        swap_phase(inst, stats);
        cycle_phase(inst, candidates, candidate_count, stats);

        double round_end = calculate_objective(inst);
        double improvement = round_start - round_end;
        stats->rounds = round;

        printf("  Round %d: objective=%.8f, improvement=%.8f, "
               "relocate=%d, swap=%d, cycle=%d\n",
               round, round_end, improvement,
               stats->relocation_moves - relocation_before,
               stats->swap_moves - swap_before,
               stats->cycle_moves - cycle_before);

        if (improvement < TOP50_MIN_ROUND_IMPROVEMENT) {
            printf("  Stopping: round improvement < %.2f.\n",
                   TOP50_MIN_ROUND_IMPROVEMENT);
            break;
        }
    }

    stats->final_objective = calculate_objective(inst);
    free(candidates);
    return 1;
}

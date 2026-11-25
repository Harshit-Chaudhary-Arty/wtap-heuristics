/*
 * Parameter-control heuristic based on algorithm 4.4 Final Top-50.
 *
 * Standalone solver: search logic lives in this file. Only shared
 * infrastructure headers (parser / mmr / objective / wtap) are included.
 *
 * Controllable knobs:
 *   min_improvement  - round stopping threshold (default 0.01).
 *                      0 means "no threshold": keep going until a whole round
 *                      finds no improving move (best-improvement to convergence).
 *   top_percent      - share of ranked k-cycle combos to keep (default 50)
 *   time_limit       - per-instance wall-clock budget in seconds
 *                      (default and minimum 16; pass a huge value for "no limit")
 *   cycle_size       - weapons in one k-cycle (default 3, range 3..16)
 *
 * One program for every cycle size. The kept top_percent of C(n, k) k-cycles is
 * chosen the same way for every k:
 *   - C(n, k) <= PARAM_MAX_STORED_CYCLES: the ranked list is stored in memory.
 *   - larger (e.g. k = 4 on 200 weapons): nothing is stored. An exact score cutoff
 *     is found by radix selection and C(n, k) is re-enumerated on each cycle scan,
 *     skipping combos below the cutoff. Same kept set, almost no memory.
 *   - above PARAM_MAX_ENUMERATED_CYCLES: refused up front, before any instance runs.
 *
 * Usage:
 *   ./build/parameter_control_heuristic \
 *       [min_improvement] [top_percent] [time_limit_seconds] [cycle_size] \
 *       [--save-csv] [input_file] [results_csv]
 *
 * The results CSV is written only when --save-csv is present.
 *
 * Defaults:
 *   min_improvement     = 0.01
 *   top_percent         = 50
 *   time_limit_seconds  = 16
 *   cycle_size          = 3
 *   input_file          = data/Instances_for_GA.txt
 *   results_csv         = results/parameter-control/parameter-control.csv
 */

#define _POSIX_C_SOURCE 200809L

#include "mmr.h"
#include "objective.h"
#include "output.h"
#include "parser.h"
#include "wtap.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define PARAM_DEFAULT_MIN_IMPROVEMENT 0.01
#define PARAM_DEFAULT_TOP_PERCENT 50.0
#define PARAM_DEFAULT_TIME_LIMIT_SECONDS 16.0
#define PARAM_MIN_TIME_LIMIT_SECONDS 16.0
#define PARAM_DEFAULT_CYCLE_SIZE 3
#define PARAM_MIN_CYCLE_SIZE 3
#define PARAM_MAX_CYCLE_SIZE 16
/* Up to this many k-cycles the full ranked list is kept in memory (80 bytes each). */
#ifndef PARAM_MAX_STORED_CYCLES
#define PARAM_MAX_STORED_CYCLES 8000000ULL
#endif
/* Above that the list is not stored; C(n, k) is re-enumerated per scan instead.
 * Beyond this many combinations even enumeration is too slow, so refuse. */
#ifndef PARAM_MAX_ENUMERATED_CYCLES
#define PARAM_MAX_ENUMERATED_CYCLES 4000000000ULL
#endif
#define PARAM_EPS 1e-10
#define PARAM_MAX_PHASE_ITERATIONS 10000
/* Improving k-cycle moves accepted before a round returns to relocation and swap. */
#define PARAM_MAX_CYCLE_MOVES_PER_ROUND 2

typedef struct {
    double min_improvement;
    double top_percent;
    double time_limit_seconds;
    int cycle_size;
} ParamSearchConfig;

typedef struct {
    double initial_objective;
    double final_objective;
    int rounds;
    int relocation_moves;
    int swap_moves;
    int cycle_moves;
    uint64_t cycle_candidates;
    uint64_t relocation_evaluations;
    uint64_t swap_evaluations;
    uint64_t cycle_evaluations;
    double elapsed_seconds;
    int timed_out;
} ParamSearchStats;

typedef struct {
    ParamSearchStats search;
    double seconds;
} InstanceResult;

typedef struct {
    int weapon[PARAM_MAX_CYCLE_SIZE];
    double score;
    uint64_t generation_order;
} CycleCandidate;

/* The kept top_percent of all C(n, k) k-cycles, in one of two representations. */
typedef struct {
    int n;                      /* weapons */
    int k;                      /* cycle size */
    uint64_t count;             /* number of kept k-cycles */
    int streaming;              /* 0: `stored` holds them; 1: re-enumerate against the cutoff */
    CycleCandidate *stored;     /* ranked list (streaming == 0) */
    int include_all;            /* streaming: every combination is kept (top_percent 100) */
    uint64_t cutoff_key;        /* streaming: ordered key of the lowest kept score */
    uint64_t ties_to_keep;      /* streaming: how many combos equal to the cutoff are kept,
                                   taken in generation order (matches the stable ranking) */
    double weapon_max[MAX_WEAPONS];
} CycleSet;

/* Lexicographic walk over all k-subsets of 0..n-1 with running left-to-right score sums
 * (the same additions, in the same order, as the stored-list scoring). */
typedef struct {
    int n;
    int k;
    const double *weapon_max;
    int combo[PARAM_MAX_CYCLE_SIZE];
    double prefix[PARAM_MAX_CYCLE_SIZE + 1];
} ComboIter;

typedef struct {
    double survival[MAX_TARGETS];
    double except_product[MAX_WEAPONS];
} SearchState;

typedef struct {
    struct timespec start;
    double limit_seconds;
    int timed_out;
} TimeBudget;

static double timespec_seconds(const struct timespec *ts) {
    return (double)ts->tv_sec + (double)ts->tv_nsec / 1e9;
}

static void time_budget_start(TimeBudget *budget, double limit_seconds) {
    clock_gettime(CLOCK_MONOTONIC, &budget->start);
    budget->limit_seconds = limit_seconds;
    budget->timed_out = 0;
}

static double time_budget_elapsed(const TimeBudget *budget) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return timespec_seconds(&now) - timespec_seconds(&budget->start);
}

static int time_budget_expired(TimeBudget *budget) {
    if (budget->timed_out) {
        return 1;
    }
    if (time_budget_elapsed(budget) >= budget->limit_seconds) {
        budget->timed_out = 1;
        return 1;
    }
    return 0;
}

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

static int compare_cycles(const void *left_ptr, const void *right_ptr) {
    const CycleCandidate *left = left_ptr;
    const CycleCandidate *right = right_ptr;

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

static uint64_t keep_count_for_percent(uint64_t total, double top_percent) {
    if (total == 0 || top_percent <= 0.0) {
        return 0;
    }
    if (top_percent >= 100.0) {
        return total;
    }

    double keep_exact = (double)total * (top_percent / 100.0);
    uint64_t keep = (uint64_t)ceil(keep_exact);
    if (keep < 1) {
        keep = 1;
    }
    if (keep > total) {
        keep = total;
    }
    return keep;
}

static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    while (b != 0) {
        uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

/* Returns 0 and writes C(n, k) when that value is at most limit. Returns 1 when it is larger. */
static int binomial_exceeds(int n, int k, uint64_t limit, uint64_t *out) {
    if (k < 0 || n < 0 || k > n) {
        *out = 0;
        return 0;
    }
    if (k > n - k) {
        k = n - k;
    }

    uint64_t value = 1;
    for (int i = 1; i <= k; i++) {
        uint64_t multiplier = (uint64_t)(n - k + i);
        uint64_t divisor = (uint64_t)i;
        uint64_t factor = gcd_u64(value, divisor);
        value /= factor;
        divisor /= factor;
        factor = gcd_u64(multiplier, divisor);
        multiplier /= factor;
        divisor /= factor;
        if (divisor != 1 || multiplier == 0 || value > limit / multiplier) {
            return 1;
        }
        value *= multiplier;
    }

    *out = value;
    return 0;
}

static void combo_iter_refresh(ComboIter *it, int from) {
    for (int i = from; i < it->k; i++) {
        it->prefix[i + 1] = it->prefix[i] + it->weapon_max[it->combo[i]];
    }
}

static void combo_iter_init(ComboIter *it, int n, int k, const double *weapon_max) {
    it->n = n;
    it->k = k;
    it->weapon_max = weapon_max;
    for (int i = 0; i < k; i++) {
        it->combo[i] = i;
    }
    it->prefix[0] = 0.0;
    combo_iter_refresh(it, 0);
}

/* Advances to the next k-subset in generation order. Returns 0 after the last one. */
static int combo_iter_next(ComboIter *it) {
    int cursor = it->k - 1;
    while (cursor >= 0 && it->combo[cursor] == it->n - it->k + cursor) {
        cursor--;
    }
    if (cursor < 0) {
        return 0;
    }
    it->combo[cursor]++;
    for (int j = cursor + 1; j < it->k; j++) {
        it->combo[j] = it->combo[j - 1] + 1;
    }
    combo_iter_refresh(it, cursor);
    return 1;
}

/* Maps a double to a uint64_t that sorts in the same order as the double. */
static uint64_t score_key(double score) {
    uint64_t bits;
    memcpy(&bits, &score, sizeof(bits));
    return (bits >> 63) ? ~bits : (bits | 0x8000000000000000ULL);
}

/*
 * Exact selection of the `keep` highest scores among all C(n, k) combinations without
 * storing them: four 16-bit radix passes over the ordered score key. On return every
 * combination with key > *cutoff_key is kept, plus the first *ties_to_keep (in
 * generation order) of those with key == *cutoff_key. That is the same set as ranking
 * by (score descending, generation order ascending) and cutting at `keep`.
 */
static int select_score_cutoff(int n, int k, const double *weapon_max, uint64_t keep,
                               uint64_t *cutoff_key, uint64_t *ties_to_keep) {
    uint64_t *histogram = malloc(65536U * sizeof(*histogram));
    if (histogram == NULL) {
        return 0;
    }

    uint64_t decided = 0;   /* key bits fixed by the passes so far */
    uint64_t rank = keep;   /* 1-based rank still to find among keys matching `decided` */

    for (int pass = 0; pass < 4; pass++) {
        int shift = 48 - 16 * pass;
        memset(histogram, 0, 65536U * sizeof(*histogram));

        ComboIter it;
        combo_iter_init(&it, n, k, weapon_max);
        do {
            uint64_t key = score_key(it.prefix[k]);
            if (pass == 0 || (key >> (shift + 16)) == (decided >> (shift + 16))) {
                histogram[(key >> shift) & 0xFFFFU]++;
            }
        } while (combo_iter_next(&it));

        uint64_t above = 0;
        int digit = 0;
        for (int d = 65535; d >= 0; d--) {
            if (above + histogram[d] >= rank) {
                digit = d;
                break;
            }
            above += histogram[d];
        }
        rank -= above;
        decided |= (uint64_t)digit << shift;
    }

    free(histogram);
    *cutoff_key = decided;
    *ties_to_keep = rank;
    return 1;
}

static void select_cycle_candidates(const WTAPInstance *inst, double top_percent,
                                    int cycle_size, CycleSet *set, int *failed) {
    int n = inst->num_weapons;
    int k = cycle_size;
    memset(set, 0, sizeof(*set));
    set->n = n;
    set->k = k;
    *failed = 0;

    if (n < k || k < PARAM_MIN_CYCLE_SIZE) {
        return;
    }

    uint64_t total = 0;
    int streaming = binomial_exceeds(n, k, PARAM_MAX_STORED_CYCLES, &total);
    if (streaming && binomial_exceeds(n, k, PARAM_MAX_ENUMERATED_CYCLES, &total)) {
        fprintf(stderr,
                "Error: C(%d, %d) exceeds %llu k-cycle combinations for instance %d. "
                "Use a smaller cycle_size.\n",
                n, k, (unsigned long long)PARAM_MAX_ENUMERATED_CYCLES, inst->instance_id);
        *failed = 1;
        return;
    }

    uint64_t keep = keep_count_for_percent(total, top_percent);
    double *weapon_max = set->weapon_max;

    for (int w = 0; w < n; w++) {
        double maximum = inst->prob[w][0];
        for (int t = 1; t < inst->num_targets; t++) {
            if (inst->prob[w][t] > maximum) {
                maximum = inst->prob[w][t];
            }
        }
        weapon_max[w] = maximum;
    }

    if (streaming) {
        set->streaming = 1;
        set->count = keep;
        if (keep >= total) {
            set->include_all = 1;
        } else if (keep > 0
                   && !select_score_cutoff(n, k, weapon_max, keep,
                                           &set->cutoff_key, &set->ties_to_keep)) {
            fprintf(stderr,
                    "Error: unable to allocate selection buffer for instance %d\n",
                    inst->instance_id);
            *failed = 1;
            return;
        }
    } else {
        if (total > SIZE_MAX / sizeof(CycleCandidate)) {
            fprintf(stderr,
                    "Error: unable to allocate k-cycle candidates for instance %d\n",
                    inst->instance_id);
            *failed = 1;
            return;
        }
        CycleCandidate *candidates = malloc((size_t)total * sizeof(*candidates));
        if (candidates == NULL) {
            fprintf(stderr,
                    "Error: unable to allocate k-cycle candidates for instance %d\n",
                    inst->instance_id);
            *failed = 1;
            return;
        }

        int combo[PARAM_MAX_CYCLE_SIZE];
        for (int i = 0; i < k; i++) {
            combo[i] = i;
        }

        uint64_t index = 0;
        while (1) {
            double score = 0.0;
            for (int i = 0; i < k; i++) {
                candidates[index].weapon[i] = combo[i];
                score += weapon_max[combo[i]];
            }
            candidates[index].score = score;
            candidates[index].generation_order = index;
            index++;

            int cursor = k - 1;
            while (cursor >= 0 && combo[cursor] == n - k + cursor) {
                cursor--;
            }
            if (cursor < 0) {
                break;
            }
            combo[cursor]++;
            for (int j = cursor + 1; j < k; j++) {
                combo[j] = combo[j - 1] + 1;
            }
        }

        qsort(candidates, (size_t)total, sizeof(*candidates), compare_cycles);

        if (keep < total) {
            CycleCandidate *smaller = realloc(candidates, (size_t)keep * sizeof(*candidates));
            if (smaller != NULL) {
                candidates = smaller;
            }
        }
        set->stored = candidates;
        set->count = keep;
    }

    printf("  Cycle size k:           %d\n", k);
    printf("  Total possible k-cycles: %llu\n", (unsigned long long)total);
    printf("  k-cycles selected:      %llu\n", (unsigned long long)keep);
    printf("  Percentage retained:    %.2f%%\n",
           total > 0 ? 100.0 * (double)keep / (double)total : 0.0);
    printf("  k-cycle ranking:        descending probability score\n");
    printf("  k-cycle storage:        %s\n",
           streaming ? "streamed (re-enumerated per scan, score cutoff)" : "stored in memory");
}

static void relocation_phase(WTAPInstance *inst, ParamSearchStats *stats, TimeBudget *budget) {
    SearchState state;

    for (int iteration = 0; iteration < PARAM_MAX_PHASE_ITERATIONS; iteration++) {
        if (time_budget_expired(budget)) {
            break;
        }

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

        if (!isfinite(best_gain) || best_gain <= PARAM_EPS) {
            break;
        }

        inst->assignment[best_weapon] = best_target;
        stats->relocation_moves++;
    }
}

static void swap_phase(WTAPInstance *inst, ParamSearchStats *stats, TimeBudget *budget) {
    SearchState state;
    uint64_t pair_count =
        (uint64_t)inst->num_weapons * (uint64_t)(inst->num_weapons - 1) / 2U;

    if (inst->num_weapons < 2) {
        return;
    }

    for (int iteration = 0; iteration < PARAM_MAX_PHASE_ITERATIONS; iteration++) {
        if (time_budget_expired(budget)) {
            break;
        }

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

        if (!isfinite(best_gain) || best_gain <= PARAM_EPS) {
            break;
        }

        int temporary = inst->assignment[best_a];
        inst->assignment[best_a] = inst->assignment[best_b];
        inst->assignment[best_b] = temporary;
        stats->swap_moves++;
    }
}

static int targets_are_distinct(const int *targets, int cycle_size) {
    for (int i = 0; i < cycle_size; i++) {
        for (int j = i + 1; j < cycle_size; j++) {
            if (targets[i] == targets[j]) {
                return 0;
            }
        }
    }
    return 1;
}

/* Scores one k-cycle (every non-trivial rotation) and records it when it beats *best_gain. */
static void evaluate_cycle(const WTAPInstance *inst, const SearchState *state,
                           const int *weapons, int k, ParamSearchStats *stats,
                           double *best_gain, int *best_weapons, int *best_shift) {
    int targets[PARAM_MAX_CYCLE_SIZE];
    for (int position = 0; position < k; position++) {
        targets[position] = inst->assignment[weapons[position]];
    }
    if (!targets_are_distinct(targets, k)) {
        return;
    }

    double old_cost = 0.0;
    for (int position = 0; position < k; position++) {
        int t = targets[position];
        old_cost += inst->target_values[t] * state->survival[t];
    }

    for (int shift = 1; shift < k; shift++) {
        double new_cost = 0.0;
        for (int position = 0; position < k; position++) {
            int incoming_position = (position - shift + k) % k;
            int incoming_weapon = weapons[incoming_position];
            int t = targets[position];
            new_cost +=
                inst->target_values[t] * state->except_product[weapons[position]]
                * (1.0 - inst->prob[incoming_weapon][t]);
        }

        double gain = old_cost - new_cost;
        stats->cycle_evaluations++;
        if (gain > *best_gain) {
            *best_gain = gain;
            for (int position = 0; position < k; position++) {
                best_weapons[position] = weapons[position];
            }
            *best_shift = shift;
        }
    }
}

static int cycle_phase(WTAPInstance *inst, const CycleSet *set,
                       ParamSearchStats *stats, TimeBudget *budget) {
    SearchState state;
    int accepted = 0;
    int k = set->k;

    if (k < PARAM_MIN_CYCLE_SIZE || set->count == 0) {
        return 0;
    }

    for (int move = 0; move < PARAM_MAX_CYCLE_MOVES_PER_ROUND; move++) {
        if (time_budget_expired(budget)) {
            break;
        }

        build_state(inst, &state);

        double best_gain = PARAM_EPS;
        int best_weapons[PARAM_MAX_CYCLE_SIZE];
        int best_shift = 0;
        for (int i = 0; i < PARAM_MAX_CYCLE_SIZE; i++) {
            best_weapons[i] = -1;
        }

        if (!set->streaming) {
            for (uint64_t candidate = 0; candidate < set->count; candidate++) {
                if ((candidate & 0x3FFU) == 0 && time_budget_expired(budget)) {
                    break;
                }
                evaluate_cycle(inst, &state, set->stored[candidate].weapon, k, stats,
                               &best_gain, best_weapons, &best_shift);
            }
        } else {
            ComboIter it;
            uint64_t ties_left = set->ties_to_keep;
            uint64_t visited = 0;
            combo_iter_init(&it, set->n, k, set->weapon_max);
            do {
                if ((visited++ & 0x3FFFU) == 0 && time_budget_expired(budget)) {
                    break;
                }
                if (!set->include_all) {
                    uint64_t key = score_key(it.prefix[k]);
                    if (key < set->cutoff_key) {
                        continue;
                    }
                    if (key == set->cutoff_key) {
                        if (ties_left == 0) {
                            continue;
                        }
                        ties_left--;
                    }
                }
                evaluate_cycle(inst, &state, it.combo, k, stats,
                               &best_gain, best_weapons, &best_shift);
            } while (combo_iter_next(&it));
        }

        if (best_weapons[0] < 0 || time_budget_expired(budget)) {
            break;
        }

        int old_targets[PARAM_MAX_CYCLE_SIZE];
        for (int position = 0; position < k; position++) {
            old_targets[position] = inst->assignment[best_weapons[position]];
        }
        for (int position = 0; position < k; position++) {
            inst->assignment[best_weapons[position]] =
                old_targets[(position + best_shift) % k];
        }

        stats->cycle_moves++;
        accepted++;
    }

    return accepted;
}

static ParamSearchConfig normalize_config(const ParamSearchConfig *config) {
    ParamSearchConfig normalized = *config;

    if (normalized.min_improvement < 0.0) {
        normalized.min_improvement = 0.0;
    }

    if (normalized.top_percent < 0.0) {
        normalized.top_percent = 0.0;
    } else if (normalized.top_percent > 100.0) {
        normalized.top_percent = 100.0;
    }

    if (normalized.time_limit_seconds < PARAM_MIN_TIME_LIMIT_SECONDS) {
        normalized.time_limit_seconds = PARAM_MIN_TIME_LIMIT_SECONDS;
    }

    if (normalized.cycle_size < PARAM_MIN_CYCLE_SIZE) {
        normalized.cycle_size = PARAM_MIN_CYCLE_SIZE;
    } else if (normalized.cycle_size > PARAM_MAX_CYCLE_SIZE) {
        normalized.cycle_size = PARAM_MAX_CYCLE_SIZE;
    }

    return normalized;
}

static int optimize_with_param_local_search(WTAPInstance *inst,
                                            const ParamSearchConfig *config,
                                            ParamSearchStats *stats) {
    ParamSearchConfig settings = normalize_config(config);
    TimeBudget budget;

    memset(stats, 0, sizeof(*stats));
    time_budget_start(&budget, settings.time_limit_seconds);

    mmr_solve(inst);
    stats->initial_objective = calculate_objective(inst);

    printf("\nInstance %d: %d weapons x %d targets\n",
           inst->instance_id, inst->num_weapons, inst->num_targets);
    printf("  Global greedy MMR objective: %.8f\n", stats->initial_objective);
    printf("  Parameters: min_improvement=%.6f, top_percent=%.2f%%, time_limit=%.3fs, cycle_size=%d\n",
           settings.min_improvement, settings.top_percent, settings.time_limit_seconds,
           settings.cycle_size);

    CycleSet cycles;
    int candidate_failed = 0;
    select_cycle_candidates(inst, settings.top_percent, settings.cycle_size,
                            &cycles, &candidate_failed);
    if (candidate_failed) {
        return 0;
    }
    stats->cycle_candidates = cycles.count;

    for (int round = 1; round <= PARAM_MAX_PHASE_ITERATIONS; round++) {
        if (time_budget_expired(&budget)) {
            printf("  Stopping: time limit %.3fs reached.\n",
                   settings.time_limit_seconds);
            break;
        }

        double round_start = calculate_objective(inst);
        int relocation_before = stats->relocation_moves;
        int swap_before = stats->swap_moves;
        int cycle_before = stats->cycle_moves;

        relocation_phase(inst, stats, &budget);
        swap_phase(inst, stats, &budget);
        cycle_phase(inst, &cycles, stats, &budget);

        double round_end = calculate_objective(inst);
        double improvement = round_start - round_end;
        stats->rounds = round;

        printf("  Round %d: objective=%.8f, improvement=%.8f, "
               "relocate=%d, swap=%d, cycle=%d, elapsed=%.3fs\n",
               round, round_end, improvement,
               stats->relocation_moves - relocation_before,
               stats->swap_moves - swap_before,
               stats->cycle_moves - cycle_before,
               time_budget_elapsed(&budget));

        if (budget.timed_out) {
            printf("  Stopping: time limit %.3fs reached.\n",
                   settings.time_limit_seconds);
            break;
        }

        if (improvement < settings.min_improvement) {
            printf("  Stopping: round improvement < %.6f.\n",
                   settings.min_improvement);
            break;
        }

        /* min_improvement = 0 means "no threshold": run to convergence. A round that made
         * no move gains exactly 0, which `improvement < 0` would never catch, so the loop
         * would otherwise repeat identical empty rounds until the round cap. */
        if (improvement <= 0.0) {
            printf("  Stopping: no improving move found in this round.\n");
            break;
        }
    }

    stats->final_objective = calculate_objective(inst);
    stats->elapsed_seconds = time_budget_elapsed(&budget);
    stats->timed_out = budget.timed_out;
    free(cycles.stored);
    return 1;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [min_improvement] [top_percent] [time_limit_seconds] [cycle_size] "
            "[--save-csv] [input_file] [results_csv]\n"
            "  min_improvement    default %.4f. 0 = no threshold (run until no move improves)\n"
            "  top_percent        default %.1f (range 0..100)\n"
            "  time_limit_seconds default %.1f (minimum %.1f). Use a huge value for no limit.\n"
            "  cycle_size         default %d (range %d..%d). Omit it to keep %d.\n"
            "  --save-csv         write the results CSV (off unless this flag is present)\n",
            prog,
            PARAM_DEFAULT_MIN_IMPROVEMENT,
            PARAM_DEFAULT_TOP_PERCENT,
            PARAM_DEFAULT_TIME_LIMIT_SECONDS,
            PARAM_MIN_TIME_LIMIT_SECONDS,
            PARAM_DEFAULT_CYCLE_SIZE,
            PARAM_MIN_CYCLE_SIZE,
            PARAM_MAX_CYCLE_SIZE,
            PARAM_DEFAULT_CYCLE_SIZE);
}

static int validate_instance(const WTAPInstance *inst) {
    if (inst->num_weapons < 1 || inst->num_weapons > MAX_WEAPONS
        || inst->num_targets < 1 || inst->num_targets > MAX_TARGETS) {
        fprintf(stderr, "Instance %d has unsupported dimensions %d x %d.\n",
                inst->instance_id, inst->num_weapons, inst->num_targets);
        return 0;
    }

    for (int t = 0; t < inst->num_targets; t++) {
        if (!isfinite(inst->target_values[t]) || inst->target_values[t] < 0.0) {
            fprintf(stderr, "Instance %d has an invalid target value.\n",
                    inst->instance_id);
            return 0;
        }
    }

    for (int w = 0; w < inst->num_weapons; w++) {
        for (int t = 0; t < inst->num_targets; t++) {
            double probability = inst->prob[w][t];
            if (!isfinite(probability) || probability < 0.0 || probability > 1.0) {
                fprintf(stderr, "Instance %d has a probability outside [0, 1].\n",
                        inst->instance_id);
                return 0;
            }
        }
    }

    return 1;
}

static void write_assignment(FILE *file, const WTAPInstance *inst) {
    fputc('"', file);
    fputc('[', file);
    for (int w = 0; w < inst->num_weapons; w++) {
        if (w > 0) {
            fputs(", ", file);
        }
        fprintf(file, "%d", inst->assignment[w] + 1);
    }
    fputc(']', file);
    fputc('"', file);
}

static void ensure_parent_dir(const char *path) {
    char buf[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) {
        return;
    }
    memcpy(buf, path, len + 1);
    char *slash = strrchr(buf, '/');
    if (slash == NULL) {
        return;
    }
    *slash = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

static int export_csv(const char *path, const WTAPInstance *instances,
                      const InstanceResult *results, int count,
                      const ParamSearchConfig *config) {
    ensure_parent_dir(path);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: cannot create results file '%s'.\n", path);
        return 0;
    }

    fputs("Instance,Weapons,Targets,Initial Objective,Final Objective,"
          "Objective Reduction,Reduction (%),Runtime (seconds),Search Elapsed (s),"
          "Timed Out,Rounds,Relocation Moves,Swap Moves,K-Cycle Moves,"
          "K-Cycle Candidates,Relocation Evaluations,Swap Evaluations,"
          "K-Cycle Evaluations,Min Improvement,Top Percent,Time Limit (s),Cycle Size,"
          "Assignment (target per weapon)\n", file);

    for (int i = 0; i < count; i++) {
        const ParamSearchStats *stats = &results[i].search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        fprintf(file,
                "%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.9f,%.9f,%d,%d,%d,%d,%d,"
                "%llu,%llu,%llu,%llu,%.17g,%.17g,%.17g,%d,",
                instances[i].instance_id,
                instances[i].num_weapons,
                instances[i].num_targets,
                stats->initial_objective,
                stats->final_objective,
                reduction,
                percentage,
                results[i].seconds,
                stats->elapsed_seconds,
                stats->timed_out,
                stats->rounds,
                stats->relocation_moves,
                stats->swap_moves,
                stats->cycle_moves,
                (unsigned long long)stats->cycle_candidates,
                (unsigned long long)stats->relocation_evaluations,
                (unsigned long long)stats->swap_evaluations,
                (unsigned long long)stats->cycle_evaluations,
                config->min_improvement,
                config->top_percent,
                config->time_limit_seconds,
                config->cycle_size);
        write_assignment(file, &instances[i]);
        fputc('\n', file);
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Error: failed while closing results file '%s'.\n", path);
        return 0;
    }
    return 1;
}

static ParamSearchConfig parse_config(int argc, char *argv[]) {
    ParamSearchConfig config = {
        .min_improvement = PARAM_DEFAULT_MIN_IMPROVEMENT,
        .top_percent = PARAM_DEFAULT_TOP_PERCENT,
        .time_limit_seconds = PARAM_DEFAULT_TIME_LIMIT_SECONDS,
        .cycle_size = PARAM_DEFAULT_CYCLE_SIZE,
    };

    if (argc > 1) {
        config.min_improvement = atof(argv[1]);
        if (config.min_improvement < 0.0) {
            fprintf(stderr,
                    "Warning: min_improvement %.6f is negative; using 0.0.\n",
                    config.min_improvement);
            config.min_improvement = 0.0;
        }
    }

    if (argc > 2) {
        config.top_percent = atof(argv[2]);
        if (config.top_percent < 0.0) {
            fprintf(stderr,
                    "Warning: top_percent %.2f is negative; using 0.0.\n",
                    config.top_percent);
            config.top_percent = 0.0;
        } else if (config.top_percent > 100.0) {
            fprintf(stderr,
                    "Warning: top_percent %.2f exceeds 100; using 100.0.\n",
                    config.top_percent);
            config.top_percent = 100.0;
        }
    }

    if (argc > 3) {
        config.time_limit_seconds = atof(argv[3]);
        if (config.time_limit_seconds < PARAM_MIN_TIME_LIMIT_SECONDS) {
            fprintf(stderr,
                    "Warning: time_limit %.3f is below minimum %.1f; using %.1f.\n",
                    config.time_limit_seconds,
                    PARAM_MIN_TIME_LIMIT_SECONDS,
                    PARAM_MIN_TIME_LIMIT_SECONDS);
            config.time_limit_seconds = PARAM_MIN_TIME_LIMIT_SECONDS;
        }
    }

    if (argc > 4) {
        char *end = NULL;
        long parsed = strtol(argv[4], &end, 10);
        if (end == argv[4] || *end != '\0') {
            fprintf(stderr, "Warning: cycle_size '%s' is not an integer; using %d.\n",
                    argv[4], PARAM_DEFAULT_CYCLE_SIZE);
            config.cycle_size = PARAM_DEFAULT_CYCLE_SIZE;
        } else if (parsed < PARAM_MIN_CYCLE_SIZE) {
            fprintf(stderr, "Warning: cycle_size %ld is below minimum %d; using %d.\n",
                    parsed, PARAM_MIN_CYCLE_SIZE, PARAM_MIN_CYCLE_SIZE);
            config.cycle_size = PARAM_MIN_CYCLE_SIZE;
        } else if (parsed > PARAM_MAX_CYCLE_SIZE) {
            fprintf(stderr, "Warning: cycle_size %ld exceeds maximum %d; using %d.\n",
                    parsed, PARAM_MAX_CYCLE_SIZE, PARAM_MAX_CYCLE_SIZE);
            config.cycle_size = PARAM_MAX_CYCLE_SIZE;
        } else {
            config.cycle_size = (int)parsed;
        }
    }

    return config;
}

int main(int argc, char *argv[]) {
    wtap_take_save_csv_flag(&argc, argv);

    if (argc > 1
        && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    ParamSearchConfig config = parse_config(argc, argv);
    const char *input_file =
        argc > 5 ? argv[5] : "data/Instances_for_GA.txt";
    const char *csv_path =
        argc > 6 ? argv[6] : "results/parameter-control/parameter-control.csv";

    if (argc > 7) {
        print_usage(argv[0]);
        return 1;
    }

    printf("============================================================================\n");
    printf("WTAP: PARAMETER CONTROL HEURISTIC (4.4 BASE)\n");
    printf("============================================================================\n");
    printf("Input file: %s\n", input_file);
    printf("Stopping threshold (min_improvement): %.6f\n", config.min_improvement);
    printf("Cycle candidate percentage: %.2f%%\n", config.top_percent);
    printf("Per-instance time limit: %.3f seconds (minimum %.1f)\n",
           config.time_limit_seconds, PARAM_MIN_TIME_LIMIT_SECONDS);
    printf("Cycle size k: %d (default %d, range %d..%d)\n",
           config.cycle_size, PARAM_DEFAULT_CYCLE_SIZE,
           PARAM_MIN_CYCLE_SIZE, PARAM_MAX_CYCLE_SIZE);
    printf("Loading instances...\n");

    WTAPInstance *instances = calloc(MAX_INSTANCES, sizeof(*instances));
    InstanceResult *results = calloc(MAX_INSTANCES, sizeof(*results));
    if (instances == NULL || results == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        free(instances);
        free(results);
        return 1;
    }

    int count = parse_instances(input_file, instances, MAX_INSTANCES);
    if (count <= 0) {
        fprintf(stderr, "No instances loaded from '%s'.\n", input_file);
        free(instances);
        free(results);
        return 1;
    }
    printf("Loaded instances: %d\n", count);

    /* Check every instance before spending any time, so an unsupported cycle_size is
     * reported immediately instead of after the earlier instances have already run
     * (the CSV is only written once all instances are done). */
    int infeasible = 0;
    for (int i = 0; i < count; i++) {
        uint64_t combinations = 0;
        if (instances[i].num_weapons >= config.cycle_size
            && binomial_exceeds(instances[i].num_weapons, config.cycle_size,
                                PARAM_MAX_ENUMERATED_CYCLES, &combinations)) {
            fprintf(stderr,
                    "Error: instance %d (%d weapons): C(%d, %d) exceeds %llu k-cycle "
                    "combinations. Use a smaller cycle_size.\n",
                    instances[i].instance_id, instances[i].num_weapons,
                    instances[i].num_weapons, config.cycle_size,
                    (unsigned long long)PARAM_MAX_ENUMERATED_CYCLES);
            infeasible = 1;
        }
    }
    if (infeasible) {
        free(instances);
        free(results);
        return 1;
    }

    clock_t overall_start = clock();

    for (int i = 0; i < count; i++) {
        if (!validate_instance(&instances[i])) {
            free(instances);
            free(results);
            return 1;
        }

        clock_t start = clock();
        if (!optimize_with_param_local_search(&instances[i], &config,
                                              &results[i].search)) {
            free(instances);
            free(results);
            return 1;
        }
        results[i].seconds = (double)(clock() - start) / CLOCKS_PER_SEC;

        ParamSearchStats *stats = &results[i].search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        printf("  Final objective:       %.8f\n", stats->final_objective);
        printf("  Objective reduction:   %.8f\n", reduction);
        printf("  Percentage reduction:  %.2f%%\n", percentage);
        printf("  Runtime:               %.3f seconds\n", results[i].seconds);
        printf("  Search elapsed:        %.3f seconds%s\n",
               stats->elapsed_seconds,
               stats->timed_out ? " (timed out)" : "");
        printf("  Accepted moves: relocate=%d, swap=%d, cycle=%d\n",
               stats->relocation_moves, stats->swap_moves, stats->cycle_moves);
        printf("----------------------------------------------------------------------------\n");
    }

    double overall_seconds = (double)(clock() - overall_start) / CLOCKS_PER_SEC;

    printf("\n====================================================================================================\n");
    printf("FINAL SUMMARY - ALL INSTANCES\n");
    printf("====================================================================================================\n");
    printf("%-9s %-8s %-8s %-18s %-18s %-14s %-12s %-9s %-7s %-7s %-8s\n",
           "Instance", "Weapons", "Targets", "Initial Objective", "Final Objective",
           "Reduction (%)", "Runtime (s)", "Relocate", "Swap", "Cycle", "Timeout");

    for (int i = 0; i < count; i++) {
        const ParamSearchStats *stats = &results[i].search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        printf("%-9d %-8d %-8d %-18.6f %-18.6f %-14.6f %-12.6f %-9d %-7d %-7d %-8s\n",
               instances[i].instance_id,
               instances[i].num_weapons,
               instances[i].num_targets,
               stats->initial_objective,
               stats->final_objective,
               percentage,
               results[i].seconds,
               stats->relocation_moves,
               stats->swap_moves,
               stats->cycle_moves,
               stats->timed_out ? "yes" : "no");
    }

    printf("\nTotal instances completed: %d\n", count);
    printf("Total runtime: %.3f seconds\n", overall_seconds);

    if (wtap_save_csv_enabled()) {
        if (!export_csv(csv_path, instances, results, count, &config)) {
            free(instances);
            free(results);
            return 1;
        }
        printf("\nExcel-compatible CSV saved successfully:\n%s\n", csv_path);
    } else {
        printf("\nCSV not saved. Pass --save-csv to write it.\n");
    }
    printf("\nALL DONE.\n");

    free(instances);
    free(results);
    return 0;
}

/*
 * Typed WTA paper-instance solver (Andersen, Pavlikov, Toffolo 2022 format).
 *
 * Same fundamental search as parameter_control_heuristic:
 *   MMR seed → rounds of (relocation + swap + top-% k-cycle)
 * Controllable knobs are identical so you can recreate those runs here:
 *   min_improvement, top_percent, time_limit, cycle_size (k)
 *
 * Fully self-contained: no shared/ helpers. Only the instance format and
 * probability storage differ:
 *   - Reads: num_types  num_targets  weapons_per_type
 *             then num_targets V_j values
 *             then num_types * num_targets lines of "type target p"
 *   - Stores P by type (twins share a row). Weapon w uses type w / weapons_per_type.
 *
 * Cycle candidate ranking:
 *   - When C(num_weapons, k) is within the enumeration budget: exact same
 *     weapon-level ranking as the original heuristic (use this for k = 3).
 *   - When C(num_weapons, k) is too large (e.g. k = 4 on 1000 weapons): rank
 *     C(num_types, k) type-tuples by type max-prob, keep top_percent, and
 *     expand each kept type-tuple to the 2^k concrete weapon choices at
 *     evaluation time. Search moves are unchanged; only the candidate filter
 *     is typed so k = 4 stays runnable.
 *
 * Usage:
 *   ./build/wta_types_heuristic \
 *       [min_improvement] [top_percent] [time_limit_seconds] [cycle_size] \
 *       [--save-csv] [input_path] [results_csv]
 *
 * input_path may be a single .txt file or a directory of paper instances.
 * A directory is scanned for *.txt, sorted smallest → largest
 * (weapons = types * weapons_per_type, then targets, then filename), and every
 * instance is solved in one run into one compiled CSV (like the old GA set).
 * With --save-csv the CSV is rewritten after each finished instance, so stopping
 * mid-run keeps every already-solved row.
 *
 * Defaults:
 *   min_improvement     = 0.01
 *   top_percent         = 50
 *   time_limit_seconds  = 16
 *   cycle_size          = 3
 *   input_path          = data/data_toffolo
 *   results_csv         = results/paper-instances/wta_types_heuristic.csv
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
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
#define PARAM_MAX_BATCH_INSTANCES 64
#define PARAM_DEFAULT_INPUT_PATH "data/data_toffolo"
#define PARAM_DEFAULT_CSV_PATH "results/paper-instances/wta_types_heuristic.csv"
#ifndef PARAM_MAX_STORED_CYCLES
#define PARAM_MAX_STORED_CYCLES 8000000ULL
#endif
#ifndef PARAM_MAX_ENUMERATED_CYCLES
#define PARAM_MAX_ENUMERATED_CYCLES 4000000000ULL
#endif
#define PARAM_EPS 1e-10
#define PARAM_MAX_PHASE_ITERATIONS 10000
#define PARAM_MAX_CYCLE_MOVES_PER_ROUND 2

typedef struct {
    int instance_id;
    int num_types;
    int weapons_per_type;
    int num_weapons;
    int num_targets;
    double *target_values; /* [num_targets] */
    double *prob;          /* [num_types * num_targets], row = type */
    int *assignment;       /* [num_weapons] */
} Instance;

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
    int typed_cycle_ranking; /* 1 when ranking used types instead of weapons */
} ParamSearchStats;

typedef struct {
    ParamSearchStats search;
    double seconds;
} InstanceResult;

/* One solved paper instance kept for the compiled CSV / summary table. */
typedef struct {
    char file_name[256];
    char path[512];
    int instance_id;
    int num_types;
    int weapons_per_type;
    int num_weapons;
    int num_targets;
    InstanceResult result;
    int *assignment;
} BatchRow;

typedef struct {
    char path[512];
    char file_name[256];
    int num_types;
    int num_targets;
    int weapons_per_type;
    int num_weapons;
} JobSpec;

typedef struct {
    int weapon[PARAM_MAX_CYCLE_SIZE];
    double score;
    uint64_t generation_order;
} CycleCandidate;

typedef struct {
    int n; /* universe size: weapons or types */
    int k;
    uint64_t count;
    int streaming;
    int typed; /* 1: n is types; expand to weapons on evaluate */
    int weapons_per_type;
    CycleCandidate *stored;
    int include_all;
    uint64_t cutoff_key;
    uint64_t ties_to_keep;
    double *weapon_max; /* length n */
} CycleSet;

typedef struct {
    int n;
    int k;
    const double *weapon_max;
    int combo[PARAM_MAX_CYCLE_SIZE];
    double prefix[PARAM_MAX_CYCLE_SIZE + 1];
} ComboIter;

typedef struct {
    double *survival;       /* [num_targets] */
    double *except_product; /* [num_weapons] */
} SearchState;

typedef struct {
    struct timespec start;
    double limit_seconds;
    int timed_out;
} TimeBudget;

static int g_save_csv = 0;

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

static int weapon_type(const Instance *inst, int weapon) {
    return weapon / inst->weapons_per_type;
}

static double prob_wt(const Instance *inst, int weapon, int target) {
    return inst->prob[(size_t)weapon_type(inst, weapon) * (size_t)inst->num_targets
                      + (size_t)target];
}

static void free_instance(Instance *inst) {
    free(inst->target_values);
    free(inst->prob);
    free(inst->assignment);
    inst->target_values = NULL;
    inst->prob = NULL;
    inst->assignment = NULL;
}

static int alloc_instance(Instance *inst, int num_types, int num_targets,
                          int weapons_per_type) {
    memset(inst, 0, sizeof(*inst));
    inst->instance_id = 1;
    inst->num_types = num_types;
    inst->num_targets = num_targets;
    inst->weapons_per_type = weapons_per_type;
    inst->num_weapons = num_types * weapons_per_type;

    inst->target_values = calloc((size_t)num_targets, sizeof(double));
    inst->prob = calloc((size_t)num_types * (size_t)num_targets, sizeof(double));
    inst->assignment = calloc((size_t)inst->num_weapons, sizeof(int));
    if (inst->target_values == NULL || inst->prob == NULL || inst->assignment == NULL) {
        free_instance(inst);
        return 0;
    }
    for (int w = 0; w < inst->num_weapons; w++) {
        inst->assignment[w] = -1;
    }
    return 1;
}

/* Paper format: T WPT? Actually header is types targets weapons_per_type. */
static int load_paper_instance(const char *path, Instance *inst) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s'.\n", path);
        return 0;
    }

    int num_types = 0;
    int num_targets = 0;
    int weapons_per_type = 0;
    if (fscanf(fp, "%d %d %d", &num_types, &num_targets, &weapons_per_type) != 3) {
        fprintf(stderr, "Error: expected header 'num_types num_targets weapons_per_type'.\n");
        fclose(fp);
        return 0;
    }
    if (num_types < 1 || num_targets < 1 || weapons_per_type < 1) {
        fprintf(stderr, "Error: invalid header dimensions %d %d %d.\n",
                num_types, num_targets, weapons_per_type);
        fclose(fp);
        return 0;
    }

    if (!alloc_instance(inst, num_types, num_targets, weapons_per_type)) {
        fprintf(stderr, "Error: memory allocation failed for instance.\n");
        fclose(fp);
        return 0;
    }

    for (int t = 0; t < num_targets; t++) {
        if (fscanf(fp, "%lf", &inst->target_values[t]) != 1) {
            fprintf(stderr, "Error: failed reading target value %d.\n", t);
            free_instance(inst);
            fclose(fp);
            return 0;
        }
    }

    size_t expected = (size_t)num_types * (size_t)num_targets;
    for (size_t row = 0; row < expected; row++) {
        int type_id = 0;
        int target_id = 0;
        double p = 0.0;
        if (fscanf(fp, "%d %d %lf", &type_id, &target_id, &p) != 3) {
            fprintf(stderr, "Error: failed reading probability triple %zu.\n", row);
            free_instance(inst);
            fclose(fp);
            return 0;
        }
        if (type_id < 0 || type_id >= num_types || target_id < 0 || target_id >= num_targets) {
            fprintf(stderr, "Error: probability index out of range (%d, %d).\n",
                    type_id, target_id);
            free_instance(inst);
            fclose(fp);
            return 0;
        }
        if (!isfinite(p) || p < 0.0 || p > 1.0) {
            fprintf(stderr, "Error: probability outside [0, 1] at (%d, %d).\n",
                    type_id, target_id);
            free_instance(inst);
            fclose(fp);
            return 0;
        }
        inst->prob[(size_t)type_id * (size_t)num_targets + (size_t)target_id] = p;
    }

    fclose(fp);
    return 1;
}

static double calculate_objective(const Instance *inst) {
    double total = 0.0;
    for (int t = 0; t < inst->num_targets; t++) {
        double survival = 1.0;
        for (int w = 0; w < inst->num_weapons; w++) {
            if (inst->assignment[w] == t) {
                survival *= (1.0 - prob_wt(inst, w, t));
            }
        }
        total += inst->target_values[t] * survival;
    }
    return total;
}

/* Same MMR as shared/mmr.c, using type-shared probabilities. */
static void mmr_solve(Instance *inst) {
    double *survival = calloc((size_t)inst->num_targets, sizeof(double));
    int *assigned = calloc((size_t)inst->num_weapons, sizeof(int));
    if (survival == NULL || assigned == NULL) {
        free(survival);
        free(assigned);
        fprintf(stderr, "Error: MMR allocation failed.\n");
        return;
    }

    for (int t = 0; t < inst->num_targets; t++) {
        survival[t] = 1.0;
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
                double marginal =
                    inst->target_values[t] * survival[t] * prob_wt(inst, w, t);
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
        survival[best_target] *= (1.0 - prob_wt(inst, best_weapon, best_target));
    }

    free(survival);
    free(assigned);
}

static int search_state_alloc(SearchState *state, const Instance *inst) {
    state->survival = calloc((size_t)inst->num_targets, sizeof(double));
    state->except_product = calloc((size_t)inst->num_weapons, sizeof(double));
    return state->survival != NULL && state->except_product != NULL;
}

static void search_state_free(SearchState *state) {
    free(state->survival);
    free(state->except_product);
    state->survival = NULL;
    state->except_product = NULL;
}

static void build_state(const Instance *inst, SearchState *state) {
    int *weapons = malloc((size_t)inst->num_weapons * sizeof(int));
    double *factors = malloc((size_t)inst->num_weapons * sizeof(double));
    double *prefix = malloc((size_t)(inst->num_weapons + 1) * sizeof(double));
    double *suffix = malloc((size_t)(inst->num_weapons + 1) * sizeof(double));
    if (weapons == NULL || factors == NULL || prefix == NULL || suffix == NULL) {
        free(weapons);
        free(factors);
        free(prefix);
        free(suffix);
        return;
    }

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
                factors[count] = 1.0 - prob_wt(inst, w, t);
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

    free(weapons);
    free(factors);
    free(prefix);
    free(suffix);
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

static uint64_t score_key(double score) {
    uint64_t bits;
    memcpy(&bits, &score, sizeof(bits));
    return (bits >> 63) ? ~bits : (bits | 0x8000000000000000ULL);
}

static int select_score_cutoff(int n, int k, const double *weapon_max, uint64_t keep,
                               uint64_t *cutoff_key, uint64_t *ties_to_keep) {
    uint64_t *histogram = malloc(65536U * sizeof(*histogram));
    if (histogram == NULL) {
        return 0;
    }

    uint64_t decided = 0;
    uint64_t rank = keep;

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

static void fill_max_probs(const Instance *inst, int typed, double *out_max, int n) {
    if (typed) {
        for (int type = 0; type < n; type++) {
            double maximum = inst->prob[(size_t)type * (size_t)inst->num_targets];
            for (int t = 1; t < inst->num_targets; t++) {
                double p = inst->prob[(size_t)type * (size_t)inst->num_targets + (size_t)t];
                if (p > maximum) {
                    maximum = p;
                }
            }
            out_max[type] = maximum;
        }
    } else {
        for (int w = 0; w < n; w++) {
            double maximum = prob_wt(inst, w, 0);
            for (int t = 1; t < inst->num_targets; t++) {
                double p = prob_wt(inst, w, t);
                if (p > maximum) {
                    maximum = p;
                }
            }
            out_max[w] = maximum;
        }
    }
}

static void select_cycle_candidates(const Instance *inst, double top_percent,
                                    int cycle_size, CycleSet *set, int *failed) {
    int k = cycle_size;
    memset(set, 0, sizeof(*set));
    set->k = k;
    set->weapons_per_type = inst->weapons_per_type;
    *failed = 0;

    if (inst->num_weapons < k || k < PARAM_MIN_CYCLE_SIZE) {
        return;
    }

    /* Prefer exact weapon-level ranking; fall back to types when C(W,k) is too big. */
    int typed = 0;
    int n = inst->num_weapons;
    uint64_t total = 0;
    if (binomial_exceeds(inst->num_weapons, k, PARAM_MAX_ENUMERATED_CYCLES, &total)) {
        typed = 1;
        n = inst->num_types;
        total = 0;
        if (binomial_exceeds(n, k, PARAM_MAX_ENUMERATED_CYCLES, &total)) {
            fprintf(stderr,
                    "Error: C(%d, %d) exceeds %llu combinations even over weapon types. "
                    "Use a smaller cycle_size.\n",
                    n, k, (unsigned long long)PARAM_MAX_ENUMERATED_CYCLES);
            *failed = 1;
            return;
        }
        printf("  Note: C(%d, %d) weapon cycles exceed the enumeration budget; "
               "ranking C(%d, %d) type cycles instead (expanded at evaluate).\n",
               inst->num_weapons, k, n, k);
    }

    set->n = n;
    set->typed = typed;

    uint64_t keep = keep_count_for_percent(total, top_percent);
    set->weapon_max = malloc((size_t)n * sizeof(double));
    if (set->weapon_max == NULL) {
        fprintf(stderr, "Error: unable to allocate weapon_max.\n");
        *failed = 1;
        return;
    }
    fill_max_probs(inst, typed, set->weapon_max, n);

    /* total is already exact; stream when it does not fit in memory. */
    int streaming = (total > PARAM_MAX_STORED_CYCLES);

    if (streaming) {
        set->streaming = 1;
        set->count = keep;
        if (keep >= total) {
            set->include_all = 1;
        } else if (keep > 0
                   && !select_score_cutoff(n, k, set->weapon_max, keep,
                                           &set->cutoff_key, &set->ties_to_keep)) {
            fprintf(stderr, "Error: unable to allocate selection buffer.\n");
            *failed = 1;
            return;
        }
    } else {
        if (total > SIZE_MAX / sizeof(CycleCandidate)) {
            fprintf(stderr, "Error: unable to allocate k-cycle candidates.\n");
            *failed = 1;
            return;
        }
        CycleCandidate *candidates = malloc((size_t)total * sizeof(*candidates));
        if (candidates == NULL) {
            fprintf(stderr, "Error: unable to allocate k-cycle candidates.\n");
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
                score += set->weapon_max[combo[i]];
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
    printf("  Ranking universe:       %s (%d)\n", typed ? "types" : "weapons", n);
    printf("  Total possible k-cycles: %llu\n", (unsigned long long)total);
    printf("  k-cycles selected:      %llu\n", (unsigned long long)keep);
    printf("  Percentage retained:    %.2f%%\n",
           total > 0 ? 100.0 * (double)keep / (double)total : 0.0);
    printf("  k-cycle ranking:        descending probability score\n");
    printf("  k-cycle storage:        %s\n",
           streaming ? "streamed (re-enumerated per scan, score cutoff)" : "stored in memory");
}

static void free_cycle_set(CycleSet *set) {
    free(set->stored);
    free(set->weapon_max);
    set->stored = NULL;
    set->weapon_max = NULL;
}

static void relocation_phase(Instance *inst, ParamSearchStats *stats, TimeBudget *budget,
                             SearchState *state) {
    for (int iteration = 0; iteration < PARAM_MAX_PHASE_ITERATIONS; iteration++) {
        if (time_budget_expired(budget)) {
            break;
        }

        build_state(inst, state);

        double best_gain = -INFINITY;
        int best_weapon = -1;
        int best_target = -1;

        for (int w = 0; w < inst->num_weapons; w++) {
            int old_target = inst->assignment[w];
            double old_contribution =
                inst->target_values[old_target] * state->survival[old_target];
            double without_weapon =
                inst->target_values[old_target] * state->except_product[w];

            for (int t = 0; t < inst->num_targets; t++) {
                if (t == old_target) {
                    continue;
                }

                double target_contribution =
                    inst->target_values[t] * state->survival[t];
                double gain =
                    old_contribution + target_contribution - without_weapon
                    - target_contribution * (1.0 - prob_wt(inst, w, t));

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

static void swap_phase(Instance *inst, ParamSearchStats *stats, TimeBudget *budget,
                       SearchState *state) {
    uint64_t pair_count =
        (uint64_t)inst->num_weapons * (uint64_t)(inst->num_weapons - 1) / 2U;

    if (inst->num_weapons < 2) {
        return;
    }

    for (int iteration = 0; iteration < PARAM_MAX_PHASE_ITERATIONS; iteration++) {
        if (time_budget_expired(budget)) {
            break;
        }

        build_state(inst, state);

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
                    inst->target_values[target_a] * state->survival[target_a]
                    + inst->target_values[target_b] * state->survival[target_b]
                    - inst->target_values[target_a] * state->except_product[a]
                      * (1.0 - prob_wt(inst, b, target_a))
                    - inst->target_values[target_b] * state->except_product[b]
                      * (1.0 - prob_wt(inst, a, target_b));

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

static void evaluate_cycle(const Instance *inst, const SearchState *state,
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
                * (1.0 - prob_wt(inst, incoming_weapon, t));
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

/*
 * When ranking was over types, expand one type-tuple into every choice of one
 * concrete weapon per type (weapons_per_type ^ k), then score each as usual.
 */
static void evaluate_typed_or_weapon(const Instance *inst, const SearchState *state,
                                     const CycleSet *set, const int *ids, int k,
                                     ParamSearchStats *stats, double *best_gain,
                                     int *best_weapons, int *best_shift) {
    if (!set->typed) {
        evaluate_cycle(inst, state, ids, k, stats, best_gain, best_weapons, best_shift);
        return;
    }

    int wpt = set->weapons_per_type;
    uint64_t expansions = 1;
    for (int i = 0; i < k; i++) {
        expansions *= (uint64_t)wpt;
    }

    int weapons[PARAM_MAX_CYCLE_SIZE];
    for (uint64_t mask = 0; mask < expansions; mask++) {
        uint64_t rest = mask;
        int ok = 1;
        for (int i = 0; i < k; i++) {
            int copy = (int)(rest % (uint64_t)wpt);
            rest /= (uint64_t)wpt;
            weapons[i] = ids[i] * wpt + copy;
        }
        /* Distinct weapons are automatic when types are distinct. */
        for (int i = 0; i < k && ok; i++) {
            for (int j = i + 1; j < k; j++) {
                if (weapons[i] == weapons[j]) {
                    ok = 0;
                    break;
                }
            }
        }
        if (!ok) {
            continue;
        }
        evaluate_cycle(inst, state, weapons, k, stats, best_gain, best_weapons, best_shift);
    }
}

static int cycle_phase(Instance *inst, const CycleSet *set, ParamSearchStats *stats,
                       TimeBudget *budget, SearchState *state) {
    int accepted = 0;
    int k = set->k;

    if (k < PARAM_MIN_CYCLE_SIZE || set->count == 0) {
        return 0;
    }

    for (int move = 0; move < PARAM_MAX_CYCLE_MOVES_PER_ROUND; move++) {
        if (time_budget_expired(budget)) {
            break;
        }

        build_state(inst, state);

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
                evaluate_typed_or_weapon(inst, state, set, set->stored[candidate].weapon, k,
                                        stats, &best_gain, best_weapons, &best_shift);
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
                evaluate_typed_or_weapon(inst, state, set, it.combo, k, stats,
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

static int optimize(Instance *inst, const ParamSearchConfig *config, ParamSearchStats *stats) {
    ParamSearchConfig settings = normalize_config(config);
    TimeBudget budget;
    SearchState state;

    memset(stats, 0, sizeof(*stats));
    if (!search_state_alloc(&state, inst)) {
        fprintf(stderr, "Error: search state allocation failed.\n");
        return 0;
    }

    time_budget_start(&budget, settings.time_limit_seconds);

    printf("\nInstance %d: %d types x %d copies = %d weapons, %d targets\n",
           inst->instance_id, inst->num_types, inst->weapons_per_type,
           inst->num_weapons, inst->num_targets);
    printf("  Building MMR seed...\n");
    fflush(stdout);
    mmr_solve(inst);
    stats->initial_objective = calculate_objective(inst);

    printf("  Global greedy MMR objective: %.8f\n", stats->initial_objective);
    printf("  Parameters: min_improvement=%.6f, top_percent=%.2f%%, time_limit=%.3fs, cycle_size=%d\n",
           settings.min_improvement, settings.top_percent, settings.time_limit_seconds,
           settings.cycle_size);

    CycleSet cycles;
    int candidate_failed = 0;
    select_cycle_candidates(inst, settings.top_percent, settings.cycle_size,
                            &cycles, &candidate_failed);
    if (candidate_failed) {
        search_state_free(&state);
        free_cycle_set(&cycles);
        return 0;
    }
    stats->cycle_candidates = cycles.count;
    stats->typed_cycle_ranking = cycles.typed;

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

        relocation_phase(inst, stats, &budget, &state);
        swap_phase(inst, stats, &budget, &state);
        cycle_phase(inst, &cycles, stats, &budget, &state);

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
        fflush(stdout);

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
        if (improvement <= 0.0) {
            printf("  Stopping: no improving move found in this round.\n");
            break;
        }
    }

    stats->final_objective = calculate_objective(inst);
    stats->elapsed_seconds = time_budget_elapsed(&budget);
    stats->timed_out = budget.timed_out;
    free_cycle_set(&cycles);
    search_state_free(&state);
    return 1;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [min_improvement] [top_percent] [time_limit_seconds] [cycle_size] "
            "[--save-csv] [input_path] [results_csv]\n"
            "  min_improvement    default %.4f. 0 = no threshold (run until no move improves)\n"
            "  top_percent        default %.1f (range 0..100)\n"
            "  time_limit_seconds default %.1f (minimum %.1f). Use a huge value for no limit.\n"
            "  cycle_size         default %d (range %d..%d). Controllable: use 3 or 4.\n"
            "  --save-csv         write the results CSV (off unless this flag is present)\n"
            "  input_path         file or directory (default %s). Directory → all *.txt,\n"
            "                     sorted smallest→largest into one compiled CSV.\n"
            "\n"
            "Same search as parameter_control_heuristic (MMR + relocate + swap + top-%% k-cycle).\n",
            prog,
            PARAM_DEFAULT_MIN_IMPROVEMENT,
            PARAM_DEFAULT_TOP_PERCENT,
            PARAM_DEFAULT_TIME_LIMIT_SECONDS,
            PARAM_MIN_TIME_LIMIT_SECONDS,
            PARAM_DEFAULT_CYCLE_SIZE,
            PARAM_MIN_CYCLE_SIZE,
            PARAM_MAX_CYCLE_SIZE,
            PARAM_DEFAULT_INPUT_PATH);
}

static int take_save_csv_flag(int *argc, char **argv) {
    int out = 1;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--save-csv") == 0) {
            g_save_csv = 1;
            continue;
        }
        argv[out++] = argv[i];
    }
    argv[out] = NULL;
    *argc = out;
    return g_save_csv;
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

static void write_assignment_row(FILE *file, const int *assignment, int num_weapons) {
    fputc('"', file);
    fputc('[', file);
    for (int w = 0; w < num_weapons; w++) {
        if (w > 0) {
            fputs(", ", file);
        }
        fprintf(file, "%d", assignment[w] + 1);
    }
    fputc(']', file);
    fputc('"', file);
}

static int path_is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int ends_with_txt(const char *name) {
    size_t len = strlen(name);
    return len >= 4 && strcmp(name + len - 4, ".txt") == 0;
}

static int peek_paper_header(const char *path, int *num_types, int *num_targets,
                             int *weapons_per_type) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    int ok = fscanf(fp, "%d %d %d", num_types, num_targets, weapons_per_type) == 3;
    fclose(fp);
    return ok && *num_types > 0 && *num_targets > 0 && *weapons_per_type > 0;
}

static int compare_jobs(const void *left_ptr, const void *right_ptr) {
    const JobSpec *left = left_ptr;
    const JobSpec *right = right_ptr;

    if (left->num_weapons != right->num_weapons) {
        return left->num_weapons < right->num_weapons ? -1 : 1;
    }
    if (left->num_targets != right->num_targets) {
        return left->num_targets < right->num_targets ? -1 : 1;
    }
    if (left->weapons_per_type != right->weapons_per_type) {
        return left->weapons_per_type < right->weapons_per_type ? -1 : 1;
    }
    return strcmp(left->file_name, right->file_name);
}

static int add_job(JobSpec *jobs, int *count, int capacity, const char *path,
                   const char *file_name) {
    if (*count >= capacity) {
        fprintf(stderr, "Error: more than %d instances; raise PARAM_MAX_BATCH_INSTANCES.\n",
                capacity);
        return 0;
    }

    JobSpec *job = &jobs[*count];
    memset(job, 0, sizeof(*job));
    snprintf(job->path, sizeof(job->path), "%s", path);
    snprintf(job->file_name, sizeof(job->file_name), "%s", file_name);
    if (!peek_paper_header(path, &job->num_types, &job->num_targets,
                           &job->weapons_per_type)) {
        fprintf(stderr, "Error: cannot read paper header from '%s'.\n", path);
        return 0;
    }
    job->num_weapons = job->num_types * job->weapons_per_type;
    (*count)++;
    return 1;
}

/* Collect one file, or every *.txt in a directory. Sorted smallest → largest. */
static int collect_jobs(const char *input_path, JobSpec *jobs, int capacity, int *out_count) {
    *out_count = 0;

    if (path_is_directory(input_path)) {
        DIR *dir = opendir(input_path);
        if (dir == NULL) {
            fprintf(stderr, "Error: cannot open directory '%s'.\n", input_path);
            return 0;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.' || !ends_with_txt(entry->d_name)) {
                continue;
            }
            char path[512];
            int n = snprintf(path, sizeof(path), "%s/%s", input_path, entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(path)) {
                fprintf(stderr, "Error: path too long for '%s/%s'.\n",
                        input_path, entry->d_name);
                closedir(dir);
                return 0;
            }
            if (!add_job(jobs, out_count, capacity, path, entry->d_name)) {
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);

        if (*out_count == 0) {
            fprintf(stderr, "Error: no .txt instances found in '%s'.\n", input_path);
            return 0;
        }
    } else {
        const char *slash = strrchr(input_path, '/');
        const char *name = slash != NULL ? slash + 1 : input_path;
        if (!add_job(jobs, out_count, capacity, input_path, name)) {
            return 0;
        }
    }

    qsort(jobs, (size_t)*out_count, sizeof(*jobs), compare_jobs);
    return 1;
}

static void free_batch_rows(BatchRow *rows, int count) {
    for (int i = 0; i < count; i++) {
        free(rows[i].assignment);
        rows[i].assignment = NULL;
    }
}

static int export_csv(const char *path, const BatchRow *rows, int count,
                      const ParamSearchConfig *config) {
    ensure_parent_dir(path);

    /* Write to a temp file then rename so an interrupt mid-write cannot leave a
     * half-truncated CSV; the previous progressive snapshot stays intact. */
    char tmp_path[1100];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >= (int)sizeof(tmp_path)) {
        fprintf(stderr, "Error: results path too long for temp file.\n");
        return 0;
    }

    FILE *file = fopen(tmp_path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: cannot create results file '%s'.\n", tmp_path);
        return 0;
    }

    fputs("Instance,File,Types,WeaponsPerType,Weapons,Targets,Initial Objective,Final Objective,"
          "Objective Reduction,Reduction (%),Runtime (seconds),Search Elapsed (s),"
          "Timed Out,Rounds,Relocation Moves,Swap Moves,K-Cycle Moves,"
          "K-Cycle Candidates,Typed Cycle Ranking,Relocation Evaluations,Swap Evaluations,"
          "K-Cycle Evaluations,Min Improvement,Top Percent,Time Limit (s),Cycle Size,"
          "Assignment (target per weapon)\n", file);

    for (int i = 0; i < count; i++) {
        const BatchRow *row = &rows[i];
        const ParamSearchStats *stats = &row->result.search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        fprintf(file,
                "%d,\"%s\",%d,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.9f,%.9f,%d,%d,%d,%d,%d,"
                "%llu,%d,%llu,%llu,%llu,%.17g,%.17g,%.17g,%d,",
                row->instance_id,
                row->file_name,
                row->num_types,
                row->weapons_per_type,
                row->num_weapons,
                row->num_targets,
                stats->initial_objective,
                stats->final_objective,
                reduction,
                percentage,
                row->result.seconds,
                stats->elapsed_seconds,
                stats->timed_out,
                stats->rounds,
                stats->relocation_moves,
                stats->swap_moves,
                stats->cycle_moves,
                (unsigned long long)stats->cycle_candidates,
                stats->typed_cycle_ranking,
                (unsigned long long)stats->relocation_evaluations,
                (unsigned long long)stats->swap_evaluations,
                (unsigned long long)stats->cycle_evaluations,
                config->min_improvement,
                config->top_percent,
                config->time_limit_seconds,
                config->cycle_size);
        write_assignment_row(file, row->assignment, row->num_weapons);
        fputc('\n', file);
    }

    if (fflush(file) != 0 || fclose(file) != 0) {
        fprintf(stderr, "Error: failed while closing results file '%s'.\n", tmp_path);
        remove(tmp_path);
        return 0;
    }
    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "Error: failed to replace '%s' with progressive CSV.\n", path);
        remove(tmp_path);
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
            fprintf(stderr, "Warning: min_improvement negative; using 0.0.\n");
            config.min_improvement = 0.0;
        }
    }
    if (argc > 2) {
        config.top_percent = atof(argv[2]);
        if (config.top_percent < 0.0) {
            config.top_percent = 0.0;
        } else if (config.top_percent > 100.0) {
            config.top_percent = 100.0;
        }
    }
    if (argc > 3) {
        config.time_limit_seconds = atof(argv[3]);
        if (config.time_limit_seconds < PARAM_MIN_TIME_LIMIT_SECONDS) {
            fprintf(stderr,
                    "Warning: time_limit %.3f below minimum %.1f; using %.1f.\n",
                    config.time_limit_seconds, PARAM_MIN_TIME_LIMIT_SECONDS,
                    PARAM_MIN_TIME_LIMIT_SECONDS);
            config.time_limit_seconds = PARAM_MIN_TIME_LIMIT_SECONDS;
        }
    }
    if (argc > 4) {
        char *end = NULL;
        long parsed = strtol(argv[4], &end, 10);
        if (end == argv[4] || *end != '\0') {
            fprintf(stderr, "Warning: cycle_size '%s' not an integer; using %d.\n",
                    argv[4], PARAM_DEFAULT_CYCLE_SIZE);
            config.cycle_size = PARAM_DEFAULT_CYCLE_SIZE;
        } else if (parsed < PARAM_MIN_CYCLE_SIZE) {
            config.cycle_size = PARAM_MIN_CYCLE_SIZE;
        } else if (parsed > PARAM_MAX_CYCLE_SIZE) {
            config.cycle_size = PARAM_MAX_CYCLE_SIZE;
        } else {
            config.cycle_size = (int)parsed;
        }
    }
    return config;
}

int main(int argc, char *argv[]) {
    take_save_csv_flag(&argc, argv);

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    ParamSearchConfig config = parse_config(argc, argv);
    const char *input_path =
        argc > 5 ? argv[5] : PARAM_DEFAULT_INPUT_PATH;
    const char *csv_path =
        argc > 6 ? argv[6] : PARAM_DEFAULT_CSV_PATH;

    if (argc > 7) {
        print_usage(argv[0]);
        return 1;
    }

    JobSpec jobs[PARAM_MAX_BATCH_INSTANCES];
    int job_count = 0;
    if (!collect_jobs(input_path, jobs, PARAM_MAX_BATCH_INSTANCES, &job_count)) {
        return 1;
    }

    printf("============================================================================\n");
    printf("WTAP: TYPED PAPER-INSTANCE HEURISTIC (MMR + k-CYCLE, SAME CONTROLS)\n");
    printf("============================================================================\n");
    printf("Input path: %s\n", input_path);
    printf("Instances found: %d (solving smallest → largest)\n", job_count);
    for (int i = 0; i < job_count; i++) {
        printf("  %2d. %s  (%d types x %d/type = %d weapons, %d targets)\n",
               i + 1, jobs[i].file_name, jobs[i].num_types, jobs[i].weapons_per_type,
               jobs[i].num_weapons, jobs[i].num_targets);
    }
    printf("Stopping threshold (min_improvement): %.6f\n", config.min_improvement);
    printf("Cycle candidate percentage: %.2f%%\n", config.top_percent);
    printf("Per-instance time limit: %.3f seconds (minimum %.1f)\n",
           config.time_limit_seconds, PARAM_MIN_TIME_LIMIT_SECONDS);
    printf("Cycle size k: %d (default %d, range %d..%d)\n",
           config.cycle_size, PARAM_DEFAULT_CYCLE_SIZE,
           PARAM_MIN_CYCLE_SIZE, PARAM_MAX_CYCLE_SIZE);
    fflush(stdout);

    /* Refuse up front if any instance cannot support this cycle_size. */
    int infeasible = 0;
    for (int i = 0; i < job_count; i++) {
        uint64_t combinations = 0;
        int weapons_ok = !binomial_exceeds(jobs[i].num_weapons, config.cycle_size,
                                           PARAM_MAX_ENUMERATED_CYCLES, &combinations);
        int types_ok = !binomial_exceeds(jobs[i].num_types, config.cycle_size,
                                         PARAM_MAX_ENUMERATED_CYCLES, &combinations);
        if (!weapons_ok && !types_ok) {
            fprintf(stderr,
                    "Error: %s — C(%d, %d) weapons and C(%d, %d) types both exceed %llu. "
                    "Use a smaller cycle_size.\n",
                    jobs[i].file_name,
                    jobs[i].num_weapons, config.cycle_size,
                    jobs[i].num_types, config.cycle_size,
                    (unsigned long long)PARAM_MAX_ENUMERATED_CYCLES);
            infeasible = 1;
        }
    }
    if (infeasible) {
        return 1;
    }

    BatchRow *rows = calloc((size_t)job_count, sizeof(*rows));
    if (rows == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        return 1;
    }

    clock_t overall_start = clock();

    for (int i = 0; i < job_count; i++) {
        printf("\n----------------------------------------------------------------------------\n");
        printf("Loading %s ...\n", jobs[i].file_name);
        fflush(stdout);

        Instance inst;
        if (!load_paper_instance(jobs[i].path, &inst)) {
            free_batch_rows(rows, i);
            free(rows);
            return 1;
        }
        inst.instance_id = i + 1;

        BatchRow *row = &rows[i];
        memcpy(row->file_name, jobs[i].file_name, sizeof(row->file_name));
        memcpy(row->path, jobs[i].path, sizeof(row->path));
        row->instance_id = inst.instance_id;
        row->num_types = inst.num_types;
        row->weapons_per_type = inst.weapons_per_type;
        row->num_weapons = inst.num_weapons;
        row->num_targets = inst.num_targets;

        clock_t start = clock();
        if (!optimize(&inst, &config, &row->result.search)) {
            free_instance(&inst);
            free_batch_rows(rows, i + 1);
            free(rows);
            return 1;
        }
        row->result.seconds = (double)(clock() - start) / CLOCKS_PER_SEC;

        row->assignment = malloc((size_t)inst.num_weapons * sizeof(int));
        if (row->assignment == NULL) {
            fprintf(stderr, "Error: unable to store assignment for '%s'.\n", jobs[i].file_name);
            free_instance(&inst);
            free_batch_rows(rows, i + 1);
            free(rows);
            return 1;
        }
        memcpy(row->assignment, inst.assignment, (size_t)inst.num_weapons * sizeof(int));

        ParamSearchStats *stats = &row->result.search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        printf("  Final objective:       %.8f\n", stats->final_objective);
        printf("  Objective reduction:   %.8f (%.2f%%)\n", reduction, percentage);
        printf("  Runtime:               %.3f seconds%s\n",
               row->result.seconds,
               stats->timed_out ? " (timed out)" : "");
        printf("  Moves: relocate=%d, swap=%d, cycle=%d\n",
               stats->relocation_moves, stats->swap_moves, stats->cycle_moves);
        fflush(stdout);

        free_instance(&inst);

        /* Rewrite the CSV after every finished instance so Ctrl+C / kill mid-batch
         * still leaves all completed rows on disk. */
        if (g_save_csv) {
            if (!export_csv(csv_path, rows, i + 1, &config)) {
                free_batch_rows(rows, i + 1);
                free(rows);
                return 1;
            }
            printf("  Progress CSV updated (%d/%d): %s\n", i + 1, job_count, csv_path);
            fflush(stdout);
        }
    }

    double overall_seconds = (double)(clock() - overall_start) / CLOCKS_PER_SEC;

    printf("\n====================================================================================================\n");
    printf("FINAL SUMMARY - ALL INSTANCES\n");
    printf("====================================================================================================\n");
    printf("%-4s %-22s %-8s %-8s %-8s %-18s %-18s %-14s %-12s %-8s\n",
           "ID", "File", "Weapons", "Targets", "W/Type", "Initial Objective",
           "Final Objective", "Reduction (%)", "Runtime (s)", "Timeout");

    for (int i = 0; i < job_count; i++) {
        const BatchRow *row = &rows[i];
        const ParamSearchStats *stats = &row->result.search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        printf("%-4d %-22s %-8d %-8d %-8d %-18.6f %-18.6f %-14.6f %-12.6f %-8s\n",
               row->instance_id,
               row->file_name,
               row->num_weapons,
               row->num_targets,
               row->weapons_per_type,
               stats->initial_objective,
               stats->final_objective,
               percentage,
               row->result.seconds,
               stats->timed_out ? "yes" : "no");
    }

    printf("\nTotal instances completed: %d\n", job_count);
    printf("Total runtime: %.3f seconds\n", overall_seconds);

    if (g_save_csv) {
        printf("\nExcel-compatible CSV saved successfully:\n%s\n", csv_path);
    } else {
        printf("\nCSV not saved. Pass --save-csv to write it.\n");
    }
    printf("\nALL DONE.\n");

    free_batch_rows(rows, job_count);
    free(rows);
    return 0;
}

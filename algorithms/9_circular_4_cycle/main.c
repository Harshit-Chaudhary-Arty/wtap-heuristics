/*
 * 4-cycle version of the parameter-control heuristic.
 *
 * Same search as parameter_control_heuristic, with the cycle size fixed at 4.
 * Every C(n, 4) combination is ranked. There is no stored-candidate cap and
 * no per-instance time limit. A round still stops when the objective improves
 * by less than min_improvement.
 *
 * Usage:
 *   ./build/9_circular_4_cycle \
 *       [min_improvement] [top_percent] \
 *       [--save-csv] [input_file] [results_csv]
 *
 * The results CSV is written only when --save-csv is present.
 *
 * Defaults:
 *   min_improvement = 0.01
 *   top_percent     = 50
 *   cycle_size      = 4
 *   input_file      = data/Instances_for_GA.txt
 *   results_csv     = results/algorithms/circular-4-cycle.csv
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
#define PARAM_CYCLE_SIZE 4
#define PARAM_EPS 1e-10
#define PARAM_MAX_PHASE_ITERATIONS 10000
/* Improving 4-cycle moves accepted before a round returns to relocation and swap. */
#define PARAM_MAX_CYCLE_MOVES_PER_ROUND 2

typedef struct {
    double min_improvement;
    double top_percent;
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
    int weapon[PARAM_CYCLE_SIZE];
    double score;
    uint64_t generation_order;
} CycleCandidate;

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

/* Returns 0 and writes C(n, k). Returns 1 when the value does not fit in uint64. */
static int binomial(int n, int k, uint64_t *out) {
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
        if (divisor != 1 || multiplier == 0 || value > UINT64_MAX / multiplier) {
            return 1;
        }
        value *= multiplier;
    }

    *out = value;
    return 0;
}

static CycleCandidate *select_cycle_candidates(const WTAPInstance *inst, double top_percent,
                                               int cycle_size, uint64_t *kept_count,
                                               int *failed) {
    int n = inst->num_weapons;
    int k = cycle_size;
    *kept_count = 0;
    *failed = 0;

    if (n < k) {
        return NULL;
    }

    uint64_t total = 0;
    if (binomial(n, k, &total)) {
        fprintf(stderr,
                "Error: C(%d, %d) does not fit in a 64-bit count for instance %d.\n",
                n, k, inst->instance_id);
        *failed = 1;
        return NULL;
    }

    uint64_t keep = keep_count_for_percent(total, top_percent);
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

    if (total > SIZE_MAX / sizeof(CycleCandidate)) {
        fprintf(stderr,
                "Error: unable to allocate k-cycle candidates for instance %d\n",
                inst->instance_id);
        *failed = 1;
        return NULL;
    }
    CycleCandidate *candidates = malloc((size_t)total * sizeof(*candidates));
    if (candidates == NULL) {
        fprintf(stderr,
                "Error: unable to allocate k-cycle candidates for instance %d\n",
                inst->instance_id);
        *failed = 1;
        return NULL;
    }

    int combo[PARAM_CYCLE_SIZE];
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

    *kept_count = keep;
    printf("  Cycle size k:           %d\n", k);
    printf("  Total possible k-cycles: %llu\n", (unsigned long long)total);
    printf("  k-cycles selected:      %llu\n", (unsigned long long)keep);
    printf("  Percentage retained:    %.2f%%\n",
           total > 0 ? 100.0 * (double)keep / (double)total : 0.0);
    printf("  k-cycle ranking:        descending probability score\n");
    return candidates;
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

static int cycle_phase(WTAPInstance *inst, const CycleCandidate *candidates,
                       uint64_t candidate_count, int cycle_size,
                       ParamSearchStats *stats, TimeBudget *budget) {
    SearchState state;
    int accepted = 0;
    int k = cycle_size;

    if (k != PARAM_CYCLE_SIZE || candidate_count == 0) {
        return 0;
    }

    for (int move = 0; move < PARAM_MAX_CYCLE_MOVES_PER_ROUND; move++) {
        if (time_budget_expired(budget)) {
            break;
        }

        build_state(inst, &state);

        double best_gain = PARAM_EPS;
        int best_weapons[PARAM_CYCLE_SIZE];
        int best_shift = 0;
        for (int i = 0; i < PARAM_CYCLE_SIZE; i++) {
            best_weapons[i] = -1;
        }

        for (uint64_t candidate = 0; candidate < candidate_count; candidate++) {
            if ((candidate & 0x3FFU) == 0 && time_budget_expired(budget)) {
                break;
            }

            const int *weapons = candidates[candidate].weapon;
            int targets[PARAM_CYCLE_SIZE];
            for (int position = 0; position < k; position++) {
                targets[position] = inst->assignment[weapons[position]];
            }
            if (!targets_are_distinct(targets, k)) {
                continue;
            }

            double old_cost = 0.0;
            for (int position = 0; position < k; position++) {
                int t = targets[position];
                old_cost += inst->target_values[t] * state.survival[t];
            }

            for (int shift = 1; shift < k; shift++) {
                double new_cost = 0.0;
                for (int position = 0; position < k; position++) {
                    int incoming_position = (position - shift + k) % k;
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
                    for (int position = 0; position < k; position++) {
                        best_weapons[position] = weapons[position];
                    }
                    best_shift = shift;
                }
            }
        }

        if (best_weapons[0] < 0 || time_budget_expired(budget)) {
            break;
        }

        int old_targets[PARAM_CYCLE_SIZE];
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
    } else     if (normalized.top_percent > 100.0) {
        normalized.top_percent = 100.0;
    }

    return normalized;
}

static int optimize_with_param_local_search(WTAPInstance *inst,
                                            const ParamSearchConfig *config,
                                            ParamSearchStats *stats) {
    ParamSearchConfig settings = normalize_config(config);
    TimeBudget budget;

    memset(stats, 0, sizeof(*stats));
    time_budget_start(&budget, INFINITY);

    mmr_solve(inst);
    stats->initial_objective = calculate_objective(inst);

    printf("\nInstance %d: %d weapons x %d targets\n",
           inst->instance_id, inst->num_weapons, inst->num_targets);
    printf("  Global greedy MMR objective: %.8f\n", stats->initial_objective);
    printf("  Parameters: min_improvement=%.6f, top_percent=%.2f%%, cycle_size=%d, no time limit\n",
           settings.min_improvement, settings.top_percent, PARAM_CYCLE_SIZE);

    uint64_t candidate_count = 0;
    int candidate_failed = 0;
    CycleCandidate *candidates =
        select_cycle_candidates(inst, settings.top_percent, PARAM_CYCLE_SIZE,
                                &candidate_count, &candidate_failed);
    if (candidate_failed) {
        return 0;
    }
    stats->cycle_candidates = candidate_count;

    for (int round = 1; round <= PARAM_MAX_PHASE_ITERATIONS; round++) {
        double round_start = calculate_objective(inst);
        int relocation_before = stats->relocation_moves;
        int swap_before = stats->swap_moves;
        int cycle_before = stats->cycle_moves;

        relocation_phase(inst, stats, &budget);
        swap_phase(inst, stats, &budget);
        cycle_phase(inst, candidates, candidate_count, PARAM_CYCLE_SIZE, stats, &budget);

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

        if (improvement < settings.min_improvement) {
            printf("  Stopping: round improvement < %.6f.\n",
                   settings.min_improvement);
            break;
        }
    }

    stats->final_objective = calculate_objective(inst);
    stats->elapsed_seconds = time_budget_elapsed(&budget);
    stats->timed_out = budget.timed_out;
    free(candidates);
    return 1;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [min_improvement] [top_percent] "
            "[--save-csv] [input_file] [results_csv]\n"
            "  min_improvement    default %.4f\n"
            "  top_percent        default %.1f (range 0..100)\n"
            "  cycle_size         fixed at %d\n"
            "  time limit         none\n"
            "  --save-csv         write the results CSV (off unless this flag is present)\n",
            prog,
            PARAM_DEFAULT_MIN_IMPROVEMENT,
            PARAM_DEFAULT_TOP_PERCENT,
            PARAM_CYCLE_SIZE);
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
          "K-Cycle Evaluations,Min Improvement,Top Percent,Cycle Size,"
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
                "%llu,%llu,%llu,%llu,%.17g,%.17g,%d,",
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
                PARAM_CYCLE_SIZE);
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
        argc > 3 ? argv[3] : "data/Instances_for_GA.txt";
    const char *csv_path =
        argc > 4 ? argv[4] : "results/algorithms/circular-4-cycle.csv";

    if (argc > 5) {
        print_usage(argv[0]);
        return 1;
    }

    printf("============================================================================\n");
    printf("WTAP: CIRCULAR 4-CYCLE (NO TIME LIMIT)\n");
    printf("============================================================================\n");
    printf("Input file: %s\n", input_file);
    printf("Stopping threshold (min_improvement): %.6f\n", config.min_improvement);
    printf("Cycle candidate percentage: %.2f%%\n", config.top_percent);
    printf("Time limit: none\n");
    printf("Cycle size k: %d\n", PARAM_CYCLE_SIZE);
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

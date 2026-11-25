/*
 * WTAP: global-greedy MMR + relocation + swap + top-50% 3-cycles.
 *
 * Usage:
 *   ./build/4_4_Final_top50 [input_file] [results_csv] [--save-csv]
 *
 * The results CSV is written only when --save-csv is present.
 *
 * Defaults:
 *   input_file  = data/Instances_for_GA.txt
 *   results_csv = results/algorithms/mmr-top50-3-cycles.csv
 */

#include "local_search_top50.h"
#include "output.h"
#include "parser.h"
#include "wtap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    Top50SearchStats search;
    double seconds;
} InstanceResult;

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
                      const InstanceResult *results, int count) {
    ensure_parent_dir(path);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: cannot create results file '%s'.\n", path);
        return 0;
    }

    fputs("Instance,Weapons,Targets,Initial Objective,Final Objective,"
          "Objective Reduction,Reduction (%),Runtime (seconds),Rounds,"
          "Relocation Moves,Swap Moves,3-Cycle Moves,3-Cycle Candidates,"
          "Relocation Evaluations,Swap Evaluations,3-Cycle Evaluations,"
          "Assignment (target per weapon)\n", file);

    for (int i = 0; i < count; i++) {
        const Top50SearchStats *stats = &results[i].search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        fprintf(file,
                "%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.9f,%d,%d,%d,%d,"
                "%llu,%llu,%llu,%llu,",
                instances[i].instance_id,
                instances[i].num_weapons,
                instances[i].num_targets,
                stats->initial_objective,
                stats->final_objective,
                reduction,
                percentage,
                results[i].seconds,
                stats->rounds,
                stats->relocation_moves,
                stats->swap_moves,
                stats->cycle_moves,
                (unsigned long long)stats->cycle_candidates,
                (unsigned long long)stats->relocation_evaluations,
                (unsigned long long)stats->swap_evaluations,
                (unsigned long long)stats->cycle_evaluations);
        write_assignment(file, &instances[i]);
        fputc('\n', file);
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Error: failed while closing results file '%s'.\n", path);
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    wtap_take_save_csv_flag(&argc, argv);

    const char *input_file =
        argc > 1 ? argv[1] : "data/Instances_for_GA.txt";
    const char *csv_path =
        argc > 2 ? argv[2] : "results/algorithms/mmr-top50-3-cycles.csv";

    printf("============================================================================\n");
    printf("WTAP: GLOBAL GREEDY MMR + RELOCATION + SWAP + TOP 50%% 3-CYCLES\n");
    printf("============================================================================\n");
    printf("Input file: %s\n", input_file);
    printf("Cycle candidate percentage: 50%%\n");
    printf("Stopping threshold: 0.01\n");
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
        if (!optimize_with_top50_local_search(&instances[i], &results[i].search)) {
            free(instances);
            free(results);
            return 1;
        }
        results[i].seconds = (double)(clock() - start) / CLOCKS_PER_SEC;

        Top50SearchStats *stats = &results[i].search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        printf("  Final objective:       %.8f\n", stats->final_objective);
        printf("  Objective reduction:   %.8f\n", reduction);
        printf("  Percentage reduction:  %.2f%%\n", percentage);
        printf("  Runtime:               %.3f seconds\n", results[i].seconds);
        printf("  Accepted moves: relocate=%d, swap=%d, cycle=%d\n",
               stats->relocation_moves, stats->swap_moves, stats->cycle_moves);
        printf("----------------------------------------------------------------------------\n");
    }

    double overall_seconds = (double)(clock() - overall_start) / CLOCKS_PER_SEC;

    printf("\n====================================================================================================\n");
    printf("FINAL SUMMARY - ALL INSTANCES\n");
    printf("====================================================================================================\n");
    printf("%-9s %-8s %-8s %-18s %-18s %-14s %-12s %-9s %-7s %-7s\n",
           "Instance", "Weapons", "Targets", "Initial Objective", "Final Objective",
           "Reduction (%)", "Runtime (s)", "Relocate", "Swap", "Cycle");

    for (int i = 0; i < count; i++) {
        const Top50SearchStats *stats = &results[i].search;
        double reduction = stats->initial_objective - stats->final_objective;
        double percentage =
            stats->initial_objective != 0.0
            ? 100.0 * reduction / stats->initial_objective
            : 0.0;

        printf("%-9d %-8d %-8d %-18.6f %-18.6f %-14.6f %-12.6f %-9d %-7d %-7d\n",
               instances[i].instance_id,
               instances[i].num_weapons,
               instances[i].num_targets,
               stats->initial_objective,
               stats->final_objective,
               percentage,
               results[i].seconds,
               stats->relocation_moves,
               stats->swap_moves,
               stats->cycle_moves);
    }

    printf("\nTotal instances completed: %d\n", count);
    printf("Total runtime: %.3f seconds\n", overall_seconds);

    if (wtap_save_csv_enabled()) {
        if (!export_csv(csv_path, instances, results, count)) {
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

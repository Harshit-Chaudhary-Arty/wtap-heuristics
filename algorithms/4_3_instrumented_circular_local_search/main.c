/*
 * MMR + instrumented circular local search.
 *
 * Same search as 4.1, but prints:
 *   - improvement-cycle counters per phase (relocate / swap / k-cycle)
 *   - per-move log with improvement cycle number
 *   - final weapon-target assignment
 *
 * Usage:
 *   ./build/4_3_instrumented_circular_local_search [cycle_size] [min_improvement] [--save-csv]
 *
 * Uses data/Instances_for_GA.txt.
 *   cycle_size defaults to 3 (valid range 3..16)
 *   min_improvement defaults to 0.01 (use 0 for strict convergence)
 */

#include "local_search_instrumented.h"
#include "mmr.h"
#include "objective.h"
#include "output.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int cycle_size = DEFAULT_CYCLE_SIZE;
static double min_improvement = LS_DEFAULT_MIN_IMPROVEMENT;

int main(int argc, char *argv[]) {
    wtap_take_save_csv_flag(&argc, argv);

    if (argc > 1) {
        int parsed = atoi(argv[1]);
        if (parsed < DEFAULT_CYCLE_SIZE) {
            fprintf(stderr,
                    "Warning: cycle_size %d is below minimum %d; using %d.\n",
                    parsed, DEFAULT_CYCLE_SIZE, DEFAULT_CYCLE_SIZE);
            cycle_size = DEFAULT_CYCLE_SIZE;
        } else if (parsed > MAX_CYCLE_SIZE) {
            fprintf(stderr,
                    "Warning: cycle_size %d exceeds maximum %d; using %d.\n",
                    parsed, MAX_CYCLE_SIZE, MAX_CYCLE_SIZE);
            cycle_size = MAX_CYCLE_SIZE;
        } else {
            cycle_size = parsed;
        }
    }

    if (argc > 2) {
        min_improvement = atof(argv[2]);
        if (min_improvement < 0.0) {
            fprintf(stderr,
                    "Warning: min_improvement %.6f is negative; using 0.0.\n",
                    min_improvement);
            min_improvement = 0.0;
        }
    }

    char algorithm_name[128];
    snprintf(algorithm_name, sizeof(algorithm_name),
             "4.3 - MMR + Instrumented Circular LS (k=%d, min_improvement=%.4f)",
             cycle_size, min_improvement);

    WTAPInstance *instances = calloc(MAX_INSTANCES, sizeof(WTAPInstance));
    if (instances == NULL) {
        fprintf(stderr, "Error: memory allocation failed\n");
        return 1;
    }

    int num_instances = parse_instances("data/Instances_for_GA.txt", instances, MAX_INSTANCES);
    if (num_instances <= 0) {
        fprintf(stderr, "No instances loaded from 'data/Instances_for_GA.txt'\n");
        free(instances);
        return 1;
    }

    printf("Weapon-Target Assignment Problem (WTAP)\n");
    printf("Algorithm: %s\n", algorithm_name);
    printf("========================================\n");
    printf("Loaded %d instance(s) from 'data/Instances_for_GA.txt'\n\n", num_instances);

    print_results_table_header();

    double total_solve_seconds = 0.0;
    double objectives[MAX_INSTANCES];
    double seconds[MAX_INSTANCES];

    for (int i = 0; i < num_instances; i++) {
        LocalSearchStats stats = {0};
        ls_stats_init(&stats);

        clock_t start = clock();

        mmr_solve(&instances[i]);
        improve_with_instrumented_circular_local_search(&instances[i], cycle_size,
                                                        min_improvement, &stats);

        double solve_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
        total_solve_seconds += solve_seconds;

        double objective = calculate_objective(&instances[i]);
        objectives[i] = objective;
        seconds[i] = solve_seconds;
        print_results_table_row(&instances[i], objective, solve_seconds);

        printf("\n--- Instance %d details ---", instances[i].instance_id);
        print_local_search_stats(&stats);
        print_assignment(&instances[i]);
        printf("\n");

        ls_stats_free(&stats);
    }

    print_results_table_footer(total_solve_seconds);
    printf("\n");

    if (wtap_save_csv_enabled()) {
        const char *csv_path = getenv("WTAP_RESULTS_CSV");
        if (csv_path == NULL || csv_path[0] == '\0') {
            csv_path = "results/algorithms/mmr-instrumented-circular-local-search.csv";
        }
        if (!export_results_csv(csv_path, instances, objectives, seconds, num_instances)) {
            free(instances);
            return 1;
        }
    } else {
        printf("CSV not saved. Pass --save-csv to write it.\n");
    }

    free(instances);
    return 0;
}

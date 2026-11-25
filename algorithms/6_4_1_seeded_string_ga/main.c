/*
 * String-coded GA seeded by algorithm 4.4 Final Top-50.
 *
 * For each instance:
 *   1. Run the complete 4.4 Final Top-50 optimizer.
 *   2. Insert that exact assignment as chromosome 0.
 *   3. Fill the rest of the 2*num_weapons population randomly.
 *   4. Run the seeded GA for 50*num_weapons generations.
 *
 * Usage:
 *   ./build/6_4_1_seeded_string_ga [random_seed] [input_file] [history_csv] [--save-csv]
 */

#include "local_search_top50.h"
#include "objective.h"
#include "output.h"
#include "parser.h"
#include "string_ga.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void print_chromosome(const WTAPInstance *inst) {
    printf("  Best chromosome: [");
    for (int w = 0; w < inst->num_weapons; w++) {
        printf("%s%d", w == 0 ? "" : ", ", inst->assignment[w]);
    }
    printf("]\n");
}

int main(int argc, char *argv[]) {
    wtap_take_save_csv_flag(&argc, argv);

    uint64_t base_seed = 1;
    const char *input_file = "data/Instances_for_GA.txt";
    const char *history_path = NULL;

    if (argc > 1) {
        base_seed = (uint64_t)strtoull(argv[1], NULL, 10);
    }
    if (argc > 2) {
        input_file = argv[2];
    }
    if (argc > 3) {
        history_path = argv[3];
    }
    if (argc > 4) {
        fprintf(stderr,
                "Usage: %s [random_seed] [input_file] [history_csv]\n",
                argv[0]);
        return 1;
    }

    FILE *history_file = NULL;
    if (history_path != NULL) {
        history_file = fopen(history_path, "w");
        if (history_file == NULL) {
            fprintf(stderr, "Error: cannot create history file '%s'\n", history_path);
            return 1;
        }
        fprintf(history_file, "instance,generation,best_objective\n");
    }

    WTAPInstance *instances = calloc(MAX_INSTANCES, sizeof(*instances));
    if (instances == NULL) {
        fprintf(stderr, "Error: memory allocation failed\n");
        if (history_file != NULL) {
            fclose(history_file);
        }
        return 1;
    }

    int num_instances = parse_instances(input_file, instances, MAX_INSTANCES);
    if (num_instances <= 0) {
        fprintf(stderr, "No instances loaded from '%s'\n", input_file);
        free(instances);
        if (history_file != NULL) {
            fclose(history_file);
        }
        return 1;
    }

    printf("Weapon-Target Assignment Problem (WTAP)\n");
    printf("Algorithm: 6 - 4.4 Final Top-50-seeded String-Coded GA\n");
    printf("================================================\n");
    printf("Loaded %d instance(s) from '%s'; random seed=%llu\n\n",
           num_instances, input_file, (unsigned long long)base_seed);
    print_results_table_header();

    double total_seconds = 0.0;
    double final_objectives[MAX_INSTANCES];
    double solve_seconds[MAX_INSTANCES];
    for (int i = 0; i < num_instances; i++) {
        WTAPInstance *inst = &instances[i];
        int seed_assignment[MAX_WEAPONS];

        clock_t start = clock();

        /* Use the complete 4.4 result as chromosome 0 without modifying it. */
        Top50SearchStats top50_stats;
        if (!optimize_with_top50_local_search(inst, &top50_stats)) {
            fprintf(stderr, "Error: 4.4 seed generation failed for instance %d\n",
                    inst->instance_id);
            free(instances);
            if (history_file != NULL) {
                fclose(history_file);
            }
            return 1;
        }
        for (int w = 0; w < inst->num_weapons; w++) {
            seed_assignment[w] = inst->assignment[w];
        }

        StringGAConfig config = {
            .population_size = 0,          /* default: 2 * num_weapons */
            .generations = 0,              /* default: 50 * num_weapons */
            .crossover_probability = 0.30,
            .mutation_rate = 0.0,          /* default: 1 / num_weapons */
            .random_seed = base_seed ^ (uint64_t)inst->instance_id
        };
        StringGAResult result;

        if (!string_ga_solve(inst, seed_assignment, &config, &result)) {
            fprintf(stderr, "Error: GA failed for instance %d\n", inst->instance_id);
            free(instances);
            if (history_file != NULL) {
                fclose(history_file);
            }
            return 1;
        }

        double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
        double final_objective = calculate_objective(inst);
        total_seconds += seconds;
        final_objectives[i] = final_objective;
        solve_seconds[i] = seconds;
        print_results_table_row(inst, final_objective, seconds);

        printf("  4.4 Top-50 seed objective: %.6f | GA best: %.6f | "
               "population: %d | generations: %d\n",
               result.seed_objective, result.best_objective,
               result.population_size, result.generations);
        print_chromosome(inst);

        if (history_file != NULL) {
            for (int generation = 0; generation <= result.generations; generation++) {
                fprintf(history_file, "%d,%d,%.12f\n", inst->instance_id,
                        generation, result.best_history[generation]);
            }
        }

        string_ga_result_free(&result);
    }

    print_results_table_footer(total_seconds);
    printf("\nFinal concise summary\n");
    printf("=====================\n");
    print_results_table_header();
    for (int i = 0; i < num_instances; i++) {
        print_results_table_row(&instances[i], final_objectives[i], solve_seconds[i]);
    }
    print_results_table_footer(total_seconds);

    if (wtap_save_csv_enabled()) {
        const char *csv_path = getenv("WTAP_RESULTS_CSV");
        if (csv_path == NULL || csv_path[0] == '\0') {
            csv_path = "results/algorithms/seeded-string-ga.csv";
        }
        if (!export_results_csv(csv_path, instances, final_objectives, solve_seconds,
                                num_instances)) {
            free(instances);
            if (history_file != NULL) {
                fclose(history_file);
            }
            return 1;
        }
    } else {
        printf("CSV not saved. Pass --save-csv to write it.\n");
    }

    if (history_file != NULL) {
        fclose(history_file);
        printf("\nConvergence history written to '%s'.\n", history_path);
    }
    printf("\n");

    free(instances);
    return 0;
}

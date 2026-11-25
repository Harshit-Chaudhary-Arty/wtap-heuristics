#include "runner.h"

#include "objective.h"
#include "output.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int run_solver(const char *algorithm_name, const char *csv_path,
               WTAPSolver solve, int argc, char *argv[]) {
    wtap_take_save_csv_flag(&argc, argv);

    const char *input_file = "data/Instances_for_GA.txt";

    if (argc > 1) {
        input_file = argv[1];
    }

    WTAPInstance *instances = calloc(MAX_INSTANCES, sizeof(WTAPInstance));
    if (instances == NULL) {
        fprintf(stderr, "Error: memory allocation failed\n");
        return 1;
    }

    int num_instances = parse_instances(input_file, instances, MAX_INSTANCES);
    if (num_instances <= 0) {
        fprintf(stderr, "No instances loaded from '%s'\n", input_file);
        free(instances);
        return 1;
    }

    printf("Weapon-Target Assignment Problem (WTAP)\n");
    printf("Algorithm: %s\n", algorithm_name);
    printf("========================================\n");
    printf("Loaded %d instance(s) from '%s'\n\n", num_instances, input_file);

    print_results_table_header();

    double total_solve_seconds = 0.0;
    double objectives[MAX_INSTANCES];
    double seconds[MAX_INSTANCES];

    for (int i = 0; i < num_instances; i++) {
        clock_t start = clock();
        solve(&instances[i]);
        double solve_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;

        total_solve_seconds += solve_seconds;
        double objective = calculate_objective(&instances[i]);
        objectives[i] = objective;
        seconds[i] = solve_seconds;
        print_results_table_row(&instances[i], objective, solve_seconds);
    }

    print_results_table_footer(total_solve_seconds);
    printf("\n");

    if (wtap_save_csv_enabled()) {
        const char *out_path = getenv("WTAP_RESULTS_CSV");
        if (out_path == NULL || out_path[0] == '\0') {
            out_path = csv_path;
        }
        if (out_path != NULL && !export_results_csv(out_path, instances, objectives,
                                                    seconds, num_instances)) {
            free(instances);
            return 1;
        }
    } else {
        printf("CSV not saved. Pass --save-csv to write it.\n");
    }

    free(instances);
    return 0;
}

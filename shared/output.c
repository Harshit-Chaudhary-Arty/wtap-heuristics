#define _POSIX_C_SOURCE 200809L

#include "output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int save_csv_requested = 0;

int wtap_take_save_csv_flag(int *argc, char **argv) {
    if (argc == NULL || argv == NULL) {
        return save_csv_requested;
    }

    int write = 1;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--save-csv") == 0) {
            save_csv_requested = 1;
            continue;
        }
        argv[write++] = argv[i];
    }
    argv[write] = NULL;
    *argc = write;
    return save_csv_requested;
}

int wtap_save_csv_enabled(void) {
    return save_csv_requested;
}

void print_results_table_header(void) {
    printf("%-10s %-10s %-10s %-14s %-12s\n",
           "Instance", "Weapons", "Targets", "Objective", "Time (s)");
    printf("%-10s %-10s %-10s %-14s %-12s\n",
           "--------", "-------", "-------", "---------", "--------");
}

void print_results_table_row(const WTAPInstance *inst, double objective, double solve_seconds) {
    printf("%-10d %-10d %-10d %-14.4f %-12.6f\n",
           inst->instance_id,
           inst->num_weapons,
           inst->num_targets,
           objective,
           solve_seconds);
}

void print_results_table_footer(double total_solve_seconds) {
    printf("%-10s %-10s %-10s %-14s %-12.6f\n",
           "TOTAL", "", "", "", total_solve_seconds);
}

static const char *move_kind_label(LSMoveKind kind) {
    switch (kind) {
    case LS_MOVE_RELOCATE:
        return "Relocate";
    case LS_MOVE_SWAP:
        return "Swap";
    case LS_MOVE_K_CYCLE:
        return "K-Cycle";
    default:
        return "Unknown";
    }
}

void print_local_search_stats(const LocalSearchStats *stats) {
    if (stats == NULL) {
        return;
    }

    printf("\n  Local search summary:\n");
    printf("    Improvement cycles: %d\n", stats->num_improvement_cycles);
    printf("    Total moves: relocate=%d, swap=%d, k-cycle=%d\n",
           stats->total_relocate_moves,
           stats->total_swap_moves,
           stats->total_k_cycle_moves);

    for (int i = 0; i < stats->num_improvement_cycles; i++) {
        const LSImprovementCycleStats *cycle = &stats->cycles[i];
        int total = cycle->relocate_moves + cycle->swap_moves + cycle->k_cycle_moves;

        printf("    Cycle %d: relocate=%d, swap=%d, k-cycle=%d (moves=%d)\n",
               cycle->improvement_cycle,
               cycle->relocate_moves,
               cycle->swap_moves,
               cycle->k_cycle_moves,
               total);
    }

    if (stats->move_log != NULL && stats->num_move_records > 0) {
        printf("\n  Move log:\n");
        for (int i = 0; i < stats->num_move_records; i++) {
            const LSMoveRecord *move = &stats->move_log[i];

            if (move->kind == LS_MOVE_RELOCATE) {
                printf("    [Cycle %d] %s | W%d -> T%d | improvement %.6f\n",
                       move->improvement_cycle,
                       move_kind_label(move->kind),
                       move->weapon,
                       move->target,
                       move->improvement);
            } else if (move->kind == LS_MOVE_SWAP) {
                printf("    [Cycle %d] %s | W%d <-> W%d | improvement %.6f\n",
                       move->improvement_cycle,
                       move_kind_label(move->kind),
                       move->weapon_a,
                       move->weapon_b,
                       move->improvement);
            } else {
                printf("    [Cycle %d] %s | shift=%d | weapons",
                       move->improvement_cycle,
                       move_kind_label(move->kind),
                       move->shift);
                for (int w = 0; w < move->cycle_size; w++) {
                    printf(" W%d", move->cycle_weapons[w]);
                }
                printf(" | improvement %.6f\n", move->improvement);
            }
        }

        if (stats->num_move_records >= LS_MAX_MOVE_LOG) {
            printf("    ... move log truncated at %d records\n", LS_MAX_MOVE_LOG);
        }
    }
}

void print_assignment(const WTAPInstance *inst) {
    if (inst == NULL) {
        return;
    }

    printf("\n  Final assignment (weapon -> target):\n");
    for (int w = 0; w < inst->num_weapons; w++) {
        printf("    W%-4d -> T%d\n", w, inst->assignment[w]);
    }
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

static void write_assignment_field(FILE *file, const WTAPInstance *inst) {
    fputs("\"[", file);
    for (int w = 0; w < inst->num_weapons; w++) {
        if (w > 0) {
            fputs(", ", file);
        }
        fprintf(file, "%d", inst->assignment[w] + 1);
    }
    fputs("]\"", file);
}

int export_results_csv(const char *path, const WTAPInstance *instances,
                       const double *objectives, const double *seconds,
                       int count) {
    ensure_parent_dir(path);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: cannot create results file '%s'.\n", path);
        return 0;
    }

    fputs("Instance,Weapons,Targets,Objective,Time_s,"
          "Assignment (target per weapon)\n", file);
    for (int i = 0; i < count; i++) {
        fprintf(file, "%d,%d,%d,%.6f,%.9f,",
                instances[i].instance_id, instances[i].num_weapons,
                instances[i].num_targets, objectives[i], seconds[i]);
        write_assignment_field(file, &instances[i]);
        fputc('\n', file);
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Error: failed while closing results file '%s'.\n", path);
        return 0;
    }
    printf("Results CSV (with final assignments) saved to '%s'.\n", path);
    return 1;
}

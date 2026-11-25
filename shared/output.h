#ifndef OUTPUT_H
#define OUTPUT_H

#include "local_search_instrumented.h"
#include "wtap.h"

void print_results_table_header(void);
void print_results_table_row(const WTAPInstance *inst, double objective, double solve_seconds);
void print_results_table_footer(double total_solve_seconds);

void print_local_search_stats(const LocalSearchStats *stats);
void print_assignment(const WTAPInstance *inst);

/*
 * Write one CSV row per instance:
 *   Instance,Weapons,Targets,Objective,Time_s,Assignment (target per weapon)
 * Assignment is a quoted 1-based list, e.g. "[8, 2, 6, ...]".
 * Parent directories are created. Returns 1 on success, 0 on failure.
 */
int export_results_csv(const char *path, const WTAPInstance *instances,
                       const double *objectives, const double *seconds,
                       int count);

/*
 * Remove every --save-csv argument and remember that a results CSV was requested.
 * Safe to call more than once. Returns 1 when any call in this process saw the flag.
 */
int wtap_take_save_csv_flag(int *argc, char **argv);

/* 1 after --save-csv was seen. Results files stay unwritten until then. */
int wtap_save_csv_enabled(void);

#endif /* OUTPUT_H */

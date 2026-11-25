#ifndef RUNNER_H
#define RUNNER_H

#include "wtap.h"

typedef void (*WTAPSolver)(WTAPInstance *inst);

/*
 * Load instances, run solver on each, and print results.
 * A results CSV is written to csv_path only when argv contains --save-csv.
 * Set env WTAP_RESULTS_CSV to override csv_path when that flag is present.
 * Returns 0 on success, 1 on failure.
 */
int run_solver(const char *algorithm_name, const char *csv_path,
               WTAPSolver solve, int argc, char *argv[]);

#endif /* RUNNER_H */

#ifndef STRING_GA_H
#define STRING_GA_H

#include "wtap.h"

#include <stdint.h>

/*
 * Direct string coding: chromosome[w] is the target assigned to weapon w.
 * Zero values in population_size/generations/mutation_rate select the seeded
 * defaults: 2*num_weapons, 50*num_weapons, and 1/num_weapons respectively.
 */
typedef struct {
    int population_size;
    int generations;
    double crossover_probability;
    double mutation_rate;
    uint64_t random_seed;
} StringGAConfig;

typedef struct {
    int population_size;
    int generations;
    double seed_objective;
    double best_objective;

    /* generations + 1 entries; entry 0 is the initial population. */
    double *best_history;

    /* Complete final population and its objective values. */
    int *final_population;
    double *final_objectives;
} StringGAResult;

/*
 * Run a seeded string-coded GA and place its best chromosome in
 * inst->assignment. seed_assignment must contain inst->num_weapons targets.
 * Returns 1 on success and 0 on invalid input or allocation failure.
 */
int string_ga_solve(WTAPInstance *inst, const int *seed_assignment,
                    const StringGAConfig *config, StringGAResult *result);

void string_ga_result_free(StringGAResult *result);

#endif /* STRING_GA_H */

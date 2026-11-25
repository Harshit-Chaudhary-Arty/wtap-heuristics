#include "string_ga.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t state;
} GARandom;

static uint64_t random_u64(GARandom *rng) {
    uint64_t z = (rng->state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static int random_index(GARandom *rng, int limit) {
    return (int)(random_u64(rng) % (uint64_t)limit);
}

static double random_unit(GARandom *rng) {
    return (double)(random_u64(rng) >> 11) * (1.0 / 9007199254740992.0);
}

static double evaluate_chromosome(const WTAPInstance *inst, const int *chromosome) {
    double surviving[MAX_TARGETS];
    double total = 0.0;

    for (int t = 0; t < inst->num_targets; t++) {
        surviving[t] = inst->target_values[t];
    }

    for (int w = 0; w < inst->num_weapons; w++) {
        int target = chromosome[w];
        surviving[target] *= 1.0 - inst->prob[w][target];
    }

    for (int t = 0; t < inst->num_targets; t++) {
        total += surviving[t];
    }
    return total;
}

/* The two contestants are distinct: sampling is without replacement. */
static int tournament_select(const double *objectives, int population_size,
                             GARandom *rng) {
    int first = random_index(rng, population_size);
    int second = random_index(rng, population_size - 1);
    if (second >= first) {
        second++;
    }
    return objectives[first] <= objectives[second] ? first : second;
}

static int best_index(const double *objectives, int population_size) {
    int best = 0;
    for (int i = 1; i < population_size; i++) {
        if (objectives[i] < objectives[best]) {
            best = i;
        }
    }
    return best;
}

static void crossover(const int *first, const int *second, int *child,
                      int chromosome_length, GARandom *rng) {
    if (chromosome_length < 2) {
        child[0] = first[0];
        return;
    }

    /* Assumption: positional crossover is a single random cut. */
    int cut = 1 + random_index(rng, chromosome_length - 1);
    memcpy(child, first, (size_t)cut * sizeof(*child));
    memcpy(child + cut, second + cut,
           (size_t)(chromosome_length - cut) * sizeof(*child));
}

static void mutate(const int *parent, int *child, int chromosome_length,
                   int num_targets, double mutation_rate, GARandom *rng) {
    memcpy(child, parent, (size_t)chromosome_length * sizeof(*child));

    int changed = 0;
    for (int w = 0; w < chromosome_length; w++) {
        if (random_unit(rng) < mutation_rate && num_targets > 1) {
            int replacement = random_index(rng, num_targets - 1);
            if (replacement >= child[w]) {
                replacement++;
            }
            child[w] = replacement;
            changed = 1;
        }
    }

    /* A selected mutation operator always changes one gene when possible. */
    if (!changed && num_targets > 1) {
        int w = random_index(rng, chromosome_length);
        int replacement = random_index(rng, num_targets - 1);
        if (replacement >= child[w]) {
            replacement++;
        }
        child[w] = replacement;
    }
}

void string_ga_result_free(StringGAResult *result) {
    if (result == NULL) {
        return;
    }
    free(result->best_history);
    free(result->final_population);
    free(result->final_objectives);
    memset(result, 0, sizeof(*result));
}

int string_ga_solve(WTAPInstance *inst, const int *seed_assignment,
                    const StringGAConfig *config, StringGAResult *result) {
    if (inst == NULL || seed_assignment == NULL || config == NULL || result == NULL ||
        inst->num_weapons <= 0 || inst->num_targets <= 0 ||
        config->crossover_probability < 0.0 ||
        config->crossover_probability > 1.0) {
        return 0;
    }

    memset(result, 0, sizeof(*result));
    int weapons = inst->num_weapons;
    int targets = inst->num_targets;
    int population_size =
        config->population_size > 0 ? config->population_size : 2 * weapons;
    int generations =
        config->generations > 0 ? config->generations : 50 * weapons;
    double mutation_rate =
        config->mutation_rate > 0.0 ? config->mutation_rate : 1.0 / weapons;

    if (population_size < 2 || generations < 0 || mutation_rate > 1.0) {
        return 0;
    }
    for (int w = 0; w < weapons; w++) {
        if (seed_assignment[w] < 0 || seed_assignment[w] >= targets) {
            return 0;
        }
    }

    size_t genes = (size_t)population_size * (size_t)weapons;
    int *population = malloc(genes * sizeof(*population));
    int *next_population = malloc(genes * sizeof(*next_population));
    double *objectives = malloc((size_t)population_size * sizeof(*objectives));
    double *next_objectives =
        malloc((size_t)population_size * sizeof(*next_objectives));
    result->best_history =
        malloc((size_t)(generations + 1) * sizeof(*result->best_history));

    if (population == NULL || next_population == NULL || objectives == NULL ||
        next_objectives == NULL || result->best_history == NULL) {
        free(population);
        free(next_population);
        free(objectives);
        free(next_objectives);
        string_ga_result_free(result);
        return 0;
    }

    GARandom rng = {config->random_seed};
    memcpy(population, seed_assignment, (size_t)weapons * sizeof(*population));
    for (int i = 1; i < population_size; i++) {
        for (int w = 0; w < weapons; w++) {
            population[(size_t)i * weapons + w] = random_index(&rng, targets);
        }
    }

    for (int i = 0; i < population_size; i++) {
        objectives[i] =
            evaluate_chromosome(inst, population + (size_t)i * weapons);
    }
    result->seed_objective = objectives[0];
    result->best_history[0] = objectives[best_index(objectives, population_size)];

    for (int generation = 1; generation <= generations; generation++) {
        int elite = best_index(objectives, population_size);
        memcpy(next_population, population + (size_t)elite * weapons,
               (size_t)weapons * sizeof(*next_population));
        next_objectives[0] = objectives[elite];

        for (int i = 1; i < population_size; i++) {
            int *child = next_population + (size_t)i * weapons;
            int first = tournament_select(objectives, population_size, &rng);

            if (random_unit(&rng) < config->crossover_probability) {
                int second = tournament_select(objectives, population_size, &rng);
                crossover(population + (size_t)first * weapons,
                          population + (size_t)second * weapons, child, weapons,
                          &rng);
            } else {
                mutate(population + (size_t)first * weapons, child, weapons,
                       targets, mutation_rate, &rng);
            }
            next_objectives[i] = evaluate_chromosome(inst, child);
        }

        int *population_swap = population;
        population = next_population;
        next_population = population_swap;
        double *objective_swap = objectives;
        objectives = next_objectives;
        next_objectives = objective_swap;

        result->best_history[generation] =
            objectives[best_index(objectives, population_size)];
    }

    int best = best_index(objectives, population_size);
    memcpy(inst->assignment, population + (size_t)best * weapons,
           (size_t)weapons * sizeof(*inst->assignment));

    result->population_size = population_size;
    result->generations = generations;
    result->best_objective = objectives[best];
    result->final_population = population;
    result->final_objectives = objectives;

    free(next_population);
    free(next_objectives);
    return 1;
}

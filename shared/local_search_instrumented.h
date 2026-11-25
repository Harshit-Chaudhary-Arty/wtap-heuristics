#ifndef LOCAL_SEARCH_INSTRUMENTED_H
#define LOCAL_SEARCH_INSTRUMENTED_H

#include "local_search_circular.h"
#include "wtap.h"

#define LS_MAX_IMPROVEMENT_CYCLES 1024
#define LS_MAX_MOVE_LOG 100000

/* Default minimum objective improvement required to apply a move. */
#define LS_DEFAULT_MIN_IMPROVEMENT 0.01

typedef enum {
    LS_MOVE_RELOCATE = 0,
    LS_MOVE_SWAP,
    LS_MOVE_K_CYCLE
} LSMoveKind;

typedef struct {
    int improvement_cycle;
    LSMoveKind kind;
    int weapon;
    int target;
    int weapon_a;
    int weapon_b;
    int cycle_weapons[MAX_CYCLE_SIZE];
    int cycle_size;
    int shift;
    double improvement;
} LSMoveRecord;

typedef struct {
    int improvement_cycle;
    int relocate_moves;
    int swap_moves;
    int k_cycle_moves;
} LSImprovementCycleStats;

typedef struct {
    int num_improvement_cycles;
    LSImprovementCycleStats cycles[LS_MAX_IMPROVEMENT_CYCLES];
    int total_relocate_moves;
    int total_swap_moves;
    int total_k_cycle_moves;
    int num_move_records;
    LSMoveRecord *move_log;
} LocalSearchStats;

void ls_stats_init(LocalSearchStats *stats);
void ls_stats_free(LocalSearchStats *stats);

/*
 * Phased circular local search with optional minimum improvement threshold.
 * Records improvement-cycle counters and a per-move log in stats.
 *
 * min_improvement = 0.0 accepts any strictly improving move.
 */
void improve_with_instrumented_circular_local_search(WTAPInstance *inst, int cycle_size,
                                                     double min_improvement,
                                                     LocalSearchStats *stats);

#endif /* LOCAL_SEARCH_INSTRUMENTED_H */

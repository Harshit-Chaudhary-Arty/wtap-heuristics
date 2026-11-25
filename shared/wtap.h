#ifndef WTAP_H
#define WTAP_H

/* Maximum sizes based on the largest instance in data/Instances_for_GA.txt */
#define MAX_WEAPONS   200
#define MAX_TARGETS   400
#define MAX_INSTANCES 20

/*
 * WTAPInstance holds one problem instance.
 *
 * Dataset convention (from Problem_context.md):
 *   n = num_weapons,  m = num_targets
 *   prob[weapon][target] = probability weapon destroys target
 */
typedef struct {
    int instance_id;
    int num_weapons;
    int num_targets;
    double target_values[MAX_TARGETS];
    double prob[MAX_WEAPONS][MAX_TARGETS];
    int assignment[MAX_WEAPONS]; /* assignment[w] = target index for weapon w */
} WTAPInstance;

#endif /* WTAP_H */

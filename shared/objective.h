#ifndef OBJECTIVE_H
#define OBJECTIVE_H

#include "wtap.h"

/* Compute total expected surviving value for the current assignment. */
double calculate_objective(const WTAPInstance *inst);

#endif /* OBJECTIVE_H */

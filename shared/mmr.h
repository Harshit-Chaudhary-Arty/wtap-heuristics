#ifndef MMR_H
#define MMR_H

#include "wtap.h"

/* Global selection assignment greedy (MMR). */
void mmr_solve(WTAPInstance *inst);

/*
 * Same search as mmr_solve, but the pair (forbidden_weapon, forbidden_target)
 * is never chosen. Pass forbidden_weapon < 0 to forbid nothing.
 */
void mmr_solve_forbidding(WTAPInstance *inst, int forbidden_weapon, int forbidden_target);

#endif /* MMR_H */

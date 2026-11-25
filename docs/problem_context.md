# Problem Definition: Static Weapon-Target Assignment Problem (WTAP)

## 1. Problem Overview

The Weapon-Target Assignment Problem (WTAP) is an optimization problem in which a set of available weapons must be assigned to a set of targets.

Each target has a value representing its potential to cause damage. Each weapon has a known probability of destroying each target.

The objective is to determine an assignment of weapons to targets that minimizes the total expected surviving value of all targets.

This implementation follows the static WTAP formulation described in Section 3 of the paper, "String- and Permutation-Coded Genetic Algorithms for the Static Weapon-Target Assignment Problem" by Bryant A. Julstrom.

## 2. Inputs

The problem consists of the following inputs:

- `m`: Number of individual weapons.
- `n`: Number of targets.
- `V[t]`: Initial value of target `t`, representing its potential to cause damage.
- `p[w][t]`: Probability that weapon `w` destroys target `t`.

The probability `p[w][t]` is a value between 0 and 1, inclusive.

The target values and weapon-target probabilities are known before the assignment process begins.

Each weapon is considered individually, and each weapon may have different probabilities of destroying different targets.

## 3. Weapon-Target Assignment

An assignment specifies one target for each available weapon.

Every weapon must be assigned to exactly one target.

A target may receive zero, one, or multiple weapons. Multiple weapons are permitted to engage the same target.

There are no restrictions on the number of weapons that may be assigned to an individual target.

The problem is static: all input information is available in advance, and the assignment is determined for the engagement without subsequently adapting it to observed engagement outcomes.

## 4. Expected Surviving Value of a Target

Let `V[t]` denote the current or initial value of target `t`.

For a single weapon `w` assigned to target `t`, let `p[w][t]` denote the probability that the weapon destroys the target.

The expected surviving value of the target after this weapon engages it is:

E(V[t]) = V[t] * (1 - p[w][t])

This expression accounts for the possibility that the target is destroyed, leaving zero value, or survives, retaining its original value.

When multiple weapons are assigned to the same target, the expected surviving value is calculated using the product of the individual weapons' survival factors.

Let `A[t]` be the set of weapons assigned to target `t`.

Then:

E(V[t]) = V[t] * PRODUCT(1 - p[w][t]), for all w in A[t]

If no weapons are assigned to a target, its expected surviving value remains equal to its initial value.

## 5. Objective Function

The total expected surviving value of the targets is:

TotalExpectedSurvivingValue = SUM(E(V[t])), for t = 1 to n

The objective of WTAP is to minimize this total expected surviving value.

Equivalently, the assignment should reduce the expected surviving value of the incoming targets as much as possible.

A smaller objective value represents a better assignment.

The objective is the sum of the expected surviving values of all targets, not merely the value of an individual target.

## 6. Required Problem Characteristics

The problem formulation must preserve the following characteristics:

1. There are `m` individual weapons and `n` targets.
2. Each target has an initial value.
3. Each weapon-target pair has a specified destruction probability.
4. Every weapon is assigned to exactly one target.
5. Multiple weapons may be assigned to the same target.
6. Assignments are made using information known before the engagement.
7. The expected surviving value of a target depends on all weapons assigned to it.
8. The overall objective is to minimize the sum of expected surviving target values.

The formulation is based on the static Weapon-Target Assignment Problem described in the paper. It does not prescribe a particular algorithm for finding an assignment.


## Dataset Format and Dimension Convention

The supplied instance file uses the following convention:

- `n`: Number of weapons.
- `m`: Number of targets.
- `V_j`: Array of target values, containing `m` elements.
- `P_ij`: Probability matrix with `n` rows and `m` columns.

The element `P_ij[i][j]` represents the probability that weapon `i`
destroys target `j`.

Each row of the probability matrix corresponds to one weapon.
Each column corresponds to one target.

Important: These dimension labels differ from the convention used
in parts of the reference paper. The implementation must follow
the actual input data and the conventions specified in this section.

When implementing in C, use descriptive variable names such as num_weapons and num_targets rather than relying on m and n. This will help prevent accidental transposition of the probability matrix.

Also add comments for understanding and keep the implementation simple and wasy to understand, not complex.
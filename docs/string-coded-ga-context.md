# Implementation Context: String-Coded Genetic Algorithm for the Static Weapon-Target Assignment Problem

## 1. Project Objective

Implement a String-Coded Genetic Algorithm (SCGA) to solve the Static Weapon-Target Assignment Problem (SWTAP), following:

Bryant A. Julstrom, "String- and Permutation-Coded Genetic Algorithms for the Static Weapon-Target Assignment Problem", GECCO 2009.

The implementation should be in MATLAB.

The primary goal is to minimize the expected surviving value of all targets after assigning weapons to targets.

The implementation should be modular, readable, and easy to test.

---

## 2. Problem Definition

Let:

- m = number of weapons.
- n = number of targets.
- V(j) = initial value of target j.
- p(i,j) = probability that weapon i destroys target j.

Each weapon must be assigned to exactly one target.

Multiple weapons are allowed to attack the same target.

### Decision Variable

Represent the weapon-target assignment using a chromosome:

C = [c(1), c(2), ..., c(m)]

where:

c(i) = target assigned to weapon i.

Each chromosome must satisfy:

1 <= c(i) <= n

The chromosome length must always equal m.

Example:

Suppose there are four weapons and three targets.

C = [2, 1, 3, 2]

This represents:

- Weapon 1 attacks Target 2.
- Weapon 2 attacks Target 1.
- Weapon 3 attacks Target 3.
- Weapon 4 attacks Target 2.

This is a valid chromosome.

IMPORTANT: This is direct assignment encoding, NOT permutation encoding. Repeated target labels are allowed.

---

## 3. Objective Function

The objective is to minimize the total expected surviving target value.

For target j:

E(V_j) = V(j) * product of (1 - p(i,j))

where the product includes only weapons assigned to target j.

More formally:

E(V_j) = V(j) * PRODUCT[(1 - p(i,j)) for all i such that c(i) = j]

The total objective is:

F(C) = SUM[E(V_j)] for j = 1 to n

The GA must minimize F(C).

A lower objective value indicates a better weapon-target assignment.

### Fitness Evaluation Procedure

Follow the evaluation approach described in Section 6 of the paper.

1. Create a local copy of the initial target values:

   locV = V

2. Process every weapon i:

   j = C(i)

   locV(j) = (1 - p(i,j)) * locV(j)

3. After processing all weapons:

   objective = sum(locV)

The original target values must not be modified.

The fitness evaluation should return the total expected surviving target value.

The paper gives the evaluation complexity as:

O(max(m,n))

---

## 4. Genetic Algorithm Configuration

Follow the GA configuration described in Section 8.

### Population

Population size:

POP_SIZE = 2*m

Each chromosome has length m.

Generate an initial population of valid random chromosomes.

Each gene is independently selected from the integer range:

1 to n

### Parent Selection

Use k-tournament selection without replacement within each tournament.

For the experiments in the paper:

k = 2

Since this is a minimization problem, the chromosome with the LOWER objective value wins the tournament.

Tournament selection should return a selected parent chromosome.

### Crossover and Mutation

For the String-Coded GA, the paper specifies:

Crossover probability = 0.30

Mutation probability = 0.70

Each offspring is generated using EITHER crossover OR mutation, never both.

The probabilities sum to 1.

IMPORTANT:

Section 6 explains that string coding supports familiar positional operators, such as k-point crossover and position-by-position mutation.

However, the paper does not explicitly identify the exact string-coded crossover and mutation operators used in Section 8.

Therefore:

- Implement a configurable positional crossover operator.
- Implement a configurable position-by-position mutation operator.
- Do not claim that a particular crossover-point count or mutation probability per gene is explicitly specified by the paper.
- Document any chosen operator settings as implementation assumptions.

The operators must preserve the validity of the string representation:

- Chromosome length remains m.
- Every gene remains an integer between 1 and n.
- Repeated target labels are permitted.

### Elitism

Use 1-elitism.

The best chromosome in the current generation must be copied unchanged into the next generation.

Because the problem is a minimization problem, preserve the chromosome with the lowest objective value.

### Termination

The algorithm runs for a fixed number of generations.

The paper specifies:

UNSEEDED STRING GA:
- Population size = 2*m
- Number of generations = 200*m

SEEDED STRING GA:
- Population size = 2*m
- Number of generations = 50*m

Implement both configurations.

---

## 5. Seeded and Unseeded Versions

The implementation should support two modes.

### Mode A: Unseeded SCGA

Initialize the population using random valid chromosomes.

Do not include an MMR solution.

Use:

- Population size = 2*m
- Generations = 200*m

### Mode B: Seeded SCGA

Initialize the population with random valid chromosomes and include the MMR solution as one member of the initial population.

The seeded chromosome must directly represent the MMR weapon-target assignments.

Use:

- Population size = 2*m
- Generations = 50*m

The MMR solution must be inserted without modifying its assignments.

The implementation should accept the MMR solution as an input rather than requiring the GA to implement MMR internally.

---

## 6. Suggested MATLAB Program Structure

Implement the algorithm using separate MATLAB functions.

Suggested files:

### Main Functions

1. string_ga.m

   Main function controlling the GA.

2. initialize_population.m

   Creates the initial population of valid string-coded chromosomes.

3. evaluate_string.m

   Calculates the objective value of a chromosome.

4. tournament_selection.m

   Performs tournament parent selection.

5. string_crossover.m

   Implements positional crossover for string chromosomes.

6. string_mutation.m

   Implements position-by-position mutation.

7. run_string_ga.m

   Runs the selected seeded or unseeded configuration and returns the results.

These functions should have clear inputs and outputs.

---

## 7. Main GA Workflow

The main algorithm should follow this sequence:

STEP 1: Read problem data.

- Number of weapons m.
- Number of targets n.
- Target values V.
- Destruction probability matrix P.

STEP 2: Initialize the population.

- Generate random chromosomes.
- If seeded mode is enabled, insert the MMR chromosome.

STEP 3: Evaluate all chromosomes.

- Calculate the objective value of each chromosome.

STEP 4: Identify the best chromosome.

- Find the chromosome with the lowest objective value.

STEP 5: Create the next generation.

Repeat until the next generation has the required population size:

a. Select parents using 2-tournament selection.

b. Decide whether to use crossover or mutation.

c. Generate an offspring.

d. Evaluate the offspring.

e. Add it to the next generation.

STEP 6: Apply elitism.

- Ensure that the best chromosome from the previous generation is copied unchanged into the next generation.
- Avoid duplicating the elite if it is already preserved by the generation-building procedure.

STEP 7: Replace the old population with the new population.

STEP 8: Repeat until the fixed generation limit is reached.

STEP 9: Return the best solution found.

---

## 8. Required Outputs

The algorithm should return:

1. Best chromosome.
2. Best objective value.
3. Best objective value at each generation.
4. Final population.
5. Final population objective values.
6. Total number of generations.
7. Execution time.

The convergence history should make it possible to plot:

X-axis: Generation number.

Y-axis: Best objective value.

Since this is a minimization problem, the best-so-far objective value should never increase.

---

## 9. Validation Requirements

Test the implementation with small artificial instances before using the full benchmark instances.

Verify:

1. Every chromosome has exactly m genes.
2. Every gene is an integer between 1 and n.
3. Multiple weapons can be assigned to the same target.
4. Fitness evaluation matches the mathematical objective.
5. Crossover produces valid chromosomes.
6. Mutation produces valid chromosomes.
7. Tournament selection chooses the better of the two candidates when k = 2.
8. Elitism preserves the best solution.
9. The population size remains exactly 2*m.
10. The algorithm executes the required number of generations.

Also verify that the best-so-far objective value is non-increasing across generations.

---

## 10. Important Implementation Restrictions

Do NOT implement permutation coding.

Do NOT use PMX crossover.

Do NOT use swap mutation.

Do NOT force every target to receive a weapon.

Do NOT prohibit multiple weapons from attacking the same target.

Do NOT maximize the objective function.

Do NOT modify the original target values during fitness evaluation.

Do NOT combine crossover and mutation on the same offspring.

Do NOT silently introduce additional constraints into the WTAP.

---

## 11. Paper Fidelity and Implementation Assumptions

The implementation should distinguish between the following:

### Explicitly specified by the paper

- Direct string representation of length m.
- Gene values represent target assignments.
- Expected surviving target value is minimized.
- Population size is 2*m.
- Tournament selection without replacement, with k = 2 in the experiments.
- Crossover probability is 30%.
- Mutation probability is 70%.
- Each offspring is generated by either crossover or mutation, not both.
- 1-elitism.
- Fixed generation limits.
- Unseeded SCGA uses 200*m generations.
- Seeded SCGA uses 50*m generations.
- Seeded population includes the MMR assignment.

### Not explicitly specified for the string-coded implementation

- Exact positional crossover operator and number of crossover points.
- Exact position-by-position mutation implementation.
- Exact per-gene mutation probability, if mutation is implemented as independent gene changes.
- Any additional implementation-specific randomization details.

Do not present implementation assumptions as facts from the paper.

Where the paper does not provide sufficient detail, choose simple configurable implementations and clearly document those choices.

---

## 12. Final Instruction to the Implementing AI

Implement a complete, working MATLAB String-Coded Genetic Algorithm for the Static Weapon-Target Assignment Problem using the specifications above.

Prioritize:

1. Correct mathematical objective function.
2. Correct direct string encoding.
3. Correct GA workflow.
4. Correct seeded and unseeded modes.
5. Valid crossover and mutation operations.
6. Correct elitism and tournament selection.
7. Reproducible experiments and clear outputs.

Write clean, modular MATLAB code.

Explain each function briefly.

Do not implement permutation coding or MMR internally unless explicitly requested.

If an implementation detail is not specified by the paper, state the assumption rather than inventing a paper-specific rule.
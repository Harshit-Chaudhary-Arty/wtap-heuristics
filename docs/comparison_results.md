# WTAP Algorithm Comparison

Dataset: `data/Instances_for_GA.txt` (15 instances)

**Lower objective = better solution.** All times are solver time in seconds.

## Algorithms

| # | Name | Description |
|---|------|-------------|
| 1 | Fixed-Order Sequential Greedy | Assigns weapons in index order; each weapon picks its best target |
| 2 | Global Selection Assignment Greedy (MMR) | Repeatedly picks the best unassigned weapon-target pair globally |
| 3 | MMR + Local Search | Starts with MMR, then improves via relocate and swap moves |

## Per-Instance Results

| Instance | Weapons | Targets | Algo 1 Objective | Algo 1 Time (s) | Algo 2 Objective | Algo 2 Time (s) | Algo 3 Objective | Algo 3 Time (s) | Best Objective |
|----------|---------|---------|------------------|-----------------|------------------|-----------------|------------------|-----------------|----------------|
| 1 | 20 | 10 | 22.5040 | 0.000006 | 13.5039 | 0.000016 | **11.5769** | 0.000603 | Algo 3 |
| 2 | 20 | 20 | 225.9585 | 0.000005 | 156.8843 | 0.000024 | **149.7652** | 0.000931 | Algo 3 |
| 3 | 20 | 40 | 1148.0690 | 0.000007 | 1102.5654 | 0.000042 | **1091.0274** | 0.001733 | Algo 3 |
| 4 | 40 | 20 | 44.9687 | 0.000011 | 18.9167 | 0.000076 | **18.2319** | 0.005103 | Algo 3 |
| 5 | 40 | 40 | 393.4158 | 0.000018 | 298.4345 | 0.000168 | **284.6869** | 0.016392 | Algo 3 |
| 6 | 40 | 80 | 2290.1283 | 0.000019 | 2185.7995 | 0.000276 | **2176.4405** | 0.005779 | Algo 3 |
| 7 | 80 | 40 | 57.4407 | 0.000031 | 29.7002 | 0.000564 | **29.2906** | 0.015041 | Algo 3 |
| 8 | 80 | 80 | 749.6934 | 0.000052 | 553.1720 | 0.001122 | **540.7995** | 0.031867 | Algo 3 |
| 9 | 80 | 160 | 4433.1033 | 0.000088 | 4315.9768 | 0.002120 | **4296.7889** | 0.053413 | Algo 3 |
| 10 | 100 | 50 | 74.5579 | 0.000040 | 34.4192 | 0.001115 | **33.2510** | 0.053714 | Algo 3 |
| 11 | 100 | 100 | 875.6962 | 0.000061 | 653.0426 | 0.002184 | **644.7152** | 0.037074 | Algo 3 |
| 12 | 100 | 200 | 5278.4190 | 0.000091 | 5114.6828 | 0.004298 | **5099.2983** | 0.109013 | Algo 3 |
| 13 | 200 | 100 | 137.6344 | 0.000109 | 71.3340 | 0.009339 | **70.0184** | 0.662613 | Algo 3 |
| 14 | 200 | 200 | 1549.4885 | 0.000209 | 1247.0042 | 0.016975 | **1235.8729** | 0.649977 | Algo 3 |
| 15 | 200 | 400 | 10155.8620 | 0.000389 | 9915.2153 | 0.031651 | **9908.4214** | 0.959126 | Algo 3 |

## Summary

| Metric | Algo 1 | Algo 2 (MMR) | Algo 3 (MMR + LS) |
|--------|--------|--------------|-------------------|
| Total solve time (s) | 0.001136 | 0.069970 | 2.602379 |
| Instances with best objective | 0 / 15 | 0 / 15 | **15 / 15** |

## Observations

- **Algo 1** is the fastest but produces the weakest solutions.
- **Algo 2 (MMR)** is a large improvement over Algo 1 with only a small runtime increase.
- **Algo 3** gives the best objective on every instance, but is slower — especially on large instances (13–15).
- The biggest objective gaps are on smaller/medium instances (e.g. instance 1: 22.50 vs 11.58).
- On the largest instance (15: 200 weapons, 400 targets), all three are closer in objective, but Algo 3 still wins.

## How to Reproduce

```bash
make
./build/1_fixed_order_sequential_greedy
./build/2_global_selection_assignment_greedy
./build/3_greedy_local_search
```

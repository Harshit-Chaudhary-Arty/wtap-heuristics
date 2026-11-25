# Threshold tuning

Algorithm 4.2 is `build/4_2_circular_threshold_local_search`.
Tune only `min_improvement`. Keep the cycle size at 2.

Run the sweep with:

```bash
bash scripts/sweep_threshold.sh
```

Output lands in `results/experiments/circular-threshold-sweep/` (CSV + per-threshold logs).
The ranking printed by that script is the result. Do not edit the C sources
during a sweep, and do not try cycle sizes above 2.

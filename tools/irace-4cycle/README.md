# irace for the 4-cycle (parameter_control_heuristic, cycle size 4)

Separate from `tools/irace/` (3-cycle); nothing there is shared or modified, except
that the 15 training instances are read from `../irace/instances/instances.list`.

Tuned: `min_improvement` (0.001–0.2), `top_percent` (5–100). Fixed: cycle size 4,
time limit = `TIME_LIMIT`. Cost = final objective (lower is better).

## Run

```bash
# 500-experiment run, 60 s per run, one run at a time
MAX_EXPERIMENTS=500 TIME_LIMIT=60 bash tools/irace-4cycle/start.sh

# same, 4 runs in parallel (faster, slightly noisier near the time limit)
MAX_EXPERIMENTS=500 TIME_LIMIT=60 PARALLEL=4 bash tools/irace-4cycle/start.sh

# only print the setup and the irace command
DRY_RUN=1 bash tools/irace-4cycle/start.sh
```

| Variable          | Default | Meaning                                         |
| ----------------- | ------- | ----------------------------------------------- |
| `MAX_EXPERIMENTS` | 500     | irace budget (config × instance runs)           |
| `TIME_LIMIT`      | 60      | per-run wall-clock limit, seconds (min 16)      |
| `PARALLEL`        | 1       | simultaneous runs                               |
| `SEED`            | 42      | irace seed                                      |

## Outputs

- `results/parameter-control/irace-4cycle/(budget)-config-N.csv` – elite table (auto-exported at the end)
- `results/parameter-control/irace-4cycle/experiments.log` – one line per run incl. `timed_out=0/1` (gitignored)
- `results/parameter-control/irace-4cycle/irace.Rdata` – full irace log (gitignored)

## Apply the winner on the full instance set

```bash
bash tools/irace-4cycle/apply_best.sh                 # latest run, same time limit as tuning
bash tools/irace-4cycle/apply_best.sh 2               # winner of run 2
APPLY_TIME_LIMIT=100000 bash tools/irace-4cycle/apply_best.sh   # no time cap
```

Writes `results/parameter-control/Irace 4-Cycle Result/(budget)-applied-N.csv`.

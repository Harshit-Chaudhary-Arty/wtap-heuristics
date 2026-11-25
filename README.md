# WTAP major project

Weapon-Target Assignment heuristics and a seeded string-coded GA, plus local result viewers.

## Layout

```
algorithms/     # one folder per solver (4.1–4.4, GA, parameter control, …)
shared/         # common C helpers (parser, MMR, local search, runner)
data/           # instance file(s)
results/        # CSV outputs (hybrid layout)
  algorithms/           # one CSV per standalone solver
  parameter-control/    # param heuristic + irace/
  experiments/          # multi-file runs (sweeps, GA, …)
tools/          # csv-viewer, mlflow, datasette, irace
docs/           # notes and comparison writeups
scripts/        # e.g. threshold sweep for 4.2
build/          # compiled binaries (gitignored)
```

## Build & run

From the repo root:

```bash
make
make run-4-4          # writes results/algorithms/mmr-top50-3-cycles.csv
make run-param        # writes results/parameter-control/parameter-control.csv
bash scripts/sweep_threshold.sh   # results/experiments/circular-threshold-sweep/
```

Defaults assume `data/Instances_for_GA.txt` and paths under `results/`.

## Viewers

```bash
bash tools/csv-viewer/start.sh    # http://127.0.0.1:5173  (reads results/)
bash tools/mlflow/start.sh        # http://127.0.0.1:5000
bash tools/datasette/start.sh     # http://127.0.0.1:8001
```

## irace (parameter_control_heuristic)

Tunes `min_improvement` and `top_percent` (time limit fixed at 16s):

```bash
bash tools/irace/start.sh
# each run writes results/parameter-control/irace/(budget)-config-N.csv
bash tools/irace/apply_best.sh
# applies that winner -> .../Irace Algo Result/(budget)-applied-N.csv
```

See `tools/irace/README.md`.

## Threshold tuning (4.2)

See `AGENTS.md`. Cycle size stays at 2; only tune `min_improvement`.

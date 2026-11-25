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

### One configurable solver: `parameter_control_heuristic`

Any cycle size, threshold and top-% from one binary (no separate 3-/4-/5-cycle programs):

```bash
make build/parameter_control_heuristic
./build/parameter_control_heuristic [min_improvement] [top_percent] [time_limit_s] [cycle_size] \
    --save-csv [input_file] [results_csv]

# 4-cycles, top 50%, no threshold, effectively no time limit
./build/parameter_control_heuristic 0 50 100000 4 --save-csv \
    data/Instances_for_GA.txt results/experiments/my-run/param-4cycle.csv
```

- `min_improvement 0` = no threshold: best-improvement until a whole round finds no improving move.
- `time_limit_s` is at least 16; pass a huge value to effectively disable it.
- Large `C(n, k)` (e.g. k=4 on 200 weapons) is handled without storing every combination:
  the kept top-% is selected exactly and re-enumerated per scan. Combinations above
  4e9 are refused up front, before any instance runs.
- Always pass an explicit `results_csv`, otherwise the default path is overwritten.

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

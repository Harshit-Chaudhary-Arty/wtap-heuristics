# irace for parameter_control_heuristic

Tunes the standalone parameter-control heuristic with [irace](https://mlopez-ibanez.github.io/irace/).

## What is tuned

| Parameter | Range | Notes |
|-----------|-------|-------|
| `min_improvement` | 0.001 … 0.2 | round stopping threshold |
| `top_percent` | 5 … 100 | share of ranked 3-cycle candidates kept |

`time_limit_seconds` is **fixed at 16** (solver minimum/default). Tuning it under a floor of 16s is not useful.

Cost = final objective on one training instance (minimized).

## Layout

```
tools/irace/
  parameters.txt
  scenario.txt
  target-runner
  instances/inst_XX.txt
  instances/instances.list
  start.sh
  export_best.sh
results/parameter-control/irace/   # execDir + irace.Rdata + CSV export
```

## Prerequisites

`start.sh` looks for R + irace in this order:

1. Micromamba env at `~/micromamba/envs/irace` (created once with conda-forge `r-base` + `r-irace`)
2. System/user `Rscript` with the `irace` package
3. Docker image from `tools/irace/Dockerfile`

One-time micromamba setup (no sudo):

```bash
# if micromamba is not installed yet, follow https://mamba.readthedocs.io/
micromamba create -y -p ~/micromamba/envs/irace -c conda-forge r-base r-irace
```

## Run

```bash
bash tools/irace/start.sh
```

Short smoke budget:

```bash
MAX_EXPERIMENTS=100 bash tools/irace/start.sh
```

(irace needs roughly ≥96 experiments with this setup.)

`start.sh` writes a new CSV after each run:

```
results/parameter-control/irace/(500)-config-1.csv
results/parameter-control/irace/(100)-config-2.csv
...
```

Re-export the current log without starting another race:

```bash
bash tools/irace/export_best.sh
```

The same `irace.Rdata` is not exported twice. Each file is one run. Rows are the elite configurations, best first. The first columns are the parameters that won: `min_improvement`, `top_percent`, and `mean_cost`, then config id and how many instances that config was scored on. The remaining columns are that run's settings: when it finished, how many irace iterations it raced, the iteration the winner first appeared, experiment budget and how many were used, seed, the fixed 16s time limit, elapsed time, instance count and sizes (`n`x`m`), and the parameter ranges.

## Apply a winner

Run the parameter-control heuristic on `data/Instances_for_GA.txt` with the rank-1 parameters from an irace CSV:

```bash
bash tools/irace/apply_best.sh        # latest irace run
bash tools/irace/apply_best.sh 2      # irace run 2
```

Each irace run gets its own file:

```
results/parameter-control/Irace Algo Result/(500)-applied-1.csv
results/parameter-control/Irace Algo Result/(100)-applied-2.csv
```

The CSV is the normal per-instance result, with `irace_run`, `config_id`, `min_improvement`, `top_percent`, and `time_limit_s` on every row. Applying the same irace run again replaces that file.

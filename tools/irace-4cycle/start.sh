#!/usr/bin/env bash
# Run irace on parameter_control_heuristic with cycle_size = 4.
# Separate from tools/irace (3-cycle). Output: results/parameter-control/irace-4cycle/
#
# Everything is controlled from the command line via environment variables:
#   MAX_EXPERIMENTS  irace budget = number of (config x instance) runs   [default: 500]
#   TIME_LIMIT       per-run wall-clock limit in seconds (min 16)        [default: 60]
#   PARALLEL         number of runs executed at the same time            [default: 1]
#   SEED             irace seed                                          [default: 42]
#   DRY_RUN=1        print the setup and the irace command, do not run it
#
# Examples:
#   MAX_EXPERIMENTS=500 TIME_LIMIT=60 bash tools/irace-4cycle/start.sh
#   MAX_EXPERIMENTS=500 TIME_LIMIT=60 PARALLEL=4 bash tools/irace-4cycle/start.sh
#   DRY_RUN=1 bash tools/irace-4cycle/start.sh
#
# Note: TIME_LIMIT is wall-clock. With PARALLEL>1 runs compete for CPU/cache, so
# runs that are near the limit get truncated slightly earlier. Use PARALLEL=1 for
# the cleanest comparison with a 3-cycle run; PARALLEL=3-4 is fine for a quick pass.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
EXEC_DIR="$ROOT/results/parameter-control/irace-4cycle"
IMAGE_NAME="${IRACE_IMAGE:-wtap-irace:local}"
MAMBA_ENV="${MAMBA_IRACE_ENV:-$HOME/micromamba/envs/irace}"
MAMBA_BIN="${MAMBA_BIN:-$HOME/micromamba/bin/micromamba}"

MAX_EXPERIMENTS="${MAX_EXPERIMENTS:-500}"
TIME_LIMIT="${TIME_LIMIT:-60}"
PARALLEL="${PARALLEL:-1}"
SEED="${SEED:-42}"
DRY_RUN="${DRY_RUN:-0}"

for name in MAX_EXPERIMENTS TIME_LIMIT PARALLEL SEED; do
  if [[ ! "${!name}" =~ ^[0-9]+$ ]]; then
    echo "error: $name must be a positive integer (got: ${!name})" >&2
    exit 1
  fi
done
if (( TIME_LIMIT < 16 )); then
  echo "error: TIME_LIMIT must be >= 16 (solver minimum, got $TIME_LIMIT)" >&2
  exit 1
fi
if (( PARALLEL < 1 )); then
  echo "error: PARALLEL must be >= 1" >&2
  exit 1
fi

export TIME_LIMIT

cd "$ROOT"
make -s build/parameter_control_heuristic
mkdir -p "$EXEC_DIR"
chmod +x "$DIR/target-runner"

EXTRA_ARGS=(--max-experiments "$MAX_EXPERIMENTS" --seed "$SEED")
if (( PARALLEL > 1 )); then
  EXTRA_ARGS+=(--parallel "$PARALLEL")
fi

echo "Running irace (4-cycle)"
echo "  scenario:         $DIR/scenario.txt"
echo "  execDir:          $EXEC_DIR"
echo "  binary:           $ROOT/build/parameter_control_heuristic"
echo "  cycle size:       4"
echo "  time limit:       ${TIME_LIMIT}s per run"
echo "  budget:           $MAX_EXPERIMENTS experiments"
echo "  parallel:         $PARALLEL"
echo "  seed:             $SEED"
echo "  run log:          $EXEC_DIR/experiments.log"

other="$(pgrep -fc 'build/parameter_control_heuristic' || true)"
if [[ "${other:-0}" -gt 0 ]]; then
  echo
  echo "  WARNING: $other other parameter_control_heuristic process(es) already running."
  echo "           They take CPU and can make runs hit the ${TIME_LIMIT}s limit sooner."
fi
echo

if [[ "$DRY_RUN" == "1" ]]; then
  echo "DRY_RUN=1: would run (from $DIR):"
  echo "  irace --scenario $DIR/scenario.txt ${EXTRA_ARGS[*]}"
  exit 0
fi

# Remembered for export_best.sh / apply_best.sh
cat >"$EXEC_DIR/run-settings.env" <<EOF
TIME_LIMIT=$TIME_LIMIT
CYCLE_SIZE=4
MAX_EXPERIMENTS=$MAX_EXPERIMENTS
PARALLEL=$PARALLEL
SEED=$SEED
EOF

run_with_rscript() {
  local irace_bin
  irace_bin="$(Rscript -e 'cat(system.file("bin", "irace", package="irace"))')"
  if [[ -z "$irace_bin" || ! -x "$irace_bin" ]]; then
    echo "Installing irace R package (user library) ..."
    Rscript -e 'install.packages("irace", repos="https://cloud.r-project.org")'
    irace_bin="$(Rscript -e 'cat(system.file("bin", "irace", package="irace"))')"
  fi
  if [[ -z "$irace_bin" || ! -x "$irace_bin" ]]; then
    echo "error: could not locate irace executable" >&2
    exit 1
  fi
  (cd "$DIR" && "$irace_bin" --scenario "$DIR/scenario.txt" "${EXTRA_ARGS[@]}")
}

run_mamba() {
  echo "Using micromamba env: $MAMBA_ENV"
  local irace_bin
  irace_bin="$("$MAMBA_BIN" run -p "$MAMBA_ENV" Rscript -e 'cat(system.file("bin", "irace", package="irace"))')"
  if [[ -z "$irace_bin" || ! -x "$irace_bin" ]]; then
    echo "error: irace binary missing in $MAMBA_ENV" >&2
    exit 1
  fi
  (cd "$DIR" && "$MAMBA_BIN" run -p "$MAMBA_ENV" "$irace_bin" \
      --scenario "$DIR/scenario.txt" "${EXTRA_ARGS[@]}")
}

run_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "error: no R/irace environment available." >&2
    echo "Create one with:" >&2
    echo "  micromamba create -y -p ~/micromamba/envs/irace -c conda-forge r-base r-irace" >&2
    exit 1
  fi
  if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Building Docker image $IMAGE_NAME ..."
    docker build -t "$IMAGE_NAME" "$DIR/../irace"
  fi
  docker run --rm -e TIME_LIMIT="$TIME_LIMIT" -v "$ROOT:/work" -w /work/tools/irace-4cycle "$IMAGE_NAME" \
    bash -lc '
      set -euo pipefail
      IRACE_BIN="$(Rscript -e "cat(system.file(\"bin\", \"irace\", package=\"irace\"))")"
      chmod +x ./target-runner
      exec "$IRACE_BIN" --scenario ./scenario.txt '"$(printf '%q ' "${EXTRA_ARGS[@]}")"'
    '
}

if [[ -x "$MAMBA_BIN" && -d "$MAMBA_ENV" ]]; then
  run_mamba
elif command -v Rscript >/dev/null 2>&1; then
  run_with_rscript
else
  run_docker
fi

echo
echo "irace finished."
echo "Log:     $EXEC_DIR/irace.Rdata"
bash "$DIR/export_best.sh"

if [[ -f "$EXEC_DIR/experiments.log" ]]; then
  total="$(grep -c 'config=' "$EXEC_DIR/experiments.log" || true)"
  capped="$(grep -c 'timed_out=1' "$EXEC_DIR/experiments.log" || true)"
  echo "Runs logged: $total, hit the ${TIME_LIMIT}s limit: $capped"
fi

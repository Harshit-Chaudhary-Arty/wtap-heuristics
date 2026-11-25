#!/usr/bin/env bash
# Install (if needed) and run irace on parameter_control_heuristic.
#
# Prefers, in order:
#   1) MAMBA_IRACE_ENV (default ~/micromamba/envs/irace) if present
#   2) local Rscript + irace package
#   3) Docker image built from tools/irace/Dockerfile
#
# Usage:
#   bash tools/irace/start.sh
#   MAX_EXPERIMENTS=60 bash tools/irace/start.sh   # shorter smoke run
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
EXEC_DIR="$ROOT/results/parameter-control/irace"
IMAGE_NAME="${IRACE_IMAGE:-wtap-irace:local}"
MAMBA_ENV="${MAMBA_IRACE_ENV:-$HOME/micromamba/envs/irace}"
MAMBA_BIN="${MAMBA_BIN:-$HOME/micromamba/bin/micromamba}"

cd "$ROOT"
make -s build/parameter_control_heuristic
mkdir -p "$EXEC_DIR"
chmod +x "$DIR/target-runner"

EXTRA_ARGS=()
if [[ -n "${MAX_EXPERIMENTS:-}" ]]; then
  EXTRA_ARGS+=(--max-experiments "$MAX_EXPERIMENTS")
fi

echo "Running irace"
echo "  scenario: $DIR/scenario.txt"
echo "  execDir:  $EXEC_DIR"
echo "  binary:   $ROOT/build/parameter_control_heuristic"
[[ ${#EXTRA_ARGS[@]} -gt 0 ]] && echo "  extra:    ${EXTRA_ARGS[*]}"
echo

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
    docker build -t "$IMAGE_NAME" "$DIR"
  fi
  docker run --rm -v "$ROOT:/work" -w /work/tools/irace "$IMAGE_NAME" \
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

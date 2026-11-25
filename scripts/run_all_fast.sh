#!/usr/bin/env bash
# Build and run all solvers that finish in reasonable time.
# Skips full 3-cycle local search (4.1, 4.3) and the seeded GA (6.4.1).
# Single-CSV algorithms write directly under results/algorithms/.
set -euo pipefail

cd "$(dirname "$0")/.."

make -s all

mkdir -p \
  results/algorithms \
  results/parameter-control \
  results/experiments/circular-threshold-sweep \
  results/experiments/seeded-string-ga

LOG_DIR=results/experiments
mkdir -p "$LOG_DIR"

# Each solver writes its own CSV (with final assignments) under results/algorithms/.
run_table() {
  local csv_name="$1"
  shift
  local log_path="${LOG_DIR}/${csv_name}.log"
  echo
  echo "=== ${csv_name} ==="
  "$@" | tee "$log_path"
}

run_native_csv() {
  local name="$1"
  shift
  echo
  echo "=== ${name} ==="
  "$@"
}

echo "Skipping slow solvers: 4.1 (full 3-cycle), 4.3 (instrumented 3-cycle), 6.4.1 (seeded GA)."

run_table fixed-order-sequential-greedy \
  ./build/1_fixed_order_sequential_greedy --save-csv

run_table global-selection-greedy \
  ./build/2_global_selection_assignment_greedy --save-csv

run_table greedy-local-search \
  ./build/3_greedy_local_search --save-csv

run_table mmr-fast-local-search \
  ./build/3_1_greedy_local_search --save-csv

run_table mmr-phased-local-search \
  ./build/4_mmr_phased_local_search --save-csv

run_table mmr-circular-threshold \
  ./build/4_2_circular_threshold_local_search 2 0.01 --save-csv

run_table mmr-forbidden-rebuild \
  ./build/5_mmr_forbidden_rebuild --save-csv

run_native_csv mmr-top50-3-cycles \
  ./build/4_4_Final_top50 --save-csv

run_native_csv parameter-control \
  ./build/parameter_control_heuristic --save-csv

echo
echo "Done. CSV tree:"
find results -name '*.csv' | sort

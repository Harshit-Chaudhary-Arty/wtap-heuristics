#!/usr/bin/env bash
# Run parameter_control_heuristic (cycle size 4) with the winning parameters of a
# 4-cycle irace run on the full instance set.
#
# Usage:
#   bash tools/irace-4cycle/apply_best.sh        # latest irace run
#   bash tools/irace-4cycle/apply_best.sh 2      # winner of irace run 2
#
# Environment:
#   APPLY_TIME_LIMIT   time limit (s) for the final run. Default: the TIME_LIMIT used
#                      while tuning (same conditions). Use e.g. 100000 for "no cap".
#
# Writes:
#   results/parameter-control/Irace 4-Cycle Result/(budget)-applied-N.csv
# Re-applying the same irace run replaces that file.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
IRACE_DIR="$ROOT/results/parameter-control/irace-4cycle"
OUT_DIR="$ROOT/results/parameter-control/Irace 4-Cycle Result"
INSTANCES="$ROOT/data/Instances_for_GA.txt"
SETTINGS="$IRACE_DIR/run-settings.env"
CYCLE_SIZE=4

TIME_LIMIT="${APPLY_TIME_LIMIT:-}"
if [[ -z "$TIME_LIMIT" && -f "$SETTINGS" ]]; then
  TIME_LIMIT="$(awk -F= '/^TIME_LIMIT=/ { print $2; exit }' "$SETTINGS")"
fi
TIME_LIMIT="${TIME_LIMIT:-60}"

if [[ ! -f "$INSTANCES" ]]; then
  echo "error: missing $INSTANCES" >&2
  exit 1
fi

run_number_of() {
  local base
  base="$(basename "$1" .csv)"
  if [[ "$base" =~ ^\(([0-9]+)\)-config-([0-9]+)$ ]]; then
    echo "${BASH_REMATCH[2]}"
  fi
}

RUN="${1:-}"
if [[ -z "$RUN" ]]; then
  RUN=0
  for f in "$IRACE_DIR"/*.csv; do
    [[ -e "$f" ]] || continue
    n="$(run_number_of "$f")"
    if [[ "$n" =~ ^[0-9]+$ ]] && (( n > RUN )); then
      RUN=$n
    fi
  done
  if (( RUN == 0 )); then
    echo "error: no irace configuration CSV in $IRACE_DIR" >&2
    exit 1
  fi
fi

if [[ ! "$RUN" =~ ^[0-9]+$ ]] || (( RUN < 1 )); then
  echo "error: run number must be a positive integer (got: ${1:-})" >&2
  exit 1
fi

SRC=""
for f in "$IRACE_DIR"/*.csv; do
  [[ -e "$f" ]] || continue
  if [[ "$(run_number_of "$f")" == "$RUN" ]]; then
    SRC="$f"
    break
  fi
done
if [[ -z "$SRC" ]]; then
  echo "error: no configuration file for irace run $RUN" >&2
  exit 1
fi

read -r MIN_IMPROVEMENT TOP_PERCENT CONFIG_ID BUDGET < <(
  python3 - "$SRC" <<'PY'
import csv, sys
path = sys.argv[1]
with open(path, newline="") as handle:
    for row in csv.DictReader(handle):
        if row.get("rank") == "1":
            print(row["min_improvement"], row["top_percent"], row["config_id"], row["budget"])
            raise SystemExit(0)
raise SystemExit("no rank-1 configuration in " + path)
PY
)

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/(${BUDGET})-applied-${RUN}.csv"

cd "$ROOT"
make -s build/parameter_control_heuristic

echo "Applying 4-cycle irace run $RUN"
echo "  config:            $CONFIG_ID"
echo "  min_improvement:   $MIN_IMPROVEMENT"
echo "  top_percent:       $TOP_PERCENT"
echo "  cycle_size:        $CYCLE_SIZE"
echo "  time_limit:        ${TIME_LIMIT}s"
echo "  instances:         $INSTANCES"
echo "  output:            $OUT"
echo

./build/parameter_control_heuristic \
  "$MIN_IMPROVEMENT" "$TOP_PERCENT" "$TIME_LIMIT" "$CYCLE_SIZE" --save-csv \
  "$INSTANCES" "$OUT"

python3 - "$OUT" "$RUN" "$CONFIG_ID" "$MIN_IMPROVEMENT" "$TOP_PERCENT" "$TIME_LIMIT" <<'PY'
import csv, sys
path, irace_run, config_id, min_improvement, top_percent, time_limit = sys.argv[1:]
with open(path, newline="") as handle:
    reader = csv.DictReader(handle)
    rows = list(reader)
    original = list(reader.fieldnames or [])

front = [
    "irace_run",
    "config_id",
    "min_improvement",
    "top_percent",
    "time_limit_s",
]
hidden = {"Min Improvement", "Top Percent", "Time Limit (s)"}
fields = front + [name for name in original if name not in hidden]
for row in rows:
    row["irace_run"] = irace_run
    row["config_id"] = config_id
    row["min_improvement"] = min_improvement
    row["top_percent"] = top_percent
    row["time_limit_s"] = time_limit

with open(path, "w", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(rows)
print(f"Saved {len(rows)} instance(s) to {path}")
PY

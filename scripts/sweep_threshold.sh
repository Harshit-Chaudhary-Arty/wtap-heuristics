#!/usr/bin/env bash
# Sweep min_improvement for algorithm 4.2 with cycle size fixed at 2.
# Progress goes to stderr. The short ranking goes to stdout for OpenCode.
set -euo pipefail

cd "$(dirname "$0")/.."

BIN=./build/4_2_circular_threshold_local_search
OUT=results/experiments/circular-threshold-sweep
K=2
THRESHOLDS=(0.1 0.08 0.05 0.01 0.001)

mkdir -p "$OUT"
rm -f "$OUT"/k${K}_t*.log
make -s "$BIN"

for t in "${THRESHOLDS[@]}"; do
  echo "running k=${K} min_improvement=${t}" >&2
  # /dev/null: sweep runs must not overwrite the main 4.2 results CSV.
  WTAP_RESULTS_CSV=/dev/null "$BIN" "$K" "$t" > "${OUT}/k${K}_t${t}.log"
done

python3 - "$OUT" "$K" << 'PY'
import glob, os, sys

out_dir, k = sys.argv[1], sys.argv[2]
rows = []
for path in sorted(glob.glob(os.path.join(out_dir, f"k{k}_t*.log"))):
    name = os.path.basename(path)[len(f"k{k}_t"):-len(".log")]
    threshold = float(name)
    for line in open(path):
        parts = line.split()
        if len(parts) == 5 and parts[0].isdigit():
            inst, weapons, targets, objective, seconds = parts
            rows.append((threshold, int(inst), int(weapons), int(targets),
                         float(objective), float(seconds)))
        elif parts and parts[0] == "TOTAL":
            rows.append((threshold, 0, 0, 0, float("nan"), float(parts[-1])))

best = {}
for threshold, inst, weapons, targets, objective, seconds in rows:
    if inst:
        best[inst] = objective if inst not in best else min(best[inst], objective)

lines = ["threshold,instance,weapons,targets,objective,time_s,rel_gap"]
gaps, times = {}, {}
for threshold, inst, weapons, targets, objective, seconds in rows:
    if inst == 0:
        times[threshold] = seconds
        continue
    gap = (objective - best[inst]) / best[inst] if best[inst] else 0.0
    gaps.setdefault(threshold, []).append(gap)
    lines.append(
        f"{threshold},{inst},{weapons},{targets},{objective:.6f},{seconds:.6f},{gap:.8f}"
    )

csv_path = os.path.join(out_dir, "threshold-sweep.csv")
with open(csv_path, "w") as handle:
    handle.write("\n".join(lines) + "\n")

objectives = {}
for threshold, inst, weapons, targets, objective, seconds in rows:
    if inst:
        objectives.setdefault(threshold, {})[inst] = objective

instances = sorted({inst for inst in best})
thresholds = sorted(objectives)
wins = {
    threshold: sum(
        1 for inst in instances if objectives[threshold][inst] <= best[inst] + 1e-9
    )
    for threshold in thresholds
}
best_threshold = min(
    thresholds,
    key=lambda threshold: (
        -wins[threshold],
        sum(objectives[threshold][inst] for inst in instances),
    ),
)

def cell(threshold, inst):
    value = objectives[threshold][inst]
    text = f"{value:.4f}"
    if value <= best[inst] + 1e-9:
        text += "*"
    return text

headers = ["Instance"] + [f"t={threshold:g}" for threshold in thresholds]
table = [headers]
md_rows = []
for inst in instances:
    row = [str(inst)] + [cell(threshold, inst) for threshold in thresholds]
    table.append(row)
    md_rows.append(
        [str(inst)]
        + [
            f"**{objectives[threshold][inst]:.4f}**"
            if objectives[threshold][inst] <= best[inst] + 1e-9
            else f"{objectives[threshold][inst]:.4f}"
            for threshold in thresholds
        ]
    )
count_row = ["Best count"] + [str(wins[threshold]) for threshold in thresholds]
table.append(count_row)

widths = [max(len(table[r][c]) for r in range(len(table))) for c in range(len(headers))]

def format_row(row):
    return "  ".join(cell_text.rjust(widths[i]) for i, cell_text in enumerate(row))

print("Lower objective is better. A * marks the best value in that row.")
print()
print(format_row(table[0]))
print("  ".join("-" * widths[i] for i in range(len(headers))))
for row in table[1:]:
    print(format_row(row))
print()
print(f"Chosen min_improvement: {best_threshold:g}")
print(f"It has the lowest objective on {wins[best_threshold]} of {len(instances)} instances.")

md_headers = headers + ["Best"]
md_lines = [
    "# Threshold comparison",
    "",
    "Algorithm 4.2, cycle size 2. Lower objective is better.",
    "Each column is one `min_improvement`. **Bold** is the best objective on that instance.",
    "",
    "| " + " | ".join(md_headers) + " |",
    "| " + " | ".join("---" for _ in md_headers) + " |",
]
for inst, row in zip(instances, md_rows):
    winners = [
        f"{threshold:g}"
        for threshold in thresholds
        if objectives[threshold][inst] <= best[inst] + 1e-9
    ]
    md_lines.append("| " + " | ".join(row + [", ".join(winners)]) + " |")
md_lines.extend([
    "| Best count | " + " | ".join(str(wins[threshold]) for threshold in thresholds) + " | |",
    "",
    f"Chosen min_improvement: **{best_threshold:g}**.",
    f"It has the lowest objective on {wins[best_threshold]} of {len(instances)} instances.",
    "",
])
with open("docs/threshold_comparison.md", "w") as handle:
    handle.write("\n".join(md_lines))
PY

#!/usr/bin/env bash
# Export one irace run to its own CSV.
# Files are named (budget)-config-1.csv, (budget)-config-2.csv, ...
# A run already exported (same irace.Rdata) is not written again.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
EXEC_DIR="$ROOT/results/parameter-control/irace"
RDATA="$EXEC_DIR/irace.Rdata"
STAMP="$EXEC_DIR/.export-stamp"
MAMBA_ENV="${MAMBA_IRACE_ENV:-$HOME/micromamba/envs/irace}"
MAMBA_BIN="${MAMBA_BIN:-$HOME/micromamba/bin/micromamba}"

if [[ ! -f "$RDATA" ]]; then
  echo "error: missing $RDATA (run bash tools/irace/start.sh first)" >&2
  exit 1
fi

TIME_LIMIT="$(awk -F= '/^TIME_LIMIT=/ { print $2; exit }' "$DIR/target-runner")"
TIME_LIMIT="${TIME_LIMIT:-16}"

sig="$(stat -c '%Y %s' "$RDATA")"
if [[ -f "$STAMP" ]]; then
  prev_sig="$(sed -n '1p' "$STAMP")"
  prev_out="$(sed -n '2p' "$STAMP")"
  if [[ "$prev_sig" == "$sig" && -n "$prev_out" && -f "$prev_out" ]]; then
    echo "This irace log is already saved as $prev_out"
    exit 0
  fi
fi

next=1
for f in "$EXEC_DIR"/*.csv; do
  [[ -e "$f" ]] || continue
  base="$(basename "$f" .csv)"
  n=""
  if [[ "$base" =~ ^\(([0-9]+)\)-config-([0-9]+)$ ]]; then
    n="${BASH_REMATCH[2]}"
  elif [[ "$base" =~ ^best-configuration-([0-9]+) ]]; then
    n="${BASH_REMATCH[1]}"
  fi
  if [[ "$n" =~ ^[0-9]+$ ]] && (( n >= next )); then
    next=$((n + 1))
  fi
done
OUT="$EXEC_DIR/best-configuration-${next}.csv"

run_rscript() {
  if [[ -x "$MAMBA_BIN" && -d "$MAMBA_ENV" ]]; then
    "$MAMBA_BIN" run -p "$MAMBA_ENV" Rscript "$@"
  else
    Rscript "$@"
  fi
}

run_rscript - "$RDATA" "$OUT" "$next" "$TIME_LIMIT" <<'RS'
args <- commandArgs(trailingOnly = TRUE)
rdata <- args[[1]]
out <- args[[2]]
run_number <- as.integer(args[[3]])
time_limit <- as.numeric(args[[4]])

suppressPackageStartupMessages(library(irace))
load(rdata)
if (!exists("iraceResults")) {
  stop("iraceResults object not found in Rdata")
}

scenario <- iraceResults$scenario
state <- iraceResults$state
elites <- NULL
invisible(capture.output(elites <<- getFinalElites(iraceResults, n = 10)))
if (is.null(elites) || nrow(elites) == 0) {
  stop("irace finished with no elite configurations")
}

experiments <- iraceResults$experiments
col_names <- colnames(experiments)
elite_ids <- as.character(elites[[".ID."]])

mean_cost <- numeric(nrow(elites))
instances_scored <- integer(nrow(elites))
for (i in seq_len(nrow(elites))) {
  col <- match(elite_ids[[i]], col_names)
  if (is.na(col)) {
    mean_cost[[i]] <- NA_real_
    instances_scored[[i]] <- 0L
  } else {
    values <- experiments[, col]
    instances_scored[[i]] <- sum(!is.na(values))
    mean_cost[[i]] <- mean(values, na.rm = TRUE)
  }
}

domains <- scenario$parameters$domains
range_of <- function(name) {
  bounds <- domains[[name]]
  if (is.null(bounds) || length(bounds) < 2) return("")
  paste0(bounds[[1]], "-", bounds[[2]])
}

instance_paths <- scenario$instances
sizes <- vapply(instance_paths, function(path) {
  line <- readLines(path, n = 1, warn = FALSE)
  match <- regexec("n=([0-9]+), m=([0-9]+)", line)
  parts <- regmatches(line, match)[[1]]
  if (length(parts) == 3) paste0(parts[[2]], "x", parts[[3]]) else basename(path)
}, character(1))

iteration_elites <- iraceResults$iterationElites
iterations <- length(iteration_elites)
winner_id <- elite_ids[[1]]
winner_since <- match(winner_id, as.character(iteration_elites))
if (is.na(winner_since)) winner_since <- iterations

elapsed <- state$elapsed
elapsed_s <- if (length(elapsed)) round(as.numeric(tail(elapsed, 1)), 1) else NA_real_
finished_at <- format(file.info(rdata)$mtime, "%Y-%m-%d %H:%M")
stopped <- if (is.null(state$completed)) "" else as.character(state$completed)

table <- data.frame(
  rank = seq_len(nrow(elites)),
  min_improvement = elites[["min_improvement"]],
  top_percent = elites[["top_percent"]],
  mean_cost = round(mean_cost, 4),
  config_id = elites[[".ID."]],
  instances_scored = instances_scored,
  parent_id = elites[[".PARENT."]],
  run = run_number,
  finished_at = finished_at,
  iterations = iterations,
  winner_since_iteration = winner_since,
  budget = scenario$maxExperiments,
  experiments_used = sum(!is.na(experiments)),
  seed = scenario$seed,
  time_limit_s = time_limit,
  elapsed_s = elapsed_s,
  n_instances = length(instance_paths),
  instance_sizes = paste(sizes, collapse = " "),
  min_improvement_range = range_of("min_improvement"),
  top_percent_range = range_of("top_percent"),
  stopped = stopped,
  stringsAsFactors = FALSE
)

write.csv(table, out, row.names = FALSE)
cat("Wrote", out, "with", nrow(table), "elite configuration(s)\n")
RS

budget="$(python3 - "$OUT" <<'PY'
import csv, sys
with open(sys.argv[1], newline="") as handle:
    row = next(csv.DictReader(handle))
print(row["budget"])
PY
)"
FINAL="$EXEC_DIR/(${budget})-config-${next}.csv"
if [[ "$OUT" != "$FINAL" ]]; then
  mv "$OUT" "$FINAL"
fi

printf '%s\n%s\n' "$sig" "$FINAL" > "$STAMP"

# The unnumbered file was the previous single overwrite target.
if [[ -f "$EXEC_DIR/best-configs.csv" ]]; then
  rm -f "$EXEC_DIR/best-configs.csv"
fi

echo "Saved irace run $next to $FINAL"

/** Human-readable labels for on-disk result folder/file names. */

const LABELS: Record<string, string> = {
  algorithms: "Algorithms",
  experiments: "Experiments",
  "parameter-control": "Parameter control",
  irace: "irace",
  "circular-threshold-sweep": "Circular threshold sweep",
  "seeded-string-ga": "Seeded string GA",

  "1_fixed_order_sequential_greedy": "Fixed-order sequential greedy",
  "2_global_selection_assignment_greedy": "Global selection greedy",
  "3_greedy_local_search": "MMR + local search",
  "3_1_greedy_local_search": "MMR + fast local search",
  "4_mmr_phased_local_search": "MMR + phased LS",
  "4_1_circular_local_search": "MMR + circular LS",
  "4_2_circular_threshold_local_search": "MMR + circular threshold LS",
  "4_2_threshold_sweep": "Circular threshold sweep",
  "4_3_instrumented_circular_local_search": "MMR + instrumented circular LS",
  "4_4_top50": "MMR — Top 50% (3-cycles)",
  "4_4_Final_top50": "MMR — Top 50% (3-cycles)",
  "5_mmr_forbidden_rebuild": "MMR forbidden rebuild",
  "6_4_1_seeded_string_ga": "Seeded string GA",
  parameter_control: "Parameter control",
  parameter_control_irace: "Parameter control — irace",

  "fixed-order-sequential-greedy": "Fixed-order sequential greedy",
  "global-selection-greedy": "Global selection greedy",
  "greedy-local-search": "MMR + local search",
  "mmr-fast-local-search": "MMR + fast local search",
  "mmr-phased-local-search": "MMR + phased LS",
  "mmr-circular-threshold": "MMR + circular threshold LS",
  "mmr-top50-3-cycles": "MMR — Top 50% (3-cycles)",
  "mmr-forbidden-rebuild": "MMR forbidden rebuild",
  "threshold-sweep": "Threshold sweep",
  threshold_sweep: "Threshold sweep",
  top50_results: "MMR — Top 50% (3-cycles)",
  parameter_control_results: "Parameter control",
  best_configs: "Best configurations",
  "best-configs": "Best configurations",
  results: "Results",
};

function stem(name: string): string {
  return name.replace(/\.csv$/i, "");
}

export function displayName(name: string): string {
  const key = stem(name);
  if (LABELS[key]) return LABELS[key];
  if (LABELS[name]) return LABELS[name];
  return key
    .replace(/[_-]+/g, " ")
    .replace(/\s+/g, " ")
    .trim()
    .replace(/\b\w/g, (c) => c.toUpperCase());
}

export function searchText(name: string, title?: string): string {
  return `${name} ${displayName(name)} ${title ?? ""}`.toLowerCase();
}

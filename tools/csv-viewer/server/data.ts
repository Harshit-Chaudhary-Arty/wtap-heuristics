// Read-only data access shared by the local API server and the static export.
import fs from "node:fs";
import fsp from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import Papa from "papaparse";

export const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const DATA_ROOT = path.resolve(__dirname, "../../../results");

export type TreeNode = {
  name: string;
  path: string;
  type: "folder" | "file";
  title?: string;
  children?: TreeNode[];
};

// Display-only titles shown in the sidebar. The real file/folder names never change.
export const TITLES_FILE = path.join(DATA_ROOT, ".titles.json");

export function readTitles(): Record<string, string> {
  try {
    const parsed = JSON.parse(fs.readFileSync(TITLES_FILE, "utf8"));
    return parsed && typeof parsed === "object" ? parsed : {};
  } catch {
    return {};
  }
}

export function writeTitles(titles: Record<string, string>): void {
  ensureDataRoot();
  const tmp = `${TITLES_FILE}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(titles, null, 2) + "\n");
  fs.renameSync(tmp, TITLES_FILE);
}

// Custom sibling order, stored as { "<parent path>": ["name", ...] }.
export const ORDER_FILE = path.join(DATA_ROOT, ".order.json");

export function readOrder(): Record<string, string[]> {
  try {
    const parsed = JSON.parse(fs.readFileSync(ORDER_FILE, "utf8"));
    return parsed && typeof parsed === "object" ? parsed : {};
  } catch {
    return {};
  }
}

export function writeOrder(order: Record<string, string[]>): void {
  ensureDataRoot();
  const tmp = `${ORDER_FILE}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(order, null, 2) + "\n");
  fs.renameSync(tmp, ORDER_FILE);
}

export function parentOf(rel: string): string {
  const i = rel.lastIndexOf("/");
  return i < 0 ? "" : rel.slice(0, i);
}

export function renameInOrder(rel: string, newName: string): void {
  const order = readOrder();
  const list = order[parentOf(rel)];
  if (!list) return;
  const i = list.indexOf(rel.slice(rel.lastIndexOf("/") + 1));
  if (i < 0) return;
  list[i] = newName;
  writeOrder(order);
}

export function dropFromOrder(rel: string): void {
  const order = readOrder();
  let changed = false;
  const list = order[parentOf(rel)];
  const name = rel.slice(rel.lastIndexOf("/") + 1);
  if (list && list.includes(name)) {
    order[parentOf(rel)] = list.filter((n) => n !== name);
    changed = true;
  }
  for (const key of Object.keys(order)) {
    if (key === rel || key.startsWith(`${rel}/`)) {
      delete order[key];
      changed = true;
    }
  }
  if (changed) writeOrder(order);
}

/** Run number at the end of `(budget)-config-N.csv` / `(budget)-applied-N.csv`. */
export function iraceRunNumber(name: string): number | null {
  const match = name.match(/-(?:config|applied)-(\d+)\.csv$/i);
  return match ? Number(match[1]) : null;
}

export function sortByOrder<T extends { name: string }>(
  items: T[],
  saved: string[] | undefined,
): T[] {
  const rank = new Map((saved ?? []).map((name, i) => [name, i]));
  return [...items].sort((a, b) => {
    const ra = rank.get(a.name);
    const rb = rank.get(b.name);
    if (ra !== undefined && rb !== undefined) return ra - rb;
    if (ra !== undefined) return -1;
    if (rb !== undefined) return 1;
    const na = iraceRunNumber(a.name);
    const nb = iraceRunNumber(b.name);
    if (na !== null && nb !== null && na !== nb) return na - nb;
    return a.name.localeCompare(b.name, undefined, { numeric: true });
  });
}

export function moveTitle(from: string, to: string): void {
  const titles = readTitles();
  if (titles[from] === undefined) return;
  titles[to] = titles[from];
  delete titles[from];
  writeTitles(titles);
}

export function dropTitles(rel: string): void {
  const titles = readTitles();
  let changed = false;
  for (const key of Object.keys(titles)) {
    if (key === rel || key.startsWith(`${rel}/`)) {
      delete titles[key];
      changed = true;
    }
  }
  if (changed) writeTitles(titles);
}

export function ensureDataRoot(): void {
  if (!fs.existsSync(DATA_ROOT)) {
    fs.mkdirSync(DATA_ROOT, { recursive: true });
  }
}

export function isSafeRelative(rel: string): boolean {
  if (!rel || rel.includes("\0")) return false;
  const normalized = path.posix.normalize(rel.replace(/\\/g, "/"));
  if (normalized.startsWith("..") || path.isAbsolute(normalized)) return false;
  if (normalized.includes("/../") || normalized === "..") return false;
  return true;
}

export function resolveInsideData(relPath: string): string | null {
  const cleaned = relPath.replace(/^\/+/, "").replace(/\\/g, "/");
  if (!isSafeRelative(cleaned) && cleaned !== ".") return null;
  const absolute = path.resolve(DATA_ROOT, cleaned === "." ? "." : cleaned);
  const rootWithSep = DATA_ROOT.endsWith(path.sep)
    ? DATA_ROOT
    : DATA_ROOT + path.sep;
  if (absolute !== DATA_ROOT && !absolute.startsWith(rootWithSep)) {
    return null;
  }
  return absolute;
}

export function toRelative(absolute: string): string {
  return path.relative(DATA_ROOT, absolute).split(path.sep).join("/");
}

export async function buildTree(
  dirAbsolute: string,
  titles: Record<string, string>,
  order: Record<string, string[]>,
): Promise<TreeNode[]> {
  const entries = await fsp.readdir(dirAbsolute, { withFileTypes: true });
  const nodes: TreeNode[] = [];

  const saved = order[toRelative(dirAbsolute)];
  const folders = sortByOrder(
    entries.filter((e) => e.isDirectory() && !e.name.startsWith(".")),
    saved,
  );
  const files = sortByOrder(
    entries.filter(
      (e) => e.isFile() && !e.name.startsWith(".") && e.name.toLowerCase().endsWith(".csv"),
    ),
    saved,
  );

  for (const folder of folders) {
    const absolute = path.join(dirAbsolute, folder.name);
    const rel = toRelative(absolute);
    nodes.push({
      name: folder.name,
      path: rel,
      type: "folder",
      ...(titles[rel] ? { title: titles[rel] } : {}),
      children: await buildTree(absolute, titles, order),
    });
  }

  for (const file of files) {
    const absolute = path.join(dirAbsolute, file.name);
    const rel = toRelative(absolute);
    nodes.push({
      name: file.name,
      path: rel,
      type: "file",
      ...(titles[rel] ? { title: titles[rel] } : {}),
    });
  }

  return nodes;
}

export const ALGO_LABELS: Record<string, string> = {
  "1_fixed_order_sequential_greedy": "Fixed-order sequential greedy",
  "2_global_selection_assignment_greedy": "Global selection greedy",
  "3_greedy_local_search": "MMR + local search",
  "3_1_greedy_local_search": "MMR + fast local search",
  "4_mmr_phased_local_search": "MMR + phased LS",
  "4_1_circular_local_search": "MMR + circular LS",
  "4_2_circular_threshold_local_search": "MMR + circular threshold LS",
  "4_3_instrumented_circular_local_search": "MMR + instrumented circular LS",
  "4_4_top50": "MMR — Top 50% (3-cycles)",
  "4_4_Final_top50": "MMR — Top 50% (3-cycles)",
  "5_mmr_forbidden_rebuild": "MMR forbidden rebuild",
  "6_4_1_seeded_string_ga": "Seeded string GA",
  parameter_control: "Parameter control",
  "fixed-order-sequential-greedy": "Fixed-order sequential greedy",
  "global-selection-greedy": "Global selection greedy",
  "greedy-local-search": "MMR + local search",
  "mmr-fast-local-search": "MMR + fast local search",
  "mmr-phased-local-search": "MMR + phased LS",
  "mmr-circular-threshold": "MMR + circular threshold LS",
  "mmr-top50-3-cycles": "MMR — Top 50% (3-cycles)",
  "mmr-forbidden-rebuild": "MMR forbidden rebuild",
  "parameter-control": "Parameter control",
  top50_results: "MMR — Top 50% (3-cycles)",
  parameter_control_results: "Parameter control",
};

export type CompareAlgo = {
  id: string;
  label: string;
  path: string;
};

export type CompareCell = {
  objective: number | null;
  time: number | null;
};

export type CompareRow = {
  instance: number;
  weapons: number | null;
  targets: number | null;
  values: Record<string, CompareCell>;
};

export function findColumn(fields: string[], candidates: string[]): string | null {
  const lower = new Map(fields.map((f) => [f.toLowerCase().trim(), f]));
  for (const candidate of candidates) {
    const hit = lower.get(candidate.toLowerCase());
    if (hit) return hit;
  }
  return null;
}

export function parseNumber(raw: string | undefined): number | null {
  if (raw === undefined || raw === null) return null;
  const text = String(raw).trim();
  if (!text || text === "TOTAL") return null;
  const value = Number(text);
  return Number.isFinite(value) ? value : null;
}

export function algoIdFromPath(relPath: string): string {
  return relPath.replace(/\.csv$/i, "").split(path.sep).join("__");
}

export function algoLabel(relPath: string): string {
  const stem = path.basename(relPath, ".csv");
  if (ALGO_LABELS[stem]) return ALGO_LABELS[stem];
  const parent = path.basename(path.dirname(relPath));
  if (ALGO_LABELS[parent]) return ALGO_LABELS[parent];
  return stem.replace(/[_-]+/g, " ").replace(/\b\w/g, (c) => c.toUpperCase());
}

export async function listCsvFiles(dirAbsolute: string): Promise<string[]> {
  const entries = await fsp.readdir(dirAbsolute, { withFileTypes: true });
  const files: string[] = [];
  for (const entry of entries) {
    if (entry.name.startsWith(".")) continue;
    const absolute = path.join(dirAbsolute, entry.name);
    if (entry.isDirectory()) {
      files.push(...(await listCsvFiles(absolute)));
    } else if (entry.isFile() && entry.name.toLowerCase().endsWith(".csv")) {
      files.push(absolute);
    }
  }
  return files;
}

export async function buildComparison(): Promise<{
  algorithms: CompareAlgo[];
  rows: CompareRow[];
}> {
  const csvFiles = await listCsvFiles(DATA_ROOT);
  const titles = readTitles();
  const algorithms: CompareAlgo[] = [];
  const byInstance = new Map<number, CompareRow>();

  // Skip non-instance result tables (sweeps, tuners, histories).
  const skipName = /(threshold[_-]sweep|best[_-]configs|history)/i;

  for (const absolute of csvFiles.sort()) {
    const rel = toRelative(absolute);
    if (skipName.test(path.basename(rel))) continue;

    const text = await fsp.readFile(absolute, "utf8");
    const parsed = Papa.parse<Record<string, string>>(text, {
      header: true,
      skipEmptyLines: true,
      dynamicTyping: false,
    });
    const fields = parsed.meta.fields ?? Object.keys(parsed.data[0] ?? {});
    if (fields.length === 0) continue;

    const instanceCol = findColumn(fields, ["Instance", "instance", "Inst"]);
    const objectiveCol = findColumn(fields, [
      "Final Objective",
      "Objective",
      "objective",
      "FinalObjective",
    ]);
    const timeCol = findColumn(fields, [
      "Runtime (seconds)",
      "Time_s",
      "Time (s)",
      "time_s",
      "Runtime",
      "Time",
    ]);
    const weaponsCol = findColumn(fields, ["Weapons", "weapons"]);
    const targetsCol = findColumn(fields, ["Targets", "targets"]);

    if (!instanceCol || !objectiveCol) continue;

    const id = algoIdFromPath(rel);
    if (algorithms.some((a) => a.id === id)) continue;

    algorithms.push({ id, label: titles[rel] || algoLabel(rel), path: rel });

    for (const row of parsed.data) {
      const instance = parseNumber(row[instanceCol]);
      if (instance === null || instance <= 0) continue;
      const objective = parseNumber(row[objectiveCol]);
      const time = timeCol ? parseNumber(row[timeCol]) : null;
      const weapons = weaponsCol ? parseNumber(row[weaponsCol]) : null;
      const targets = targetsCol ? parseNumber(row[targetsCol]) : null;

      let entry = byInstance.get(instance);
      if (!entry) {
        entry = {
          instance,
          weapons,
          targets,
          values: {},
        };
        byInstance.set(instance, entry);
      } else {
        if (entry.weapons === null && weapons !== null) entry.weapons = weapons;
        if (entry.targets === null && targets !== null) entry.targets = targets;
      }
      entry.values[id] = { objective, time };
    }
  }

  // Follow the sidebar order (depth-first through the tree, same as it is shown).
  const position = new Map<string, number>();
  const walk = (nodes: TreeNode[]): void => {
    for (const node of nodes) {
      if (node.type === "file") position.set(node.path, position.size);
      else walk(node.children ?? []);
    }
  };
  walk(await buildTree(DATA_ROOT, titles, readOrder()));
  algorithms.sort(
    (a, b) =>
      (position.get(a.path) ?? Number.MAX_SAFE_INTEGER) -
      (position.get(b.path) ?? Number.MAX_SAFE_INTEGER),
  );
  const rows = [...byInstance.values()].sort((a, b) => a.instance - b.instance);
  return { algorithms, rows };
}

export type CsvPayload = {
  path: string;
  name: string;
  columns: string[];
  rows: Record<string, string>[];
  rowCount: number;
  columnCount: number;
};

/** Parse one CSV under the data root. Throws on unreadable / unparsable files. */
export async function readCsvPayload(absolute: string): Promise<CsvPayload> {
  const text = await fsp.readFile(absolute, "utf8");
  const parsed = Papa.parse<Record<string, string>>(text, {
    header: true,
    skipEmptyLines: true,
    dynamicTyping: false,
  });

  if (parsed.errors.length > 0 && (!parsed.data || parsed.data.length === 0)) {
    throw new Error(parsed.errors[0]?.message ?? "Failed to parse CSV");
  }

  const columns =
    parsed.meta.fields && parsed.meta.fields.length > 0
      ? parsed.meta.fields
      : Object.keys(parsed.data[0] ?? {});

  return {
    path: toRelative(absolute),
    name: path.basename(absolute),
    columns,
    rows: parsed.data,
    rowCount: parsed.data.length,
    columnCount: columns.length,
  };
}

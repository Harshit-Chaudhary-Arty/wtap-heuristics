/**
 * Shareable deep links for the results viewer.
 *
 *   ?f=paper-instances/toffolo-k3-50.csv
 *   ?view=compare
 *   ?view=compare&a=algorithms/foo.csv&a=algorithms/bar.csv
 *
 * Works on GitHub Pages (no server rewrite needed) and in local dev.
 */

export type Route =
  | { kind: "file"; path: string }
  | {
      kind: "compare";
      /** Relative CSV paths. `null` = select all; `[]` = none. */
      algos: string[] | null;
    }
  | { kind: "home" };

export function parseRoute(search: string = window.location.search): Route {
  const params = new URLSearchParams(search);
  const view = params.get("view");
  if (view === "compare") {
    return { kind: "compare", algos: parseCompareAlgos(params) };
  }
  const file = params.get("f");
  if (file && file.trim()) {
    return { kind: "file", path: normalizePath(file) };
  }
  return { kind: "home" };
}

/** `null` = no `a` params (select all). Otherwise the listed CSV paths. */
export function parseCompareAlgos(
  params: URLSearchParams = new URLSearchParams(window.location.search),
): string[] | null {
  if (!params.has("a")) return null;
  return params
    .getAll("a")
    .map((value) => normalizePath(value.trim()))
    .filter(Boolean);
}

export function normalizePath(path: string): string {
  return path.replace(/^\/+/, "").replace(/\\/g, "/");
}

export function fileShareUrl(path: string): string {
  const url = new URL(window.location.href);
  url.search = "";
  url.hash = "";
  url.searchParams.set("f", normalizePath(path));
  return url.toString();
}

/**
 * @param algos relative CSV paths to include. Pass `null` for "all algorithms".
 *   Pass `[]` for none selected.
 * @param allPaths when provided and `algos` lists every path, the URL omits `a`
 *   (shorter "compare all" link).
 */
export function compareShareUrl(
  algos: string[] | null = null,
  allPaths?: string[],
): string {
  const url = new URL(window.location.href);
  url.search = "";
  url.hash = "";
  url.searchParams.set("view", "compare");
  applyCompareAlgos(url.searchParams, algos, allPaths);
  return url.toString();
}

function applyCompareAlgos(
  params: URLSearchParams,
  algos: string[] | null,
  allPaths?: string[],
): void {
  params.delete("a");
  if (algos === null) return;

  const normalized = algos.map(normalizePath).filter(Boolean);
  if (normalized.length === 0) {
    // Explicit empty selection (distinct from "all").
    params.set("a", "");
    return;
  }

  if (allPaths && allPaths.length > 0) {
    const allSet = new Set(allPaths.map(normalizePath));
    const sameSize = normalized.length === allSet.size;
    const coversAll = sameSize && normalized.every((p) => allSet.has(p));
    if (coversAll) return;
  }

  for (const path of normalized) {
    params.append("a", path);
  }
}

/** Update the address bar without reloading. */
export function writeRoute(route: Route, mode: "push" | "replace" = "push"): void {
  const url = new URL(window.location.href);
  url.search = "";
  url.hash = "";

  if (route.kind === "file") {
    url.searchParams.set("f", normalizePath(route.path));
  } else if (route.kind === "compare") {
    url.searchParams.set("view", "compare");
    applyCompareAlgos(url.searchParams, route.algos);
  }

  const next = `${url.pathname}${url.search}${url.hash}`;
  const current = `${window.location.pathname}${window.location.search}${window.location.hash}`;
  if (next === current) return;

  if (mode === "replace") {
    window.history.replaceState(null, "", next);
  } else {
    window.history.pushState(null, "", next);
  }
}

/** Ancestor folder paths for expanding the sidebar (excludes the file itself). */
export function ancestorFolders(path: string): string[] {
  const parts = normalizePath(path).split("/").filter(Boolean);
  if (parts.length <= 1) return [""];
  const folders: string[] = [""];
  for (let i = 0; i < parts.length - 1; i++) {
    folders.push(parts.slice(0, i + 1).join("/"));
  }
  return folders;
}

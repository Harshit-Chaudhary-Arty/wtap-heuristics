import {
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
} from "react";
import { fetchCompare } from "../api";
import {
  formatObjective,
  formatTime,
  objectiveHeatColor,
} from "../lib/heatmap";
import type { CompareAlgo, ComparePayload, CompareRow } from "../types";

type SortKey =
  | "instance"
  | "weapons"
  | "targets"
  | `obj:${string}`
  | `time:${string}`;

type SortState = {
  key: SortKey;
  dir: "asc" | "desc";
};

function sortMark(active: boolean, dir: "asc" | "desc"): string {
  if (!active) return "";
  return dir === "asc" ? " ↑" : " ↓";
}

function compareNullable(
  a: number | null,
  b: number | null,
  dir: "asc" | "desc",
): number {
  const aMissing = a === null || !Number.isFinite(a);
  const bMissing = b === null || !Number.isFinite(b);
  if (aMissing && bMissing) return 0;
  if (aMissing) return 1;
  if (bMissing) return -1;
  const diff = (a as number) - (b as number);
  return dir === "asc" ? diff : -diff;
}

function rowValue(row: CompareRow, key: SortKey): number | null {
  if (key === "instance") return row.instance;
  if (key === "weapons") return row.weapons;
  if (key === "targets") return row.targets;
  if (key.startsWith("obj:")) {
    return row.values[key.slice(4)]?.objective ?? null;
  }
  if (key.startsWith("time:")) {
    return row.values[key.slice(5)]?.time ?? null;
  }
  return null;
}

export function CompareView() {
  const [data, setData] = useState<ComparePayload | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [filter, setFilter] = useState("");
  const [sort, setSort] = useState<SortState>({
    key: "instance",
    dir: "asc",
  });
  const [menuOpen, setMenuOpen] = useState(false);
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);
    void fetchCompare()
      .then((payload) => {
        if (cancelled) return;
        setData(payload);
        setSelected(new Set(payload.algorithms.map((a) => a.id)));
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setError(
            err instanceof Error ? err.message : "Failed to load comparison",
          );
        }
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    if (!menuOpen) return;
    function onPointerDown(event: MouseEvent) {
      if (!menuRef.current?.contains(event.target as Node)) {
        setMenuOpen(false);
      }
    }
    function onKey(event: KeyboardEvent) {
      if (event.key === "Escape") setMenuOpen(false);
    }
    document.addEventListener("mousedown", onPointerDown);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("mousedown", onPointerDown);
      document.removeEventListener("keydown", onKey);
    };
  }, [menuOpen]);

  const algorithms = useMemo(() => {
    if (!data) return [];
    return data.algorithms.filter((a) => selected.has(a.id));
  }, [data, selected]);

  const filteredRows = useMemo(() => {
    if (!data) return [];
    const q = filter.trim().toLowerCase();
    if (!q) return data.rows;
    return data.rows.filter((row) => {
      const hay = `${row.instance} ${row.weapons ?? ""} ${row.targets ?? ""}`;
      return hay.toLowerCase().includes(q);
    });
  }, [data, filter]);

  const rows = useMemo(() => {
    const next = [...filteredRows];
    next.sort((a, b) =>
      compareNullable(rowValue(a, sort.key), rowValue(b, sort.key), sort.dir),
    );
    return next;
  }, [filteredRows, sort]);

  const totals = useMemo(() => {
    const out: Record<string, number | null> = {};
    for (const algo of algorithms) {
      let sum = 0;
      let count = 0;
      for (const row of filteredRows) {
        const time = row.values[algo.id]?.time;
        if (time !== null && time !== undefined && Number.isFinite(time)) {
          sum += time;
          count += 1;
        }
      }
      out[algo.id] = count > 0 ? sum : null;
    }
    return out;
  }, [algorithms, filteredRows]);

  const fastestTotalId = useMemo(() => {
    let bestId: string | null = null;
    let best = Number.POSITIVE_INFINITY;
    for (const algo of algorithms) {
      const value = totals[algo.id];
      if (value !== null && value !== undefined && value < best) {
        best = value;
        bestId = algo.id;
      }
    }
    return bestId;
  }, [algorithms, totals]);

  function toggleSort(key: SortKey) {
    setSort((prev) => {
      if (prev.key === key) {
        return { key, dir: prev.dir === "asc" ? "desc" : "asc" };
      }
      const defaultDir =
        key === "instance" || key === "weapons" || key === "targets"
          ? "asc"
          : "asc";
      return { key, dir: defaultDir };
    });
  }

  function toggleAlgo(id: string) {
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  }

  function selectAllAlgos(list: CompareAlgo[]) {
    setSelected(new Set(list.map((a) => a.id)));
  }

  function clearAlgos() {
    setSelected(new Set());
  }

  if (loading) {
    return (
      <div className="empty-state">
        <p>Building comparison…</p>
      </div>
    );
  }

  if (error) {
    return <div className="banner error">{error}</div>;
  }

  if (!data || data.algorithms.length === 0) {
    return (
      <div className="empty-state">
        <h2>No comparable CSVs</h2>
        <p>
          Run solvers so each folder under <code>results/</code> has an Instance
          + Objective CSV.
        </p>
      </div>
    );
  }

  return (
    <div className="table-shell compare-shell">
      <div className="table-toolbar compare-toolbar">
        <div className="compare-toolbar-row">
          <div className="search-field compare-search">
            <span aria-hidden>⌕</span>
            <input
              type="search"
              placeholder="Search instances…"
              value={filter}
              onChange={(e) => setFilter(e.target.value)}
            />
          </div>

          <div className="compare-toolbar-actions">
            <div className="algo-menu" ref={menuRef}>
              <button
                type="button"
                className="btn btn-ghost algo-menu-trigger"
                aria-expanded={menuOpen}
                aria-haspopup="listbox"
                onClick={() => setMenuOpen((open) => !open)}
              >
                Algorithms ({selected.size})
                <span aria-hidden>{menuOpen ? "▴" : "▾"}</span>
              </button>
              {menuOpen && (
                <div className="algo-menu-panel" role="listbox">
                  <div className="algo-menu-actions">
                    <button
                      type="button"
                      className="btn btn-ghost"
                      onClick={() => selectAllAlgos(data.algorithms)}
                    >
                      Select all
                    </button>
                    <button
                      type="button"
                      className="btn btn-ghost"
                      onClick={clearAlgos}
                    >
                      Clear
                    </button>
                  </div>
                  <div className="algo-menu-list">
                    {data.algorithms.map((algo) => {
                      const on = selected.has(algo.id);
                      return (
                        <label key={algo.id} className="algo-menu-item" title={algo.path}>
                          <input
                            type="checkbox"
                            checked={on}
                            onChange={() => toggleAlgo(algo.id)}
                          />
                          <span>{algo.label}</span>
                        </label>
                      );
                    })}
                  </div>
                </div>
              )}
            </div>

            <button
              type="button"
              className="btn btn-ghost"
              disabled={!filter}
              onClick={() => setFilter("")}
            >
              Clear
            </button>

            <div className="heat-legend" aria-hidden>
              <span className="heat-swatch best" />
              better
              <span className="heat-swatch mid" />
              <span className="heat-swatch worst" />
              worse
            </div>
          </div>
        </div>
      </div>

      {algorithms.length === 0 ? (
        <div className="empty-state compare-empty">
          <p>Select at least one algorithm to compare.</p>
        </div>
      ) : (
        <div className="table-scroll">
          <table className="data-table compare-table">
            <thead>
              <tr>
                <th rowSpan={2} className="meta meta-0">
                  <button
                    type="button"
                    className="th-button"
                    onClick={() => toggleSort("instance")}
                  >
                    Instance
                    <span className="sort-mark">
                      {sortMark(sort.key === "instance", sort.dir)}
                    </span>
                  </button>
                </th>
                <th rowSpan={2} className="meta meta-1">
                  <button
                    type="button"
                    className="th-button"
                    onClick={() => toggleSort("weapons")}
                  >
                    Weapons
                    <span className="sort-mark">
                      {sortMark(sort.key === "weapons", sort.dir)}
                    </span>
                  </button>
                </th>
                <th rowSpan={2} className="meta meta-2">
                  <button
                    type="button"
                    className="th-button"
                    onClick={() => toggleSort("targets")}
                  >
                    Targets
                    <span className="sort-mark">
                      {sortMark(sort.key === "targets", sort.dir)}
                    </span>
                  </button>
                </th>
                {algorithms.map((algo) => (
                  <th
                    key={algo.id}
                    colSpan={2}
                    className="algo-group"
                    title={algo.label}
                  >
                    <div className="algo-group-name">{algo.label}</div>
                    <div
                      className={[
                        "algo-group-total",
                        fastestTotalId === algo.id ? "is-fastest" : "",
                      ].join(" ")}
                      title={
                        filter.trim()
                          ? "Total time across filtered instances"
                          : "Total time across all instances"
                      }
                    >
                      Total {formatTime(totals[algo.id] ?? null)} s
                    </div>
                  </th>
                ))}
              </tr>
              <tr className="compare-subhead-row">
                {algorithms.map((algo) => (
                  <th key={`${algo.id}-subs`} colSpan={2} className="compare-pair">
                    <div className="compare-pair-inner">
                      <button
                        type="button"
                        className="th-button sub"
                        onClick={() => toggleSort(`obj:${algo.id}`)}
                      >
                        Objective
                        <span className="sort-mark">
                          {sortMark(sort.key === `obj:${algo.id}`, sort.dir)}
                        </span>
                      </button>
                      <button
                        type="button"
                        className="th-button sub"
                        onClick={() => toggleSort(`time:${algo.id}`)}
                      >
                        Time
                        <span className="sort-mark">
                          {sortMark(sort.key === `time:${algo.id}`, sort.dir)}
                        </span>
                      </button>
                    </div>
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.length === 0 ? (
                <tr>
                  <td
                    colSpan={3 + algorithms.length * 2}
                    className="empty-cell"
                  >
                    No rows match this search.
                  </td>
                </tr>
              ) : (
                rows.map((row) => {
                  const objectives = algorithms
                    .map((algo) => row.values[algo.id]?.objective)
                    .filter(
                      (v): v is number =>
                        v !== null && v !== undefined && Number.isFinite(v),
                    );
                  const min = objectives.length ? Math.min(...objectives) : NaN;
                  const max = objectives.length ? Math.max(...objectives) : NaN;

                  return (
                    <tr key={row.instance}>
                      <td className="meta meta-0 is-numeric">{row.instance}</td>
                      <td className="meta meta-1 is-numeric">
                        {row.weapons ?? "—"}
                      </td>
                      <td className="meta meta-2 is-numeric">
                        {row.targets ?? "—"}
                      </td>
                      {algorithms.flatMap((algo) => {
                        const cell = row.values[algo.id];
                        const objective = cell?.objective ?? null;
                        const isBest =
                          objective !== null &&
                          Number.isFinite(min) &&
                          Math.abs(objective - min) < 1e-12;
                        const bg =
                          objective === null
                            ? undefined
                            : objectiveHeatColor(objective, min, max) ??
                              undefined;
                        const heatStyle = bg
                          ? ({ "--heat": bg } as CSSProperties)
                          : undefined;
                        return [
                          <td
                            key={`${algo.id}-obj`}
                            className={[
                              "is-numeric",
                              "heat-cell",
                              isBest ? "is-best" : "",
                            ].join(" ")}
                            style={heatStyle}
                          >
                            {formatObjective(objective)}
                          </td>,
                          <td
                            key={`${algo.id}-time`}
                            className="is-numeric time-cell"
                          >
                            {formatTime(cell?.time ?? null)}
                          </td>,
                        ];
                      })}
                    </tr>
                  );
                })
              )}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}

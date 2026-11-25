/** Subtle mint → white → rose. Lower objective = better. */

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}

function lerpColor(
  from: [number, number, number],
  to: [number, number, number],
  t: number,
): string {
  const r = Math.round(lerp(from[0], to[0], t));
  const g = Math.round(lerp(from[1], to[1], t));
  const b = Math.round(lerp(from[2], to[2], t));
  return `rgb(${r}, ${g}, ${b})`;
}

const BEST: [number, number, number] = [226, 242, 234];
const MID: [number, number, number] = [255, 255, 255];
const WORST: [number, number, number] = [254, 236, 236];

export function objectiveHeatColor(
  value: number,
  min: number,
  max: number,
): string | null {
  if (!Number.isFinite(value) || !Number.isFinite(min) || !Number.isFinite(max)) {
    return null;
  }
  if (max <= min) {
    return lerpColor(BEST, BEST, 0);
  }
  const t = Math.min(1, Math.max(0, (value - min) / (max - min)));
  if (t <= 0.5) {
    return lerpColor(BEST, MID, t * 2);
  }
  return lerpColor(MID, WORST, (t - 0.5) * 2);
}

export function formatObjective(value: number | null): string {
  if (value === null || !Number.isFinite(value)) return "—";
  return value.toFixed(4);
}

export function formatTime(value: number | null): string {
  if (value === null || !Number.isFinite(value)) return "—";
  if (value === 0) return "0";
  if (value < 0.0001) return value.toExponential(2);
  if (value < 1) {
    const text = value.toFixed(6);
    return text.replace(/\.?0+$/, "") || "0";
  }
  return value.toFixed(4);
}

const TIME_COLUMNS = ["Runtime (seconds)", "Time_s", "Time (s)", "time_s", "Runtime", "Time"];

/** Sum of the solver time column for a CSV, or null if it has none. Skips TOTAL rows. */
export function totalCsvTime(
  columns: string[],
  rows: Record<string, string>[],
): number | null {
  const lower = new Map(columns.map((c) => [c.trim().toLowerCase(), c]));
  const col = TIME_COLUMNS.map((c) => lower.get(c.toLowerCase())).find(Boolean);
  if (!col) return null;
  const first = columns[0];
  let sum = 0;
  let count = 0;
  for (const row of rows) {
    if (first && /^total$/i.test((row[first] ?? "").trim())) continue;
    const value = Number.parseFloat(row[col] ?? "");
    if (Number.isFinite(value)) {
      sum += value;
      count += 1;
    }
  }
  return count > 0 ? sum : null;
}

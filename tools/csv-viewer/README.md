# CSV Results Viewer

Browse and edit experiment CSVs under the repo `results/` tree.

## Layout

```
results/                  # shared Results root (repo)
  algorithms/             # one CSV per standalone solver
  parameter-control/      # param runs + irace/
  experiments/            # multi-file sweeps / GA
```

## Run

```bash
bash tools/csv-viewer/start.sh
# or: cd tools/csv-viewer && npm run dev
```

UI: http://127.0.0.1:5173  
API: http://127.0.0.1:8787

## Static (read-only) build for GitHub Pages

```bash
cd tools/csv-viewer && npm run build:static   # -> dist/ (site + dist/data/ snapshot of results/)
```

Same app, but with `VITE_STATIC=1`: data is read from JSON exported out of `results/`
at build time, and Upload / New folder / Rename / Delete / title edit / drag-reorder
are hidden. Local `start.sh` stays fully editable. Nothing is copied into the repo;
`dist/` is gitignored. `.github/workflows/pages.yml` runs this on every push to `main`
that touches `results/` or the viewer.

## Shareable links

Opening a CSV updates the URL. Copy the link button (chain icon) next to the
title, or share the address bar:

```
https://…/wtap-heuristics/?f=paper-instances/toffolo-k3-50.csv
https://…/wtap-heuristics/?view=compare
https://…/wtap-heuristics/?view=compare&a=algorithms/foo.csv&a=algorithms/bar.csv
```

In Compare, toggling algorithms updates `a=` in the URL. Copy the link button
shares the current selection. Omitting `a` means “all algorithms”.

Locally the same works: `http://127.0.0.1:5173/?f=algorithms/mmr-top50-3-cycles.csv`.

## Compare page

Sidebar → **Compare instances** merges every per-algorithm results CSV
(Instance / Objective / Time) into one sheet-style table. Objective cells are
heatmapped **per row** (green = better/lower, red = worse/higher). Toggle
algorithms with the chips above the table.

/**
 * `true` in the static build (GitHub Pages): data comes from pre-exported JSON and
 * every upload / edit / delete / reorder control is hidden.
 * `false` in local dev (`start.sh`): talks to the Node API and is fully editable.
 *
 * Set by `VITE_STATIC=1`, see the `build:static` script.
 */
export const READ_ONLY = import.meta.env.VITE_STATIC === "1";

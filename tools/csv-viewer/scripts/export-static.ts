// Pack ../../results into dist/data/ so the built viewer can run with no server.
//
//   dist/data/tree.json            sidebar tree (titles + order applied)
//   dist/data/compare.json         the Compare page payload
//   dist/data/csv/<path>.json      one parsed CSV per file in the tree
//
// Run by `npm run build:static` after `vite build`. Output only goes to dist/.
import fsp from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  DATA_ROOT,
  buildComparison,
  buildTree,
  ensureDataRoot,
  readCsvPayload,
  readOrder,
  readTitles,
  type TreeNode,
} from "../server/data.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.resolve(here, "../dist/data");

async function writeJson(rel: string, value: unknown): Promise<void> {
  const target = path.join(OUT, rel);
  await fsp.mkdir(path.dirname(target), { recursive: true });
  await fsp.writeFile(target, JSON.stringify(value));
}

function filePaths(nodes: TreeNode[]): string[] {
  return nodes.flatMap((n) =>
    n.type === "file" ? [n.path] : filePaths(n.children ?? []),
  );
}

ensureDataRoot();
await fsp.rm(OUT, { recursive: true, force: true });

const children = await buildTree(DATA_ROOT, readTitles(), readOrder());
const tree: TreeNode = { name: "Results", path: "", type: "folder", children };
await writeJson("tree.json", tree);
await writeJson("compare.json", await buildComparison());

const files = filePaths(children);
for (const rel of files) {
  await writeJson(`csv/${rel}.json`, await readCsvPayload(path.join(DATA_ROOT, rel)));
}

// GitHub Pages: skip Jekyll so nothing in dist/ is filtered.
await fsp.writeFile(path.resolve(OUT, "../.nojekyll"), "");

console.log(`Exported ${files.length} CSVs from ${DATA_ROOT} -> ${OUT}`);

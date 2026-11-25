import cors from "cors";
import express from "express";
import fs from "node:fs";
import fsp from "node:fs/promises";
import path from "node:path";
import multer from "multer";
import {
  DATA_ROOT,
  buildComparison,
  buildTree,
  dropFromOrder,
  dropTitles,
  ensureDataRoot,
  moveTitle,
  readCsvPayload,
  readOrder,
  readTitles,
  renameInOrder,
  resolveInsideData,
  toRelative,
  writeOrder,
  writeTitles,
  type TreeNode,
} from "./data.ts";

const PORT = Number(process.env.PORT ?? 8787);

function isValidFolderName(name: string): boolean {
  return /^[A-Za-z0-9][A-Za-z0-9 _.-]{0,63}$/.test(name);
}

const upload = multer({
  storage: multer.memoryStorage(),
  limits: { fileSize: 50 * 1024 * 1024 },
  fileFilter: (_req, file, cb) => {
    if (!file.originalname.toLowerCase().endsWith(".csv")) {
      cb(new Error("Only .csv files are allowed"));
      return;
    }
    cb(null, true);
  },
});

ensureDataRoot();

const app = express();
app.use(cors({ origin: true }));
app.use(express.json({ limit: "2mb" }));

app.get("/api/health", (_req, res) => {
  res.json({ ok: true, dataRoot: DATA_ROOT });
});

app.get("/api/tree", async (_req, res) => {
  try {
    ensureDataRoot();
    const children = await buildTree(DATA_ROOT, readTitles(), readOrder());
    res.json({
      name: "Results",
      path: "",
      type: "folder",
      children,
    } satisfies TreeNode);
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to read results tree" });
  }
});

app.get("/api/csv", async (req, res) => {
  try {
    const rel = String(req.query.path ?? "");
    const absolute = resolveInsideData(rel);
    if (!absolute) {
      res.status(400).json({ error: "Invalid path" });
      return;
    }

    const stat = await fsp.stat(absolute);
    if (!stat.isFile() || !absolute.toLowerCase().endsWith(".csv")) {
      res.status(404).json({ error: "CSV not found" });
      return;
    }

    res.json(await readCsvPayload(absolute));
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to load CSV" });
  }
});

app.get("/api/compare", async (_req, res) => {
  try {
    ensureDataRoot();
    const payload = await buildComparison();
    res.json(payload);
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to build comparison" });
  }
});

function isValidCsvName(name: string): boolean {
  const base = name.trim();
  if (!base) return false;
  if (base.includes("/") || base.includes("\\") || base.includes("\0")) {
    return false;
  }
  const withExt = base.toLowerCase().endsWith(".csv") ? base : `${base}.csv`;
  return /^[A-Za-z0-9][A-Za-z0-9 _.\-()[\]]{0,120}\.csv$/i.test(withExt);
}

function normalizeCsvName(name: string): string {
  const trimmed = name.trim();
  return trimmed.toLowerCase().endsWith(".csv") ? trimmed : `${trimmed}.csv`;
}

app.post("/api/rename", async (req, res) => {
  try {
    const currentPath = String(req.body?.path ?? "").trim();
    const newNameRaw = String(req.body?.name ?? "").trim();

    if (!currentPath) {
      res.status(400).json({ error: "Missing current path" });
      return;
    }
    if (!isValidCsvName(newNameRaw)) {
      res.status(400).json({
        error:
          "Name must start with a letter/number and end up as a .csv file name",
      });
      return;
    }

    const absolute = resolveInsideData(currentPath);
    if (!absolute) {
      res.status(400).json({ error: "Invalid path" });
      return;
    }

    const stat = await fsp.stat(absolute);
    if (!stat.isFile() || !absolute.toLowerCase().endsWith(".csv")) {
      res.status(404).json({ error: "CSV not found" });
      return;
    }

    const newName = normalizeCsvName(newNameRaw);
    const target = path.join(path.dirname(absolute), newName);
    if (path.resolve(target) === path.resolve(absolute)) {
      res.json({
        name: newName,
        path: toRelative(absolute),
        type: "file",
      });
      return;
    }

    if (fs.existsSync(target)) {
      res.status(409).json({ error: "A file with that name already exists" });
      return;
    }

    await fsp.rename(absolute, target);
    moveTitle(toRelative(absolute), toRelative(target));
    renameInOrder(toRelative(absolute), newName);
    res.json({
      name: newName,
      path: toRelative(target),
      type: "file",
    });
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to rename CSV" });
  }
});

app.post("/api/delete", async (req, res) => {
  try {
    const rel = String(req.body?.path ?? "").trim();
    if (!rel) {
      res.status(400).json({ error: "Cannot delete the Results root" });
      return;
    }

    const absolute = resolveInsideData(rel);
    if (!absolute || absolute === DATA_ROOT) {
      res.status(400).json({ error: "Invalid path" });
      return;
    }

    const stat = await fsp.stat(absolute);
    if (stat.isDirectory()) {
      await fsp.rm(absolute, { recursive: true, force: false });
      dropTitles(rel);
      dropFromOrder(rel);
      res.json({ deleted: rel, type: "folder" });
      return;
    }

    if (stat.isFile() && absolute.toLowerCase().endsWith(".csv")) {
      await fsp.unlink(absolute);
      dropTitles(rel);
      dropFromOrder(rel);
      res.json({ deleted: rel, type: "file" });
      return;
    }

    res.status(400).json({ error: "Only CSV files and folders can be deleted" });
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to delete item" });
  }
});

app.post("/api/title", async (req, res) => {
  try {
    const rel = String(req.body?.path ?? "").trim();
    const title = String(req.body?.title ?? "").trim();

    const absolute = rel ? resolveInsideData(rel) : null;
    if (!absolute || absolute === DATA_ROOT || !fs.existsSync(absolute)) {
      res.status(400).json({ error: "Invalid path" });
      return;
    }
    if (title.length > 80 || /[\0\r\n]/.test(title)) {
      res.status(400).json({ error: "Title must be a single line, up to 80 characters" });
      return;
    }

    const key = toRelative(absolute);
    const titles = readTitles();
    if (title) titles[key] = title;
    else delete titles[key];
    writeTitles(titles);
    res.json({ path: key, title: title || null });
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to save title" });
  }
});

app.post("/api/order", async (req, res) => {
  try {
    const parent = String(req.body?.parent ?? "").trim();
    const names: unknown = req.body?.names;
    if (
      !Array.isArray(names) ||
      names.length > 1000 ||
      !names.every((n) => typeof n === "string" && n && !n.includes("/") && !n.includes("\0"))
    ) {
      res.status(400).json({ error: "Invalid order" });
      return;
    }

    const parentAbsolute = resolveInsideData(parent || ".");
    if (!parentAbsolute || !(await fsp.stat(parentAbsolute)).isDirectory()) {
      res.status(400).json({ error: "Invalid parent folder" });
      return;
    }

    const order = readOrder();
    order[toRelative(parentAbsolute)] = names as string[];
    writeOrder(order);
    res.json({ parent: toRelative(parentAbsolute), names });
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to save order" });
  }
});

app.post("/api/folders", async (req, res) => {
  try {
    const name = String(req.body?.name ?? "").trim();
    const parent = String(req.body?.parent ?? "").trim();

    if (!isValidFolderName(name)) {
      res.status(400).json({
        error:
          "Folder name must start with a letter/number and use only letters, numbers, spaces, _, ., -",
      });
      return;
    }

    const parentAbsolute = resolveInsideData(parent || ".");
    if (!parentAbsolute) {
      res.status(400).json({ error: "Invalid parent folder" });
      return;
    }

    const parentStat = await fsp.stat(parentAbsolute);
    if (!parentStat.isDirectory()) {
      res.status(400).json({ error: "Parent is not a folder" });
      return;
    }

    const target = path.join(parentAbsolute, name);
    if (fs.existsSync(target)) {
      res.status(409).json({ error: "Folder already exists" });
      return;
    }

    await fsp.mkdir(target, { recursive: false });
    res.status(201).json({
      name,
      path: toRelative(target),
      type: "folder",
    });
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: "Failed to create folder" });
  }
});

app.post("/api/upload", (req, res) => {
  upload.single("file")(req, res, async (err) => {
    if (err) {
      res.status(400).json({ error: err.message || "Upload failed" });
      return;
    }

    try {
      const folder = String(req.body?.folder ?? "").trim();
      const folderAbsolute = resolveInsideData(folder || ".");
      if (!folderAbsolute) {
        res.status(400).json({ error: "Invalid folder" });
        return;
      }

      const folderStat = await fsp.stat(folderAbsolute);
      if (!folderStat.isDirectory()) {
        res.status(400).json({ error: "Upload target must be a folder" });
        return;
      }

      const file = req.file;
      if (!file) {
        res.status(400).json({ error: "No file uploaded" });
        return;
      }

      const safeName = path
        .basename(file.originalname)
        .replace(/[^\w.\- ()[\]]+/g, "_");
      if (!safeName.toLowerCase().endsWith(".csv")) {
        res.status(400).json({ error: "Only .csv files are allowed" });
        return;
      }

      const target = path.join(folderAbsolute, safeName);
      if (fs.existsSync(target)) {
        res.status(409).json({ error: "A file with that name already exists" });
        return;
      }

      await fsp.writeFile(target, file.buffer);
      res.status(201).json({
        name: safeName,
        path: toRelative(target),
        type: "file",
      });
    } catch (error) {
      console.error(error);
      res.status(500).json({ error: "Failed to upload CSV" });
    }
  });
});

app.listen(PORT, "127.0.0.1", () => {
  console.log(`CSV Results API on http://127.0.0.1:${PORT}`);
  console.log(`Data root: ${DATA_ROOT}`);
});

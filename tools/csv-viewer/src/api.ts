import { READ_ONLY } from "./lib/mode";
import type { ComparePayload, CsvPayload, TreeNode } from "./types";

async function readJson<T>(response: Response): Promise<T> {
  const data = (await response.json()) as T & { error?: string };
  if (!response.ok) {
    throw new Error(
      (data as { error?: string }).error || `Request failed (${response.status})`,
    );
  }
  return data;
}

// Static build: relative URLs so it works under any GitHub Pages sub-path.
function staticUrl(rel: string): string {
  return `./data/${rel.split("/").map(encodeURIComponent).join("/")}`;
}

export async function fetchTree(): Promise<TreeNode> {
  const response = await fetch(READ_ONLY ? staticUrl("tree.json") : "/api/tree");
  return readJson<TreeNode>(response);
}

export async function fetchCsv(path: string): Promise<CsvPayload> {
  const response = await fetch(
    READ_ONLY
      ? staticUrl(`csv/${path}.json`)
      : `/api/csv?path=${encodeURIComponent(path)}`,
  );
  return readJson<CsvPayload>(response);
}

export async function fetchCompare(): Promise<ComparePayload> {
  const response = await fetch(
    READ_ONLY ? staticUrl("compare.json") : "/api/compare",
  );
  return readJson<ComparePayload>(response);
}

export async function createFolder(
  name: string,
  parent = "",
): Promise<TreeNode> {
  const response = await fetch("/api/folders", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name, parent }),
  });
  return readJson<TreeNode>(response);
}

export async function uploadCsv(folder: string, file: File): Promise<TreeNode> {
  const form = new FormData();
  form.append("folder", folder);
  form.append("file", file);
  const response = await fetch("/api/upload", {
    method: "POST",
    body: form,
  });
  return readJson<TreeNode>(response);
}

export async function renameCsv(
  path: string,
  name: string,
): Promise<TreeNode> {
  const response = await fetch("/api/rename", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ path, name }),
  });
  return readJson<TreeNode>(response);
}

export async function deleteItem(
  path: string,
): Promise<{ deleted: string; type: "file" | "folder" }> {
  const response = await fetch("/api/delete", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ path }),
  });
  return readJson(response);
}

export async function setTitle(path: string, title: string): Promise<void> {
  const response = await fetch("/api/title", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ path, title }),
  });
  await readJson(response);
}

export async function saveOrder(parent: string, names: string[]): Promise<void> {
  const response = await fetch("/api/order", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ parent, names }),
  });
  await readJson(response);
}

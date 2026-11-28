import { useCallback, useEffect, useMemo, useState } from "react";
import {
  createFolder,
  deleteItem,
  fetchCsv,
  fetchTree,
  renameCsv,
  uploadCsv,
} from "./api";
import { saveOrder, setTitle } from "./api";
import { CompareView } from "./components/CompareView";
import { DataTable } from "./components/DataTable";
import { Sidebar } from "./components/Sidebar";
import type { CsvPayload, TreeNode } from "./types";
import { formatTime, totalCsvTime } from "./lib/heatmap";
import { READ_ONLY } from "./lib/mode";
import {
  fileShareUrl,
  normalizePath,
  parseRoute,
  writeRoute,
} from "./lib/routing";

type ViewMode = "browse" | "compare";

function breadcrumbParts(path: string | null): string[] {
  if (!path) return ["Results"];
  return ["Results", ...path.split("/").filter(Boolean)];
}

function fileNameFromPath(path: string): string {
  return path.split("/").pop() ?? path;
}

function treeHasFile(node: TreeNode, path: string): boolean {
  if (node.type === "file" && node.path === path) return true;
  return (node.children ?? []).some((child) => treeHasFile(child, path));
}

export default function App() {
  const initialRoute = parseRoute();
  const [viewMode, setViewMode] = useState<ViewMode>(
    initialRoute.kind === "compare" ? "compare" : "browse",
  );
  const [tree, setTree] = useState<TreeNode | null>(null);
  const [selectedPath, setSelectedPath] = useState<string | null>(
    initialRoute.kind === "file" ? initialRoute.path : null,
  );
  const [selectedFolder, setSelectedFolder] = useState(() => {
    if (initialRoute.kind !== "file") return "";
    const path = initialRoute.path;
    return path.includes("/") ? path.slice(0, path.lastIndexOf("/")) : "";
  });
  const [csv, setCsv] = useState<CsvPayload | null>(null);
  const [loadingTree, setLoadingTree] = useState(true);
  const [loadingCsv, setLoadingCsv] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [renaming, setRenaming] = useState(false);
  const [renameValue, setRenameValue] = useState("");
  const [renameBusy, setRenameBusy] = useState(false);
  const [linkCopied, setLinkCopied] = useState(false);

  const refreshTree = useCallback(async () => {
    setLoadingTree(true);
    setError(null);
    try {
      const next = await fetchTree();
      setTree(next);
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to load tree");
    } finally {
      setLoadingTree(false);
    }
  }, []);

  useEffect(() => {
    void refreshTree();
  }, [refreshTree]);

  // After the tree loads, validate a deep-linked file still exists.
  useEffect(() => {
    if (!tree || loadingTree || viewMode !== "browse" || !selectedPath) return;
    if (!treeHasFile(tree, selectedPath)) {
      setError(`Linked CSV not found: ${selectedPath}`);
      setSelectedPath(null);
      setCsv(null);
      writeRoute({ kind: "home" }, "replace");
    }
  }, [tree, loadingTree, viewMode, selectedPath]);

  // Keep the address bar in sync and support Back/Forward.
  useEffect(() => {
    function onPopState() {
      const route = parseRoute();
      setRenaming(false);
      setLinkCopied(false);
      if (route.kind === "compare") {
        setViewMode("compare");
        setSelectedPath(null);
        return;
      }
      setViewMode("browse");
      if (route.kind === "file") {
        setSelectedPath(route.path);
        setSelectedFolder(
          route.path.includes("/")
            ? route.path.slice(0, route.path.lastIndexOf("/"))
            : "",
        );
      } else {
        setSelectedPath(null);
      }
    }
    window.addEventListener("popstate", onPopState);
    return () => window.removeEventListener("popstate", onPopState);
  }, []);

  useEffect(() => {
    if (viewMode !== "browse" || !selectedPath) {
      setCsv(null);
      return;
    }

    let cancelled = false;
    setLoadingCsv(true);
    setError(null);

    void fetchCsv(selectedPath)
      .then((payload) => {
        if (!cancelled) setCsv(payload);
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setCsv(null);
          setError(err instanceof Error ? err.message : "Failed to load CSV");
        }
      })
      .finally(() => {
        if (!cancelled) setLoadingCsv(false);
      });

    return () => {
      cancelled = true;
    };
  }, [selectedPath, viewMode]);

  const crumbs = useMemo(() => {
    if (viewMode === "compare") return ["Results", "Compare"];
    return breadcrumbParts(selectedPath ?? (selectedFolder || null));
  }, [viewMode, selectedPath, selectedFolder]);

  const csvTotalTime = useMemo(
    () => (csv ? totalCsvTime(csv.columns, csv.rows) : null),
    [csv],
  );

  function handleSelectFile(path: string, historyMode: "push" | "replace" = "push") {
    const normalized = normalizePath(path);
    setViewMode("browse");
    setSelectedPath(normalized);
    setLinkCopied(false);
    const parent = normalized.includes("/")
      ? normalized.slice(0, normalized.lastIndexOf("/"))
      : "";
    setSelectedFolder(parent);
    writeRoute({ kind: "file", path: normalized }, historyMode);
  }

  function handleOpenCompare() {
    setRenaming(false);
    setLinkCopied(false);
    setViewMode("compare");
    // Selection details (?a=…) are owned by CompareView once it mounts.
    writeRoute({ kind: "compare", algos: null });
  }

  function handleSelectFolder(path: string) {
    setSelectedFolder(path);
  }

  async function handleCopyLink() {
    const url =
      viewMode === "compare"
        ? window.location.href
        : selectedPath
          ? fileShareUrl(selectedPath)
          : null;
    if (!url) return;
    try {
      await navigator.clipboard.writeText(url);
      setLinkCopied(true);
      window.setTimeout(() => setLinkCopied(false), 2000);
    } catch {
      window.prompt("Copy this link:", url);
    }
  }

  async function handleCreateFolder(name: string, parent: string) {
    await createFolder(name, parent);
    await refreshTree();
  }

  async function handleUpload(folder: string, file: File) {
    const created = await uploadCsv(folder, file);
    await refreshTree();
    handleSelectFile(created.path);
  }

  async function handleReorder(parent: string, names: string[]) {
    // Optimistic: reorder the local tree right away, then persist.
    const rank = new Map(names.map((n, i) => [n, i]));
    const apply = (node: TreeNode): TreeNode => {
      if (node.type !== "folder" || !node.children) return node;
      const children = node.children.map(apply);
      if (node.path === parent) {
        children.sort(
          (a, b) =>
            (a.type === b.type ? 0 : a.type === "folder" ? -1 : 1) ||
            (rank.get(a.name) ?? 0) - (rank.get(b.name) ?? 0),
        );
      }
      return { ...node, children };
    };
    setTree((prev) => (prev ? apply(prev) : prev));
    try {
      await saveOrder(parent, names);
    } catch (err) {
      setError(err instanceof Error ? err.message : "Could not save order");
      await refreshTree();
    }
  }

  async function handleSetTitle(node: TreeNode, title: string) {
    await setTitle(node.path, title);
    await refreshTree();
  }

  async function handleDelete(node: TreeNode) {
    await deleteItem(node.path);
    if (
      selectedPath === node.path ||
      (node.type === "folder" &&
        selectedPath !== null &&
        selectedPath.startsWith(`${node.path}/`))
    ) {
      setSelectedPath(null);
      setCsv(null);
      setRenaming(false);
      writeRoute({ kind: "home" }, "replace");
    }
    if (
      selectedFolder === node.path ||
      (node.type === "folder" && selectedFolder.startsWith(`${node.path}/`))
    ) {
      setSelectedFolder("");
    }
    await refreshTree();
  }

  function startRename() {
    if (!selectedPath) return;
    setRenameValue(fileNameFromPath(selectedPath));
    setRenaming(true);
    setError(null);
  }

  function cancelRename() {
    setRenaming(false);
    setRenameValue("");
  }

  async function saveRename() {
    if (!selectedPath) return;
    const nextName = renameValue.trim();
    if (!nextName) return;

    setRenameBusy(true);
    setError(null);
    try {
      const renamed = await renameCsv(selectedPath, nextName);
      setSelectedPath(renamed.path);
      setCsv((prev) =>
        prev
          ? { ...prev, path: renamed.path, name: renamed.name }
          : prev,
      );
      setRenaming(false);
      writeRoute({ kind: "file", path: renamed.path }, "replace");
      await refreshTree();
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to rename CSV");
    } finally {
      setRenameBusy(false);
    }
  }

  return (
    <div className="app-shell">
      <Sidebar
        tree={tree}
        selectedPath={viewMode === "browse" ? selectedPath : null}
        selectedFolder={selectedFolder}
        compareActive={viewMode === "compare"}
        onOpenCompare={handleOpenCompare}
        onSelectFile={(path) => {
          setRenaming(false);
          handleSelectFile(path);
        }}
        onSelectFolder={handleSelectFolder}
        onCreateFolder={handleCreateFolder}
        onUpload={handleUpload}
        onReorder={handleReorder}
        onSetTitle={handleSetTitle}
        onDelete={handleDelete}
      />

      <main
        className={["main-panel", viewMode === "compare" ? "is-compare" : ""].join(
          " ",
        )}
      >
        <header
          className={[
            "main-header",
            viewMode === "compare" ? "is-compare" : "",
          ].join(" ")}
        >
          <div className="breadcrumb">
            {crumbs.map((part, index) => (
              <span key={`${part}-${index}`} className="crumb">
                {index > 0 && <span className="crumb-sep">/</span>}
                <span className={index === crumbs.length - 1 ? "crumb-current" : ""}>
                  {part}
                </span>
              </span>
            ))}
          </div>

          {viewMode === "browse" && selectedPath && renaming ? (
            <div className="title-edit">
              <input
                className="title-edit-input"
                value={renameValue}
                autoFocus
                disabled={renameBusy}
                onChange={(e) => setRenameValue(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") void saveRename();
                  if (e.key === "Escape") cancelRename();
                }}
              />
              <button
                type="button"
                className="btn btn-primary"
                disabled={renameBusy || !renameValue.trim()}
                onClick={() => void saveRename()}
              >
                Save
              </button>
              <button
                type="button"
                className="btn btn-ghost"
                disabled={renameBusy}
                onClick={cancelRename}
              >
                Cancel
              </button>
            </div>
          ) : (
            <div className="title-block">
              <div className="title-row">
                <h1 className="page-title">
                  {viewMode === "compare"
                    ? "Instance comparison"
                    : selectedPath
                      ? fileNameFromPath(selectedPath)
                      : loadingTree
                        ? "Loading…"
                        : "Select a CSV"}
                </h1>
                {((viewMode === "browse" && selectedPath) ||
                  viewMode === "compare") && (
                  <button
                    type="button"
                    className="icon-btn"
                    title={linkCopied ? "Link copied" : "Copy shareable link"}
                    aria-label={linkCopied ? "Link copied" : "Copy shareable link"}
                    onClick={() => void handleCopyLink()}
                  >
                    {linkCopied ? (
                      <svg
                        width="16"
                        height="16"
                        viewBox="0 0 16 16"
                        fill="none"
                        aria-hidden="true"
                      >
                        <path
                          d="M3.5 8.5l3 3 6-7"
                          stroke="currentColor"
                          strokeWidth="1.6"
                          strokeLinecap="round"
                          strokeLinejoin="round"
                        />
                      </svg>
                    ) : (
                      <svg
                        width="16"
                        height="16"
                        viewBox="0 0 16 16"
                        fill="none"
                        aria-hidden="true"
                      >
                        <path
                          d="M6.5 9.5a3 3 0 0 0 4.2.2l1.8-1.8a3 3 0 0 0-4.2-4.2L7.5 4.5"
                          stroke="currentColor"
                          strokeWidth="1.4"
                          strokeLinecap="round"
                        />
                        <path
                          d="M9.5 6.5a3 3 0 0 0-4.2-.2L3.5 8.1a3 3 0 0 0 4.2 4.2L8.5 11.5"
                          stroke="currentColor"
                          strokeWidth="1.4"
                          strokeLinecap="round"
                        />
                      </svg>
                    )}
                  </button>
                )}
                {!READ_ONLY && viewMode === "browse" && selectedPath && (
                  <button
                    type="button"
                    className="icon-btn"
                    title="Rename CSV"
                    aria-label="Rename CSV"
                    onClick={startRename}
                  >
                    <svg
                      width="16"
                      height="16"
                      viewBox="0 0 16 16"
                      fill="none"
                      aria-hidden="true"
                    >
                      <path
                        d="M11.5 1.5l3 3L5 14H2v-3L11.5 1.5z"
                        stroke="currentColor"
                        strokeWidth="1.4"
                        strokeLinejoin="round"
                      />
                      <path
                        d="M10 3l3 3"
                        stroke="currentColor"
                        strokeWidth="1.4"
                      />
                    </svg>
                  </button>
                )}
              </div>
              {viewMode === "compare" && (
                <p className="page-subtitle">
                  Compare algorithm performance across instances.
                </p>
              )}
            </div>
          )}
        </header>

        {error && viewMode === "browse" && (
          <div className="banner error">{error}</div>
        )}

        {viewMode === "compare" && <CompareView />}

        {viewMode === "browse" && !selectedPath && !loadingCsv && (
          <div className="empty-state">
            <h2>Results workspace</h2>
            <p>
              Pick an experiment folder on the left, open a CSV, or open{" "}
              <strong>Compare</strong> to heatmap all algorithms by instance.
            </p>
          </div>
        )}

        {viewMode === "browse" && selectedPath && loadingCsv && (
          <div className="empty-state">
            <p>Loading CSV…</p>
          </div>
        )}

        {viewMode === "browse" && selectedPath && !loadingCsv && csv && (
          <>
            {csvTotalTime !== null && (
              <div
                className="meta-row"
                title="Cumulative solver runtime across all instances in this CSV"
              >
                <span className="meta-label">Total runtime</span>
                <span className="meta-value">{formatTime(csvTotalTime)} s</span>
              </div>
            )}
            <DataTable data={csv} />
          </>
        )}
      </main>
    </div>
  );
}

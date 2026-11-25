import { useMemo, useRef, useState } from "react";
import { displayName, searchText } from "../lib/labels";
import { READ_ONLY } from "../lib/mode";
import type { TreeNode } from "../types";

type SidebarProps = {
  tree: TreeNode | null;
  selectedPath: string | null;
  selectedFolder: string;
  compareActive?: boolean;
  onOpenCompare?: () => void;
  onSelectFile: (path: string) => void;
  onSelectFolder: (path: string) => void;
  onCreateFolder: (name: string, parent: string) => Promise<void>;
  onUpload: (folder: string, file: File) => Promise<void>;
  onSetTitle: (node: TreeNode, title: string) => Promise<void>;
  onReorder: (parent: string, names: string[]) => Promise<void>;
  onDelete: (node: TreeNode) => Promise<void>;
};

type DropPos = "before" | "after";

type Dnd = {
  enabled: boolean;
  dragPath: string | null;
  hint: { path: string; pos: DropPos } | null;
  canDropOn: (target: TreeNode) => boolean;
  start: (node: TreeNode) => void;
  over: (target: TreeNode, pos: DropPos) => void;
  clear: () => void;
  drop: (target: TreeNode, pos: DropPos) => void;
  end: () => void;
};

function parentPath(path: string): string {
  const i = path.lastIndexOf("/");
  return i < 0 ? "" : path.slice(0, i);
}

function findNode(node: TreeNode, path: string): TreeNode | null {
  if (node.path === path) return node;
  for (const child of node.children ?? []) {
    const hit = findNode(child, path);
    if (hit) return hit;
  }
  return null;
}

function filterTree(node: TreeNode, query: string): TreeNode | null {
  if (!query) return node;
  const q = query.toLowerCase();

  if (node.type === "file") {
    return searchText(node.name, node.title).includes(q) ? node : null;
  }

  const children = (node.children ?? [])
    .map((child) => filterTree(child, query))
    .filter((child): child is TreeNode => child !== null);

  if (searchText(node.name || "Results", node.title).includes(q) || children.length > 0) {
    return { ...node, children };
  }
  return null;
}

function highlightMatch(label: string, query: string) {
  const q = query.trim();
  if (!q) return label;
  const lower = label.toLowerCase();
  const index = lower.indexOf(q.toLowerCase());
  if (index < 0) return label;
  return (
    <>
      {label.slice(0, index)}
      <mark className="tree-mark">{label.slice(index, index + q.length)}</mark>
      {label.slice(index + q.length)}
    </>
  );
}

function FolderIcon({ open }: { open: boolean }) {
  return (
    <svg width="15" height="15" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      {open ? (
        <path
          d="M2 12.5V4a1 1 0 0 1 1-1h3l1.2 1.3H12a1 1 0 0 1 1 1V6M2 12.5l1.6-5.2a1 1 0 0 1 .95-.7H14a.5.5 0 0 1 .48.64l-1.5 5.1a1 1 0 0 1-.96.72H3a1 1 0 0 1-1-.56z"
          stroke="currentColor"
          strokeWidth="1.2"
          strokeLinejoin="round"
          strokeLinecap="round"
        />
      ) : (
        <path
          d="M2.5 4.5a1 1 0 0 1 1-1h3l1.2 1.3h4.8a1 1 0 0 1 1 1v6.2a1 1 0 0 1-1 1h-9a1 1 0 0 1-1-1V4.5z"
          stroke="currentColor"
          strokeWidth="1.2"
          strokeLinejoin="round"
        />
      )}
    </svg>
  );
}

function SheetIcon() {
  return (
    <svg width="15" height="15" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      <path
        d="M4.5 2h5l3 3v8.5a.5.5 0 0 1-.5.5h-7.5a.5.5 0 0 1-.5-.5v-11a.5.5 0 0 1 .5-.5z"
        stroke="currentColor"
        strokeWidth="1.2"
        strokeLinejoin="round"
      />
      <path d="M9.5 2v3h3M6 8.5h4.5M6 11h4.5M8.25 7v6" stroke="currentColor" strokeWidth="1" strokeLinecap="round" />
    </svg>
  );
}

function CompareIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      <path d="M3 12V4M8 12V7M13 12V5" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
    </svg>
  );
}

function PencilIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      <path
        d="M11 2.5l2.5 2.5L5.5 13H3v-2.5L11 2.5z"
        stroke="currentColor"
        strokeWidth="1.3"
        strokeLinejoin="round"
      />
    </svg>
  );
}

function TrashIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      <path
        d="M3.5 4.5h9M6.5 4.5V3.5a1 1 0 0 1 1-1h1a1 1 0 0 1 1 1v1M5 4.5l.5 8.5a1 1 0 0 0 1 .9h3a1 1 0 0 0 1-.9L11 4.5"
        stroke="currentColor"
        strokeWidth="1.3"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

function TreeItem({
  node,
  depth,
  query,
  selectedPath,
  selectedFolder,
  compareActive,
  expanded,
  deletingPath,
  onToggle,
  onSelectFile,
  onSelectFolder,
  onSetTitle,
  onDelete,
  dnd,
}: {
  node: TreeNode;
  depth: number;
  query: string;
  selectedPath: string | null;
  selectedFolder: string;
  compareActive: boolean;
  expanded: Set<string>;
  deletingPath: string | null;
  onToggle: (path: string) => void;
  onSelectFile: (path: string) => void;
  onSelectFolder: (path: string) => void;
  onSetTitle: (node: TreeNode, title: string) => Promise<void>;
  onDelete: (node: TreeNode) => void;
  dnd: Dnd;
}) {
  const isFolder = node.type === "folder";
  const isRoot = node.path === "";
  const isOpen = isFolder && (isRoot || expanded.has(node.path));
  const isSelectedFile = !isFolder && selectedPath === node.path;
  // A folder is only "selected" while no CSV is open; expanded is a separate state.
  const isSelectedFolder =
    isFolder && !selectedPath && !compareActive && selectedFolder === node.path;
  const isActive = isSelectedFile || isSelectedFolder;
  const busy = deletingPath === node.path;
  const defaultLabel = isRoot ? "Results" : displayName(node.name);
  const label = !isRoot && node.title ? node.title : defaultLabel;
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState("");
  const [saving, setSaving] = useState(false);
  const cancelled = useRef(false);
  const fullName = isRoot ? "results/" : node.name;

  function startEdit() {
    setDraft(node.title ?? defaultLabel);
    cancelled.current = false;
    setEditing(true);
  }

  async function commitEdit() {
    if (saving || cancelled.current) return;
    const next = draft.trim();
    // Same as the default name means "no custom title".
    const value = next === defaultLabel ? "" : next;
    setEditing(false);
    if (value === (node.title ?? "")) return;
    setSaving(true);
    try {
      await onSetTitle(node, value);
    } finally {
      setSaving(false);
    }
  }

  const canDrag = dnd.enabled && !isRoot && !editing;
  const isDragging = dnd.dragPath === node.path;
  const hintPos = dnd.hint?.path === node.path ? dnd.hint.pos : null;

  function dropPos(e: React.DragEvent<HTMLDivElement>): DropPos {
    const rect = e.currentTarget.getBoundingClientRect();
    return e.clientY < rect.top + rect.height / 2 ? "before" : "after";
  }

  return (
    <div className="tree-branch">
      <div
        className={[
          "tree-row",
          isActive ? "is-active" : "",
          isRoot ? "is-root" : "",
          isDragging ? "is-dragging" : "",
          hintPos === "before" ? "drop-before" : "",
          hintPos === "after" ? "drop-after" : "",
        ].join(" ")}
        draggable={canDrag}
        onDragStart={(e) => {
          if (!canDrag) return;
          e.dataTransfer.effectAllowed = "move";
          e.dataTransfer.setData("text/plain", node.path);
          dnd.start(node);
        }}
        onDragOver={(e) => {
          if (!dnd.dragPath) return;
          if (dnd.canDropOn(node)) {
            e.preventDefault();
            e.dataTransfer.dropEffect = "move";
            dnd.over(node, dropPos(e));
          } else {
            dnd.clear();
          }
        }}
        onDrop={(e) => {
          if (!dnd.canDropOn(node)) return;
          e.preventDefault();
          dnd.drop(node, dropPos(e));
        }}
        onDragEnd={dnd.end}
      >
        {editing ? (
          <div
            className="tree-item is-editing"
            style={{ paddingLeft: `${8 + depth * 14}px` }}
          >
            <span className="tree-type-icon" aria-hidden>
              {isFolder ? <FolderIcon open={!!isOpen} /> : <SheetIcon />}
            </span>
            <input
              className="tree-edit-input"
              value={draft}
              autoFocus
              maxLength={80}
              placeholder={defaultLabel}
              aria-label={`Sidebar title for ${fullName}`}
              onFocus={(e) => e.currentTarget.select()}
              onChange={(e) => setDraft(e.target.value)}
              onBlur={() => void commitEdit()}
              onKeyDown={(e) => {
                if (e.key === "Enter") void commitEdit();
                if (e.key === "Escape") {
                  cancelled.current = true;
                  setEditing(false);
                }
              }}
            />
          </div>
        ) : (
          <button
            type="button"
            className={["tree-item", isFolder ? "is-folder" : "is-file"].join(" ")}
            style={{ paddingLeft: `${8 + depth * 14}px` }}
            title={fullName}
            aria-expanded={isFolder && !isRoot ? isOpen : undefined}
            onClick={() => {
              if (isFolder) {
                onSelectFolder(node.path);
                if (!isRoot) onToggle(node.path);
              } else {
                onSelectFile(node.path);
              }
            }}
          >
            <span className="tree-type-icon" aria-hidden>
              {isFolder ? <FolderIcon open={!!isOpen} /> : <SheetIcon />}
            </span>
            <span className="tree-label">{highlightMatch(label, query)}</span>
          </button>
        )}

        {!READ_ONLY && !isRoot && !editing && (
          <button
            type="button"
            className="tree-delete tree-edit"
            title="Edit sidebar title (file name stays the same)"
            aria-label={`Edit sidebar title for ${fullName}`}
            onClick={(e) => {
              e.stopPropagation();
              startEdit();
            }}
          >
            <PencilIcon />
          </button>
        )}

        {!READ_ONLY && !isRoot && (
          <button
            type="button"
            className="tree-delete"
            title={isFolder ? "Delete folder and contents" : "Delete CSV"}
            aria-label={
              isFolder ? `Delete folder ${fullName}` : `Delete CSV ${fullName}`
            }
            disabled={busy}
            onClick={(e) => {
              e.stopPropagation();
              onDelete(node);
            }}
          >
            <TrashIcon />
          </button>
        )}
      </div>

      {isFolder &&
        isOpen &&
        (node.children ?? []).map((child) => (
          <TreeItem
            key={`${child.type}:${child.path}`}
            node={child}
            depth={depth + 1}
            query={query}
            selectedPath={selectedPath}
            selectedFolder={selectedFolder}
            compareActive={compareActive}
            expanded={expanded}
            deletingPath={deletingPath}
            onToggle={onToggle}
            onSelectFile={onSelectFile}
            onSelectFolder={onSelectFolder}
            onSetTitle={onSetTitle}
            onDelete={onDelete}
            dnd={dnd}
          />
        ))}
    </div>
  );
}

function destinationLabel(folder: string): string {
  if (!folder) return "Results";
  return ["Results", ...folder.split("/").filter(Boolean).map(displayName)].join(" / ");
}

export function Sidebar({
  tree,
  selectedPath,
  selectedFolder,
  compareActive = false,
  onOpenCompare,
  onSelectFile,
  onSelectFolder,
  onCreateFolder,
  onUpload,
  onSetTitle,
  onReorder,
  onDelete,
}: SidebarProps) {
  const [query, setQuery] = useState("");
  const [expanded, setExpanded] = useState<Set<string>>(() => new Set([""]));
  const [creating, setCreating] = useState(false);
  const [uploading, setUploading] = useState(false);
  const [newFolderName, setNewFolderName] = useState("");
  const [busy, setBusy] = useState(false);
  const [deletingPath, setDeletingPath] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const visibleTree = useMemo(() => {
    if (!tree) return null;
    return filterTree(tree, query.trim());
  }, [tree, query]);

  const emptyResults = useMemo(() => {
    if (!tree) return false;
    return (tree.children ?? []).length === 0;
  }, [tree]);

  function toggle(path: string) {
    setExpanded((prev) => {
      const next = new Set(prev);
      if (next.has(path)) next.delete(path);
      else next.add(path);
      return next;
    });
  }

  async function handleCreateFolder() {
    const name = newFolderName.trim();
    if (!name) return;
    setBusy(true);
    setError(null);
    try {
      await onCreateFolder(name, selectedFolder);
      setNewFolderName("");
      setCreating(false);
      setExpanded((prev) => new Set(prev).add(selectedFolder));
    } catch (err) {
      setError(err instanceof Error ? err.message : "Could not create folder");
    } finally {
      setBusy(false);
    }
  }

  async function handleUpload(file: File | undefined) {
    if (!file) return;
    setBusy(true);
    setError(null);
    try {
      await onUpload(selectedFolder, file);
      setUploading(false);
      setExpanded((prev) => new Set(prev).add(selectedFolder));
    } catch (err) {
      setError(err instanceof Error ? err.message : "Upload failed");
    } finally {
      setBusy(false);
      if (fileInputRef.current) fileInputRef.current.value = "";
    }
  }

  async function handleDelete(node: TreeNode) {
    const label = displayName(node.name);
    const ok = window.confirm(
      node.type === "folder"
        ? `Delete folder "${label}" and everything inside it?\n\nThis permanently removes all nested folders and CSV files.`
        : `Delete CSV "${label}"?\n\nOn-disk file: ${node.name}`,
    );
    if (!ok) return;

    setDeletingPath(node.path);
    setError(null);
    try {
      await onDelete(node);
    } catch (err) {
      setError(err instanceof Error ? err.message : "Delete failed");
    } finally {
      setDeletingPath(null);
    }
  }

  const dest = destinationLabel(selectedFolder);

  const [dragNode, setDragNode] = useState<TreeNode | null>(null);
  const [hint, setHint] = useState<Dnd["hint"]>(null);

  const dnd: Dnd = {
    // Reordering is disabled while searching (the tree is filtered then).
    enabled: !READ_ONLY && !query.trim(),
    dragPath: dragNode?.path ?? null,
    hint,
    canDropOn: (target) =>
      dragNode !== null &&
      target.path !== dragNode.path &&
      target.path !== "" &&
      target.type === dragNode.type &&
      parentPath(target.path) === parentPath(dragNode.path),
    start: (node) => setDragNode(node),
    over: (target, pos) =>
      setHint((prev) =>
        prev?.path === target.path && prev.pos === pos
          ? prev
          : { path: target.path, pos },
      ),
    clear: () => setHint((prev) => (prev === null ? prev : null)),
    drop: (target, pos) => {
      const dragged = dragNode;
      setDragNode(null);
      setHint(null);
      if (!dragged || !tree) return;
      const parent = findNode(tree, parentPath(dragged.path));
      if (!parent) return;
      const siblings = (parent.children ?? []).filter(
        (c) => c.type === dragged.type,
      );
      const names = siblings.map((c) => c.name).filter((n) => n !== dragged.name);
      const at = names.indexOf(target.name);
      if (at < 0) return;
      names.splice(pos === "before" ? at : at + 1, 0, dragged.name);
      const sameOrder = names.every((n, i) => n === siblings[i]?.name);
      if (sameOrder) return;
      const others = (parent.children ?? [])
        .filter((c) => c.type !== dragged.type)
        .map((c) => c.name);
      void onReorder(
        parent.path,
        dragged.type === "folder" ? [...names, ...others] : [...others, ...names],
      );
    },
    end: () => {
      setDragNode(null);
      setHint(null);
    },
  };

  return (
    <aside className="sidebar">
      <div className="sidebar-top">
        <div className="sidebar-search">
          <input
            type="search"
            placeholder="Search folders or CSVs"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
          {query && (
            <button
              type="button"
              className="search-clear"
              aria-label="Clear search"
              onClick={() => setQuery("")}
            >
              ×
            </button>
          )}
        </div>

        {!READ_ONLY && (
        <div className="sidebar-actions">
          <button
            type="button"
            className="btn btn-ghost sidebar-action"
            disabled={busy}
            onClick={() => {
              setCreating((v) => !v);
              setUploading(false);
            }}
          >
            <span aria-hidden>+</span> New folder
          </button>
          <button
            type="button"
            className="btn btn-ghost sidebar-action"
            disabled={busy}
            onClick={() => {
              setUploading((v) => !v);
              setCreating(false);
            }}
          >
            Upload CSV
          </button>
          <input
            ref={fileInputRef}
            type="file"
            accept=".csv,text/csv"
            hidden
            onChange={(e) => void handleUpload(e.target.files?.[0])}
          />
        </div>
        )}

        {onOpenCompare && (
          <button
            type="button"
            className={[
              "btn",
              "btn-ghost",
              "compare-nav",
              compareActive ? "is-active" : "",
            ].join(" ")}
            onClick={onOpenCompare}
          >
            <CompareIcon />
            Compare instances
          </button>
        )}

        {creating && (
          <div className="sidebar-create">
            <p className="sidebar-dest">Creating in {dest}</p>
            <div className="sidebar-create-row">
              <input
                type="text"
                placeholder="folder_name"
                value={newFolderName}
                onChange={(e) => setNewFolderName(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") void handleCreateFolder();
                }}
              />
              <button
                type="button"
                className="btn btn-primary"
                disabled={busy || !newFolderName.trim()}
                onClick={() => void handleCreateFolder()}
              >
                Create
              </button>
            </div>
          </div>
        )}

        {uploading && (
          <div className="sidebar-create">
            <p className="sidebar-dest">Upload to {dest}</p>
            <div className="sidebar-create-row">
              <button
                type="button"
                className="btn btn-primary"
                disabled={busy}
                onClick={() => fileInputRef.current?.click()}
              >
                Choose file
              </button>
              <button
                type="button"
                className="btn btn-ghost"
                onClick={() => setUploading(false)}
              >
                Cancel
              </button>
            </div>
          </div>
        )}

        {error && <div className="sidebar-error">{error}</div>}
      </div>

      <div
        className="sidebar-tree"
        onDragLeave={(e) => {
          if (!e.currentTarget.contains(e.relatedTarget as Node | null)) {
            setHint(null);
          }
        }}
      >
        {emptyResults && !query.trim() ? (
          <div className="sidebar-empty">
            No results yet. CSVs appear here after you run solvers.
          </div>
        ) : visibleTree ? (
          <TreeItem
            node={visibleTree}
            depth={0}
            query={query.trim()}
            selectedPath={selectedPath}
            selectedFolder={selectedFolder}
            compareActive={compareActive}
            expanded={expanded}
            deletingPath={deletingPath}
            onToggle={toggle}
            onSelectFile={onSelectFile}
            onSelectFolder={onSelectFolder}
            onSetTitle={onSetTitle}
            onDelete={(node) => void handleDelete(node)}
            dnd={dnd}
          />
        ) : (
          <div className="sidebar-empty">No matching folders or CSVs</div>
        )}
      </div>
    </aside>
  );
}

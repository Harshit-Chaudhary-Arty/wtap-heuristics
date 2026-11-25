import {
  flexRender,
  getCoreRowModel,
  getFilteredRowModel,
  getSortedRowModel,
  useReactTable,
  type ColumnDef,
  type SortingState,
} from "@tanstack/react-table";
import { useMemo, useState } from "react";
import type { CsvPayload } from "../types";

type DataTableProps = {
  data: CsvPayload;
};

export function DataTable({ data }: DataTableProps) {
  const [sorting, setSorting] = useState<SortingState>([]);
  const [globalFilter, setGlobalFilter] = useState("");

  const columns = useMemo<ColumnDef<Record<string, string>>[]>(
    () =>
      data.columns.map((column) => ({
        accessorKey: column,
        header: column,
        cell: (info) => {
          const value = info.getValue<string | undefined>();
          return value === undefined || value === "" ? "—" : String(value);
        },
      })),
    [data.columns],
  );

  const table = useReactTable({
    data: data.rows,
    columns,
    state: {
      sorting,
      globalFilter,
    },
    onSortingChange: setSorting,
    onGlobalFilterChange: setGlobalFilter,
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
    getFilteredRowModel: getFilteredRowModel(),
  });

  return (
    <div className="table-shell">
      <div className="table-toolbar">
        <div className="toolbar-left">
          <div className="search-field">
            <span aria-hidden>⌕</span>
            <input
              type="search"
              placeholder="Filter rows"
              value={globalFilter}
              onChange={(e) => setGlobalFilter(e.target.value)}
            />
          </div>
        </div>
      </div>

      <div className="table-scroll">
        <table className="data-table">
          <thead>
            {table.getHeaderGroups().map((headerGroup) => (
              <tr key={headerGroup.id}>
                {headerGroup.headers.map((header) => (
                  <th key={header.id}>
                    {header.isPlaceholder ? null : (
                      <button
                        type="button"
                        className="th-button"
                        onClick={header.column.getToggleSortingHandler()}
                      >
                        {flexRender(
                          header.column.columnDef.header,
                          header.getContext(),
                        )}
                        <span className="sort-mark">
                          {{
                            asc: " ↑",
                            desc: " ↓",
                          }[header.column.getIsSorted() as string] ?? ""}
                        </span>
                      </button>
                    )}
                  </th>
                ))}
              </tr>
            ))}
          </thead>
          <tbody>
            {table.getRowModel().rows.length === 0 ? (
              <tr>
                <td colSpan={Math.max(columns.length, 1)} className="empty-cell">
                  No rows match this search.
                </td>
              </tr>
            ) : (
              table.getRowModel().rows.map((row) => (
                <tr key={row.id}>
                  {row.getVisibleCells().map((cell) => (
                    <td key={cell.id} title={String(cell.getValue() ?? "")}>
                      {flexRender(cell.column.columnDef.cell, cell.getContext())}
                    </td>
                  ))}
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}

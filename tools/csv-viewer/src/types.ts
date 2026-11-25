export type TreeNode = {
  name: string;
  path: string;
  type: "folder" | "file";
  title?: string;
  children?: TreeNode[];
};

export type CsvPayload = {
  path: string;
  name: string;
  columns: string[];
  rows: Record<string, string>[];
  rowCount: number;
  columnCount: number;
};

export type CompareAlgo = {
  id: string;
  label: string;
  path: string;
};

export type CompareCell = {
  objective: number | null;
  time: number | null;
};

export type CompareRow = {
  instance: number;
  weapons: number | null;
  targets: number | null;
  values: Record<string, CompareCell>;
};

export type ComparePayload = {
  algorithms: CompareAlgo[];
  rows: CompareRow[];
};

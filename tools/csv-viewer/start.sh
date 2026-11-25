#!/usr/bin/env bash
# Local CSV Results Viewer (Supabase-style)
#
# Usage:
#   bash tools/csv-viewer/start.sh
#
# Then open:
#   http://127.0.0.1:5173

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
cd "$DIR"

if [[ ! -d node_modules ]]; then
  npm install
fi

mkdir -p "$ROOT/results"
echo "Starting CSV Results Viewer..."
echo "UI:  http://127.0.0.1:5173"
echo "API: http://127.0.0.1:8787"
echo "Data root: $ROOT/results"
echo "Press Ctrl+C to stop."

exec npm run dev

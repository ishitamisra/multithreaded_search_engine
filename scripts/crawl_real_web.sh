#!/usr/bin/env bash
# Crawl the REAL internet (not the offline local demo site), index the
# result, compute PageRank, and serve a browser search UI whose results
# link to genuine http(s):// pages -- so clicking a result actually opens
# the real page, unlike file:// links from a locally-indexed folder,
# which most browsers block from an http:// page for security reasons.
#
# Usage:
#   ./scripts/crawl_real_web.sh [seeds_file] [output_dir] [max_pages] [port]
#
# Defaults to data/real_web_seeds.txt, ./crawl_data/real_web, 15 pages,
# port 8080. Re-running with the same output_dir resumes/extends a
# previous crawl instead of starting over (see Crawler restartability in
# README.md).
set -euo pipefail
cd "$(dirname "$0")/.."

SEEDS_FILE="${1:-data/real_web_seeds.txt}"
OUTPUT_DIR="${2:-crawl_data/real_web}"
MAX_PAGES="${3:-15}"
PORT="${4:-8080}"

BUILD_DIR=build
if [ ! -d "$BUILD_DIR" ]; then
  cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

echo "=============================================================="
echo "Crawling the real web from seeds in $SEEDS_FILE"
echo "(politeness delay: 2s/host, robots.txt honored, max $MAX_PAGES pages)"
echo "=============================================================="
"$BUILD_DIR/crawl" "$SEEDS_FILE" "$OUTPUT_DIR" \
  --max-pages "$MAX_PAGES" --threads 2 --delay 2.0

echo
echo "=============================================================="
echo "Indexing crawled pages and computing PageRank"
echo "=============================================================="
"$BUILD_DIR/build_index" "$OUTPUT_DIR" "$OUTPUT_DIR/index" --threads 4
"$BUILD_DIR/pagerank_tool" "$OUTPUT_DIR/index" --iterations 30

echo
echo "=============================================================="
echo "Starting the search UI -- results link to the real crawled pages"
echo "=============================================================="
echo "Open http://localhost:$PORT in your browser once this prints "
echo "'Serving search UI on ...'. Press Ctrl+C here to stop the server."
echo
exec "$BUILD_DIR/serve" "$OUTPUT_DIR/index" "$PORT"

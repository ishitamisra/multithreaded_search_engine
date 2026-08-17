#!/usr/bin/env bash
# End-to-end demo of the whole pipeline. Run from the repository root:
#   ./scripts/demo.sh
#
# Part 1 indexes the local sample_corpus/ (Levels 1, 3, 5, 6) and runs a
# few example queries. Part 2 spins up a tiny local HTTP site, crawls it
# with the real multithreaded crawler (Level 2), indexes the crawl output,
# computes PageRank (Level 7.5) over the discovered link graph, and shows
# how the resulting scores affect ranking -- all without touching the
# public internet, so this works in any sandboxed environment.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=build
if [ ! -d "$BUILD_DIR" ]; then
  cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD_DIR" -j"$(nproc)"

WORK_DIR=$(mktemp -d)
SITE_PID=""
cleanup() {
  [ -n "$SITE_PID" ] && kill "$SITE_PID" 2>/dev/null || true
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

echo "=============================================================="
echo "Part 1: index the local sample corpus and run some queries"
echo "=============================================================="
"$BUILD_DIR/build_index" data/sample_corpus "$WORK_DIR/corpus_index" --threads 4
for q in "cats AND dogs" "\"reverse word index\"" "cats -training" "threads OR ranker"; do
  echo
  echo "--- query: $q ---"
  "$BUILD_DIR/query" "$WORK_DIR/corpus_index" "$q"
done

echo
echo "=============================================================="
echo "Part 2: crawl a local demo site, index it, compute PageRank"
echo "=============================================================="
PORT=8090
(cd data/crawl_target_site && python3 -m http.server "$PORT" --bind 127.0.0.1 \
  >"$WORK_DIR/site_server.log" 2>&1 &)
sleep 1
SITE_PID=$(lsof -t -i:"$PORT" 2>/dev/null | head -1 || true)

"$BUILD_DIR/crawl" data/seeds.txt "$WORK_DIR/crawl" --max-pages 10 --threads 4 --delay 0.05
echo
echo "--- crawl manifest ---"
cat "$WORK_DIR/crawl/manifest.tsv"

"$BUILD_DIR/build_index" "$WORK_DIR/crawl" "$WORK_DIR/crawl_index" --threads 4
"$BUILD_DIR/pagerank_tool" "$WORK_DIR/crawl_index" --iterations 30 --threads 2

echo
echo "--- query the crawled index (scores now include PageRank) ---"
"$BUILD_DIR/query" "$WORK_DIR/crawl_index" "multithreading OR crawler"

echo
echo "Demo complete. To browse results in the HTTP UI, run:"
echo "  $BUILD_DIR/serve $WORK_DIR/crawl_index 8080"
echo "(the index above lives in a temp dir that is deleted when this script exits --"
echo " rerun with your own --keep-work-dir workflow, or just build_index somewhere durable.)"

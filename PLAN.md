# Project Plan

This follows Level 0 of the course project plan ("Create a plan"). It
documents the decisions the plan asks for and maps the seven
implementation levels to concrete deliverables and milestones. A real
6-person team would adapt the "suggested owner" column to actual names;
it's included here to show how the pieces divide across people.

## 1. What information do we collect?

Per crawled page: the raw HTML bytes, the resolved URL, the page title,
the plain-text content (HTML-stripped), and the outgoing links (URL +
anchor text). See `manifest.tsv` written by the crawler and consumed by
the indexer (`README.md` → "Design decisions → Outlink resolution").

## 2. Do we keep a copy of every document we crawl?

Yes. Raw page bytes are stored under `<crawl_dir>/pages/<id>.html`. This
keeps snippet generation and re-indexing (e.g. with different
stopword/stemming settings) possible without re-crawling.

## 3. Storage estimate

At demo scale (tens to low thousands of pages) this is negligible: a
crawled page averages a few KB of HTML, the on-disk index adds roughly
one lexicon entry per distinct word (~15-30 bytes) plus 8 bytes per
posting plus 4 bytes per word occurrence for positions. For a corpus of
N pages averaging W words each, expect index size on the rough order of
`N * W * 12` bytes for postings, dominated by position lists -- the
biggest lever for shrinking it is dropping per-position storage for
words that will never be phrase-queried, which this project does not do
(the "additional statistics/encoding" tradeoff called out in Level 3.2).

## 4. Problem breakdown, what each piece does, and LOC

| Piece | What it does | Suggested owner | Approx. LOC |
|---|---|---|---|
| Tokenizer + HTML parser | Text → normalized tokens; strip HTML, extract title/links | Person A | ~350 |
| Hash table + Indexer | In-memory hash table; parallel directory-to-index build | Person B | ~350 |
| On-disk index format | `.docs`/`.lex`/`.post` writer + streaming reader | Person C | ~450 |
| Crawler + robots.txt | Multithreaded, polite, restartable fetch loop | Person D | ~450 |
| Stream readers + query parser | AND/OR/NOT/PHRASE constraint solver, recursive-descent parser | Person E | ~600 |
| Ranker + PageRank + snippets | TF-IDF, static score, PageRank, excerpt generation | Person F | ~350 |
| CLI tools + HTTP server | 5 executables, POSIX-socket HTTP server, search UI | shared | ~700 |
| Thread pool | Shared concurrency primitive used by every piece above | shared, built first | ~90 |

(Actual counts in this implementation are close to this table; see `wc
-l src/*.cpp include/engine/*.h` for exact numbers.)

## 5. How we work as a team

- **Source control**: one repository, one branch per feature, PRs
  reviewed before merging into `main`.
- **Common library**: `include/engine/` + `src/` build into a single
  static library (`searchengine`) that every CLI tool and test links
  against, so the five tools and the test suite never duplicate logic.
- **Interfaces first**: the module table above corresponds 1:1 to header
  files with documented responsibilities (see `README.md` → Modules),
  so pieces can be built and unit-tested independently before wiring
  them together end-to-end.
- **Integration point**: `scripts/demo.sh` is the standing end-to-end
  check that all pieces still work together after any change.

## 6. Deliverables and milestones

1. **Level 1** -- tokenizer, HTML parser, hash table, directory indexer.
   *Done when*: `build_index` on a local corpus produces a hash table
   you can look up a word in and list its documents.
2. **Level 2** -- crawler with frontier, dedup, robots.txt, restart.
   *Done when*: `crawl` run twice on the same output directory fetches
   each page exactly once across both runs.
3. **Level 3** -- on-disk index format, writer, reader.
   *Done when*: an index written by `build_index` can be reloaded and
   answer "which documents contain word X" without re-reading the whole
   corpus.
4. **Level 4** -- CLI and HTTP UI.
   *Done when*: `query` (REPL) and `serve` (browser) both return
   title/URL/snippet for a query.
5. **Level 5** -- stream readers and query parser.
   *Done when*: AND/OR/NOT/phrase queries with parentheses all return
   correct result sets (see `tests/test_stream_readers.cpp`,
   `tests/test_query_parser.cpp`).
6. **Level 6** -- ranker.
   *Done when*: for an OR query, a document matching more query terms
   ranks above one matching fewer (see the ranker check in
   `tests/test_query_parser.cpp`).
7. **Level 7** -- snippets, anchor text, PageRank (see `README.md` →
   "What's intentionally not implemented" for the stretch goals we
   scoped out and why).

## Milestone verification for this implementation

`ctest --test-dir build` and `scripts/demo.sh` (see `README.md` → Quick
start) exercise every level above against real data; both currently
pass.

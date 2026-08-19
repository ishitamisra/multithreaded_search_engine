# Multithreaded Search Engine (C++)

A  working internet search engine built in C++17: includes
a multithreaded crawler, a hash-table-based indexer, an on-disk reverse
index, a boolean query parser/constraint solver, a relevance ranker
(TF-IDF + PageRank + heuristics), a command-line UI, and an HTTP UI.


![Search results for "ucla computer science" against a real crawl of cs.ucla.edu, showing highlighted matches, snippets, and relevance scores](docs/search-ui.png)

## Quick start

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure   # unit tests

./scripts/demo.sh   # builds, indexes a sample corpus, crawls a local
                     # demo site, computes PageRank, and runs queries --
                     # a complete tour of the pipeline in one command
```

Requires a C++17 compiler, CMake 3.16+, and POSIX threads. `libcurl`
(`libcurl4-openssl-dev` on Debian/Ubuntu, or `brew install curl` on macOS)
is optional but required for the crawler to actually fetch pages over the
network. Without it the crawler still builds and runs, it just can't
fetch anything (this is detected and reported clearly at both build and
run time).

### Search the real web (clickable results)

`scripts/demo.sh` indexes a local sample corpus for the CLI-query part of
its tour, and a locally-served demo site for the crawler part. Both
run with no internet access, which is why `demo.sh` is safe to use in a
sandboxed CI environment. But it means those results' links are either
`file://` URLs (which most browsers block from navigating to when
clicked from an `http://` page, for security reasons) or `http://
127.0.0.1:.../` URLs that stop working once the demo's local server
exits.

To get an index whose results are genuine, permanently clickable web
pages, crawl the real internet instead:

```sh
./scripts/crawl_real_web.sh
```

This crawls a small, real, crawler-friendly site (see
`data/real_web_seeds.txt`), indexes it, computes PageRank, and starts
the HTTP UI -- open `http://localhost:8080` and every result links to a
real page on the actual internet. Edit `data/real_web_seeds.txt` to
crawl a different site (respect robots.txt and use a reasonable
`--delay`; the script defaults to 2 seconds per host). Optional
arguments: `./scripts/crawl_real_web.sh [seeds_file] [output_dir]
[max_pages] [port]`.

## Architecture

```
include/engine/     public headers, one per module (see below)
src/                 implementations
tools/               five CLI executables (see "Command-line tools")
tests/               unit tests, run via ctest
data/sample_corpus/  a small local text corpus for the indexer/query demo
data/crawl_target_site/  a tiny site the crawler can fetch over HTTP
                         locally, so the crawl demo needs no internet
data/seeds.txt       seed URL list for the crawler
scripts/demo.sh      end-to-end pipeline demo
```

Data flows through five independent stages, each with its own module and
its own CLI tool, matching how a real search engine is actually built and
operated:

```
crawl ──> pages/ + manifest.tsv ──> build_index ──> foo.{docs,lex,post}
                                                          │
                                          pagerank_tool ──┤ (optional)
                                                          │
                                  query / serve  <────────┘
```

- **`crawl`** fetches pages from the web (or resumes a previous crawl)
  and writes raw pages plus a manifest of URLs/titles/links.
- **`build_index`** walks *any* directory of documents -- crawler output
  or a hand-written local corpus -- tokenizes it in parallel, and writes
  an on-disk reverse index.
- **`pagerank_tool`** (optional) computes PageRank over the link graph
  baked into an index and attaches the scores as a sidecar file.
- **`query`** and **`serve`** load an index and answer boolean queries,
  ranked by relevance.

### Modules

| Header | Responsibility |
|---|---|
| `common.h` | `DocId`, `Posting`, `DocumentMeta` value types |
| `threadpool.h` | Generic thread pool (`std::thread` + task queue) |
| `tokenizer.h` | Text -> normalized tokens; optional stopwords/stemming |
| `html_parser.h` | Tag-stripping HTML scanner: text, title, links |
| `hashtable.h` | Hand-rolled separate-chaining hash table |
| `indexer.h` | Directory walker -> parallel tokenize -> in-memory index |
| `index_format.h` / `index_writer.h` / `index_reader.h` | On-disk index: format, writer, streaming reader |
| `stream_reader.h` | AND/OR/NOT/PHRASE constraint solver over posting streams |
| `query_parser.h` | Recursive-descent query language -> `StreamReader` tree |
| `ranker.h` | TF-IDF + static score (PageRank) + title/URL bonuses |
| `robots.h` | robots.txt parser (Allow/Disallow/Crawl-delay) |
| `crawler.h` | Multithreaded, polite, restartable crawler |
| `pagerank.h` | Parallel power-iteration PageRank over the link graph |
| `snippet.h` | Best-window excerpt generation + HTML highlighting |
| `http_server.h` | Minimal POSIX-socket HTTP/1.1 server |

### Command-line tools

```sh
# Index local corpus (plain text or HTML) -- 
build/build_index data/sample_corpus /tmp/myindex --threads 4 [--stopwords] [--stem]

# Query it -- 
build/query /tmp/myindex
build/query /tmp/myindex 'cats AND dogs'
build/query /tmp/myindex '"exact phrase" OR (fast -slow)'

# Serve a browser UI 
build/serve /tmp/myindex 8080     # http://localhost:8080

# Crawl the web 
build/crawl data/seeds.txt /tmp/mycrawl --max-pages 50 --threads 4
build/build_index /tmp/mycrawl /tmp/myindex   # note: the crawl OUTPUT DIR,
                                               # not its pages/ subfolder --
                                               # that's where manifest.tsv lives

# Compute PageRank over index's link graph 
build/pagerank_tool /tmp/myindex --iterations 30
# (query/serve automatically pick up /tmp/myindex.pagerank on next load)
```

Grammar (top-down recursive descent, `query_parser.cpp`):

```
Query   := OrExpr
OrExpr  := AndExpr ("OR" AndExpr)*
AndExpr := AndTerm (["AND"] AndTerm)*      -- juxtaposition == AND
AndTerm := ["NOT" | "-"] Primary
Primary := WORD | "phrase words" | "(" OrExpr ")"
```

A query compiles to a tree of `StreamReader`s (`TermStream`, `AndStream`,
`OrStream`, `NotStream`, `PhraseStream`), each a merge-join cursor over
sorted DocId streams -- the classic inverted-index intersection
algorithm, and the "constraint solver" the assignment asks for. Boolean
matching (which documents match) is deliberately separate from ranking
(`ranker.h`, run afterward only on the matched set) -- the same
separation real search engines use.

## Design decisions

- **On-disk index format** (`index_format.h`): three files per index --
  `.docs` (document table, loaded fully into RAM), `.lex` (word -> byte
  offset/count into `.post`, also loaded fully into RAM -- the classic
  "keep the dictionary in memory, stream postings from disk" design),
  and `.post` (postings themselves, streamed lazily via
  `PostingsStreamReader` so query-time memory doesn't scale with corpus
  size).
- **In-memory index** (`hashtable.h`): a hand-written separate-chaining
  hash table, per Level 1's "parse text files into a hash table" -- not
  a wrapper around `std::unordered_map`.
- **Crawler restartability** (`crawler.h`): the frontier and seen-URL set
  are checkpointed to `frontier.txt`/`seen.txt` in the output directory
  on every run and reloaded on the next one, so `crawl` can be re-run
  with a larger `--max-pages` to pick up where it left off.
- **Crawler politeness**: per-host robots.txt is fetched and honored
  (`robots.h`), and a per-host minimum delay (`--delay`, or the site's
  own `Crawl-delay` if larger) is enforced before every fetch to that
  host.
- **Outlink resolution**: the crawler records each page's outgoing
  *URLs* in `manifest.tsv` (not filenames -- a link's target usually
  hasn't been fetched, let alone named, yet). `Indexer::build()`
  resolves URLs to `DocId`s only after every crawled page has a stable
  id, which is also what feeds the PageRank link graph.
- **Ranking** (`ranker.h`): TF-IDF (term frequency normalized by
  document length, times inverse document frequency) plus a static
  page-quality term (PageRank, if computed) plus heuristic bonuses for
  query terms found in the title or URL.
- **Parallelism**: the indexer tokenizes files across a thread pool and
  merges per-thread hash tables; the crawler runs a configurable number
  of fetch worker threads sharing one frontier; PageRank's power
  iteration splits the per-document score update across a thread pool
  each round (each iteration depends only on the previous, read-only,
  score vector, so this parallelizes cleanly); the HTTP server hands
  each connection to a thread pool.

## Advanced Functionality / Add-ons


- **7.1/7.2 Gradient descent / ML ranking** -- would need a labeled
  training set that doesn't exist for a from-scratch demo corpus.
- **7.6 Distributed crawling/indexing/query** -- this codebase
  parallelizes with threads on a single machine.
- **7.7 PDF indexing** -- `indexer.h` only handles plain text and HTML. 
  Extending it means adding a PDF-to-text extraction step ahead of
  tokenization (e.g. shelling out to `pdftotext`).

## Testing

`ctest --test-dir build` runs unit tests covering the tokenizer, HTML
parser, hash table, thread pool, index write/read round-tripping, the
AND/OR/NOT/PHRASE stream readers, and the query parser (including error
cases like unbalanced parens and all-negated clauses). `scripts/demo.sh`
is the end-to-end smoke test: it tests every tool against real data,
including a (local) HTTP crawl.

## Limitations

- The HTTP server is HTTP/1.1 without keep-alive, or
  TLS.
- The crawler's URL-priority strategy is plain BFS (nearer to the seeds
  first); it does not include content-quality-based frontier prioritization.

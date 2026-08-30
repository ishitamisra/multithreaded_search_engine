# Multithreaded Search Engine (C++)

A search engine built from scratch in C++17: a multithreaded crawler, a
hash-table-based indexer, a boolean query parser, and a TF-IDF + PageRank
ranker, with a CLI and a browser UI.

![Search results for "ucla computer science" against a real crawl of cs.ucla.edu, showing highlighted matches, snippets, and relevance scores](docs/search-ui.png)

## Running it

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

./scripts/demo.sh   # builds, indexes a sample corpus, crawls a local
                     # demo site, computes PageRank, and runs queries
```

For results that link to real, permanently reachable web pages instead
of the offline demo data, crawl the actual internet:

```sh
./scripts/crawl_real_web.sh
```

Then open `http://localhost:8080`.

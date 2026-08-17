// CLI: serve <index_prefix> [port]
//
// Level 4.2/4.3: a small HTTP wrapper UI around the query engine. Serves
// a search form at "/" and renders results (title, clickable URL,
// snippet, score) at "/search?q=...". Also exposes "/api/search?q=..."
// returning JSON, for programmatic use.

#include <fstream>
#include <iostream>
#include <sstream>

#include "engine/html_parser.h"
#include "engine/http_server.h"
#include "engine/index_reader.h"
#include "engine/query_parser.h"
#include "engine/ranker.h"
#include "engine/snippet.h"

using namespace engine;

namespace {

std::string readFileToString(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool looksLikeHtml(const std::string& path) {
  return path.size() >= 5 &&
         (path.compare(path.size() - 5, 5, ".html") == 0 ||
          (path.size() >= 4 && path.compare(path.size() - 4, 4, ".htm") == 0));
}

std::string htmlEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

const char* kPageHeader = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>Multithreaded Search Engine</title>
<style>
body { font-family: system-ui, sans-serif; max-width: 700px; margin: 2rem auto; padding: 0 1rem; color: #1a1a1a; }
h1 { font-size: 1.4rem; }
form { display: flex; gap: 0.5rem; margin-bottom: 1.5rem; }
input[type=text] { flex: 1; padding: 0.5rem; font-size: 1rem; }
button { padding: 0.5rem 1rem; }
.result { margin-bottom: 1.2rem; }
.result a { font-size: 1.05rem; text-decoration: none; color: #1a4fba; }
.result .url { color: #2a7d2a; font-size: 0.85rem; }
.result .snippet { color: #444; font-size: 0.92rem; }
.result .score { color: #999; font-size: 0.8rem; }
mark { background: #fff2a8; }
.meta { color: #777; font-size: 0.85rem; margin-bottom: 1rem; }
</style></head><body>
<h1>Multithreaded Search Engine</h1>
)HTML";

class SearchApp {
 public:
  explicit SearchApp(const std::string& indexPrefix) : reader_(indexPrefix) {}

  HttpResponse handle(const HttpRequest& req) {
    if (req.path == "/") return renderHome();
    if (req.path == "/search") return renderSearch(req);
    if (req.path == "/api/search") return apiSearch(req);
    return HttpResponse{404, "text/plain", "not found"};
  }

 private:
  HttpResponse renderHome() {
    std::ostringstream body;
    body << kPageHeader;
    body << "<div class=\"meta\">" << reader_.numDocuments() << " documents indexed</div>";
    body << "<form action=\"/search\" method=\"get\">"
            "<input type=\"text\" name=\"q\" placeholder=\"cats AND dogs, \\\"exact phrase\\\", -spam\" autofocus>"
            "<button type=\"submit\">Search</button></form>";
    body << "</body></html>";
    return HttpResponse{200, "text/html; charset=utf-8", body.str()};
  }

  struct QueryOutcome {
    bool ok = false;
    std::string error;
    QueryParser::ParsedQuery parsed;
    std::vector<RankedResult> results;
    size_t totalMatches = 0;
  };

  QueryOutcome runQuery(const std::string& q) {
    QueryOutcome outcome;
    if (q.find_first_not_of(" \t") == std::string::npos) {
      outcome.error = "empty query";
      return outcome;
    }
    QueryParser parser(reader_);
    try {
      outcome.parsed = parser.parse(q);
    } catch (const QueryParseError& e) {
      outcome.error = e.what();
      return outcome;
    }
    std::vector<DocId> matches;
    for (auto& stream = outcome.parsed.stream; stream->valid(); stream->next()) {
      matches.push_back(stream->docId());
    }
    outcome.totalMatches = matches.size();
    Ranker ranker(reader_);
    outcome.results = ranker.rank(outcome.parsed.terms, matches, 20);
    outcome.ok = true;
    return outcome;
  }

  HttpResponse renderSearch(const HttpRequest& req) {
    auto it = req.query.find("q");
    std::string q = it == req.query.end() ? "" : it->second;
    QueryOutcome outcome = runQuery(q);

    std::ostringstream body;
    body << kPageHeader;
    body << "<form action=\"/search\" method=\"get\">"
            "<input type=\"text\" name=\"q\" value=\"" << htmlEscape(q) << "\" autofocus>"
            "<button type=\"submit\">Search</button></form>";

    if (!outcome.ok) {
      body << "<p>Error: " << htmlEscape(outcome.error) << "</p>";
    } else {
      body << "<div class=\"meta\">" << outcome.totalMatches << " match(es)</div>";
      SnippetGenerator snippetGen;
      for (const auto& r : outcome.results) {
        const DocumentMeta* doc = reader_.getDocument(r.docId);
        if (!doc) continue;
        std::string raw = readFileToString(doc->path);
        std::string bodyText = looksLikeHtml(doc->path) ? parseHtml(raw, doc->url).text : raw;
        std::string snippet = snippetGen.generate(bodyText, outcome.parsed.terms);

        body << "<div class=\"result\">";
        body << "<div><a href=\"" << htmlEscape(doc->url) << "\">"
             << htmlEscape(doc->title.empty() ? doc->url : doc->title) << "</a></div>";
        body << "<div class=\"url\">" << htmlEscape(doc->url) << "</div>";
        body << "<div class=\"snippet\">" << SnippetGenerator::highlightHtml(snippet, outcome.parsed.terms)
             << "</div>";
        body << "<div class=\"score\">score " << r.score << "</div>";
        body << "</div>";
      }
    }
    body << "</body></html>";
    return HttpResponse{200, "text/html; charset=utf-8", body.str()};
  }

  HttpResponse apiSearch(const HttpRequest& req) {
    auto it = req.query.find("q");
    std::string q = it == req.query.end() ? "" : it->second;
    QueryOutcome outcome = runQuery(q);

    std::ostringstream json;
    if (!outcome.ok) {
      json << "{\"error\":\"" << jsonEscape(outcome.error) << "\"}";
      return HttpResponse{400, "application/json", json.str()};
    }
    json << "{\"totalMatches\":" << outcome.totalMatches << ",\"results\":[";
    bool first = true;
    for (const auto& r : outcome.results) {
      const DocumentMeta* doc = reader_.getDocument(r.docId);
      if (!doc) continue;
      if (!first) json << ",";
      first = false;
      json << "{\"title\":\"" << jsonEscape(doc->title) << "\",\"url\":\""
           << jsonEscape(doc->url) << "\",\"score\":" << r.score << "}";
    }
    json << "]}";
    return HttpResponse{200, "application/json", json.str()};
  }

  IndexReader reader_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <index_prefix> [port]\n";
    return 1;
  }
  std::string prefix = argv[1];
  int port = argc > 2 ? std::stoi(argv[2]) : 8080;

  try {
    SearchApp app(prefix);
    HttpServer server(port, 8);
    server.setHandler([&app](const HttpRequest& req) { return app.handle(req); });
    std::cout << "Serving search UI on http://0.0.0.0:" << port << "\n";
    server.run();
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

#pragma once
// Strips HTML down to plain text while pulling out the two things the
// crawler and indexer need most: the page title and the outgoing links
// (href + anchor text). This is intentionally a tolerant, regex-free
// hand-written scanner rather than a full HTML5 parser -- real-world pages
// are frequently malformed and a strict parser would choke on them.

#include <string>
#include <vector>

namespace engine {

struct Link {
  std::string url;         // resolved (absolute) target URL
  std::string anchorText;  // visible text of the <a> tag
};

struct ParsedHtml {
  std::string title;
  std::string text;  // visible text content, tags/scripts/styles stripped
  std::vector<Link> links;
};

// Resolves `href` against `baseUrl` per (a simplified subset of) RFC 3986:
// handles absolute URLs, protocol-relative (`//host/path`), root-relative
// (`/path`) and simple relative (`path`) forms. Returns "" for schemes we
// don't want to crawl (javascript:, mailto:, tel:, #fragment-only).
std::string resolveUrl(const std::string& baseUrl, const std::string& href);

ParsedHtml parseHtml(const std::string& html, const std::string& baseUrl);

}  // namespace engine

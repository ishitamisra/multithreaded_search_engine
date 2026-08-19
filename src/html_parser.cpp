#include "engine/html_parser.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace engine {

namespace {

std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::string decodeEntities(const std::string& in) {
  static const std::unordered_map<std::string, std::string> kEntities = {
      {"amp", "&"},  {"lt", "<"},   {"gt", ">"},    {"quot", "\""},
      {"apos", "'"}, {"nbsp", " "}, {"mdash", "-"}, {"ndash", "-"},
      {"rsquo", "'"}, {"lsquo", "'"}, {"ldquo", "\""}, {"rdquo", "\""},
  };
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    if (in[i] == '&') {
      size_t semi = in.find(';', i);
      if (semi != std::string::npos && semi - i <= 10) {
        std::string entity = in.substr(i + 1, semi - i - 1);
        if (!entity.empty() && entity[0] == '#') {
          // numeric entity, e.g. &#39;
          try {
            int code = entity[1] == 'x' || entity[1] == 'X'
                           ? std::stoi(entity.substr(2), nullptr, 16)
                           : std::stoi(entity.substr(1));
            if (code > 0 && code < 128) {
              out.push_back(static_cast<char>(code));
              i = semi + 1;
              continue;
            }
          } catch (...) {
            // fall through: treat literally
          }
        } else {
          auto it = kEntities.find(toLowerCopy(entity));
          if (it != kEntities.end()) {
            out += it->second;
            i = semi + 1;
            continue;
          }
        }
      }
    }
    out.push_back(in[i]);
    ++i;
  }
  return out;
}

// Extracts the value of `attr` from a raw tag body (text between < and >),
// handling both quoted and unquoted attribute values.
std::string extractAttr(const std::string& tagBody, const std::string& attr) {
  std::string needle = toLowerCopy(attr) + "=";
  std::string lowerBody = toLowerCopy(tagBody);
  size_t pos = lowerBody.find(needle);
  while (pos != std::string::npos) {
    // ensure it's a standalone attribute, not a suffix of another name
    bool boundaryOk = pos == 0 || std::isspace(static_cast<unsigned char>(
                                       tagBody[pos - 1]));
    if (boundaryOk) {
      size_t valueStart = pos + needle.size();
      if (valueStart < tagBody.size()) {
        char quote = tagBody[valueStart];
        if (quote == '"' || quote == '\'') {
          size_t end = tagBody.find(quote, valueStart + 1);
          if (end != std::string::npos) {
            return tagBody.substr(valueStart + 1, end - valueStart - 1);
          }
        } else {
          size_t end = valueStart;
          while (end < tagBody.size() &&
                 !std::isspace(static_cast<unsigned char>(tagBody[end])) &&
                 tagBody[end] != '>') {
            ++end;
          }
          return tagBody.substr(valueStart, end - valueStart);
        }
      }
    }
    pos = lowerBody.find(needle, pos + needle.size());
  }
  return "";
}

}  // namespace

std::string resolveUrl(const std::string& baseUrl, const std::string& href) {
  if (href.empty()) return "";
  std::string h = href;
  // Trim whitespace.
  while (!h.empty() && std::isspace(static_cast<unsigned char>(h.front())))
    h.erase(h.begin());
  while (!h.empty() && std::isspace(static_cast<unsigned char>(h.back())))
    h.pop_back();
  // A fragment identifies a position within a page, not a different
  // resource (RFC 3986 §3.5) -- strip it so "page.html" and
  // "page.html#section" dedup to the same URL instead of being fetched
  // (and indexed) as if they were two different pages.
  size_t fragmentPos = h.find('#');
  if (fragmentPos != std::string::npos) h.erase(fragmentPos);
  if (h.empty()) return "";

  std::string lower = toLowerCopy(h);
  if (lower.rfind("javascript:", 0) == 0 || lower.rfind("mailto:", 0) == 0 ||
      lower.rfind("tel:", 0) == 0 || lower.rfind("data:", 0) == 0) {
    return "";
  }
  if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0) {
    return h;
  }

  size_t schemeEnd = baseUrl.find("://");
  if (schemeEnd == std::string::npos) return "";  // no usable base
  std::string scheme = baseUrl.substr(0, schemeEnd);
  size_t hostStart = schemeEnd + 3;
  size_t pathStart = baseUrl.find('/', hostStart);
  std::string host =
      pathStart == std::string::npos ? baseUrl.substr(hostStart)
                                      : baseUrl.substr(hostStart, pathStart - hostStart);

  if (h.rfind("//", 0) == 0) {
    return scheme + ":" + h;
  }
  if (h[0] == '/') {
    return scheme + "://" + host + h;
  }
  // relative path: resolve against the directory of the base path.
  std::string basePath =
      pathStart == std::string::npos ? "/" : baseUrl.substr(pathStart);
  size_t lastSlash = basePath.find_last_of('/');
  std::string baseDir =
      lastSlash == std::string::npos ? "/" : basePath.substr(0, lastSlash + 1);
  return scheme + "://" + host + baseDir + h;
}

ParsedHtml parseHtml(const std::string& html, const std::string& baseUrl) {
  ParsedHtml result;
  std::string text;
  text.reserve(html.size());

  size_t i = 0;
  const size_t n = html.size();
  std::string currentAnchorHref;
  std::string currentAnchorText;
  bool inAnchor = false;
  bool titleCaptured = false;
  bool inTitle = false;

  while (i < n) {
    if (html[i] == '<') {
      size_t tagEnd = html.find('>', i);
      if (tagEnd == std::string::npos) break;
      std::string tagBody = html.substr(i + 1, tagEnd - i - 1);
      i = tagEnd + 1;
      if (tagBody.empty()) continue;

      bool closing = tagBody[0] == '/';
      std::string nameOnly = closing ? tagBody.substr(1) : tagBody;
      size_t nameEnd = 0;
      while (nameEnd < nameOnly.size() &&
             (std::isalnum(static_cast<unsigned char>(nameOnly[nameEnd])) ||
              nameOnly[nameEnd] == '-')) {
        ++nameEnd;
      }
      std::string tagName = toLowerCopy(nameOnly.substr(0, nameEnd));

      if (tagName == "script" || tagName == "style") {
        std::string closeTag = "</" + tagName;
        size_t closePos = toLowerCopy(html).find(closeTag, i);
        i = closePos == std::string::npos ? n : html.find('>', closePos) + 1;
        continue;
      }
      if (tagName == "title") {
        inTitle = !closing;
        continue;
      }
      if (tagName == "a") {
        if (!closing) {
          inAnchor = true;
          currentAnchorHref = extractAttr(tagBody, "href");
          currentAnchorText.clear();
        } else if (inAnchor) {
          inAnchor = false;
          std::string resolved = resolveUrl(baseUrl, currentAnchorHref);
          if (!resolved.empty()) {
            Link link;
            link.url = resolved;
            link.anchorText = decodeEntities(currentAnchorText);
            result.links.push_back(std::move(link));
          }
        }
        continue;
      }
      // any other tag: treat as whitespace boundary
      text.push_back(' ');
      if (inAnchor) currentAnchorText.push_back(' ');
      continue;
    }
    char c = html[i++];
    if (inTitle && !titleCaptured) {
      result.title.push_back(c);
    }
    text.push_back(c);
    if (inAnchor) currentAnchorText.push_back(c);
  }
  if (!result.title.empty()) titleCaptured = true;

  result.text = decodeEntities(text);
  result.title = decodeEntities(result.title);
  // collapse leading/trailing whitespace on title
  size_t start = result.title.find_first_not_of(" \t\r\n");
  size_t end = result.title.find_last_not_of(" \t\r\n");
  result.title = (start == std::string::npos)
                     ? ""
                     : result.title.substr(start, end - start + 1);
  return result;
}

}  // namespace engine

#include "engine/html_parser.h"
#include "test_framework.h"

using namespace engine;

TEST_MAIN_BEGIN()
  std::string html =
      "<html><head><title>My Page</title><style>.x{color:red}</style></head>"
      "<body><h1>Hello</h1><p>Some <b>bold</b> text with a "
      "<a href=\"/about.html\">link</a> and "
      "<a href=\"https://other.example/page\">external</a>.</p>"
      "<script>var x = 1;</script></body></html>";
  ParsedHtml parsed = parseHtml(html, "https://example.com/dir/index.html");

  CHECK_EQ(parsed.title, std::string("My Page"));
  CHECK(parsed.text.find("color:red") == std::string::npos);
  CHECK(parsed.text.find("var x") == std::string::npos);
  CHECK(parsed.text.find("Hello") != std::string::npos);
  CHECK(parsed.text.find("bold") != std::string::npos);
  CHECK(parsed.text.find("text with a") != std::string::npos);

  CHECK_EQ(parsed.links.size(), (size_t)2);
  CHECK_EQ(parsed.links[0].url, std::string("https://example.com/about.html"));
  CHECK_EQ(parsed.links[0].anchorText, std::string("link"));
  CHECK_EQ(parsed.links[1].url, std::string("https://other.example/page"));

  CHECK_EQ(resolveUrl("https://example.com/a/b.html", "c.html"),
           std::string("https://example.com/a/c.html"));
  CHECK_EQ(resolveUrl("https://example.com/a/b.html", "/root.html"),
           std::string("https://example.com/root.html"));
  CHECK_EQ(resolveUrl("https://example.com/a/b.html", "https://x.com/y"),
           std::string("https://x.com/y"));
  CHECK_EQ(resolveUrl("https://example.com/a/b.html", "#frag"), std::string(""));
  CHECK_EQ(resolveUrl("https://example.com/a/b.html", "javascript:void(0)"),
           std::string(""));
TEST_MAIN_END()

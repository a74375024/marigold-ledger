// The streaming XML tokenizer: every construct, in chunks of any size.
#include <string>

#include "core/guide/xml.hpp"
#include "doctest.h"

using namespace marigold::guide::xml;

namespace {

// Writes events as "<name a=v>", "</name>" and "[text]" (adjacent text pieces
// joined, so chunking doesn't show).
struct Trace : Handler {
  std::string out;
  std::string pending;
  bool cdataSeen = false;
  void flush() {
    if (!pending.empty()) out += "[" + pending + "]";
    pending.clear();
  }
  void startElement(std::string_view name, const Attributes& attributes) override {
    flush();
    out += "<" + std::string(name);
    for (const Attribute& a : attributes.all()) out += " " + a.name + "=" + a.value;
    out += ">";
  }
  void endElement(std::string_view name) override {
    flush();
    out += "</" + std::string(name) + ">";
  }
  void characters(std::string_view text) override { pending += text; }
  void cdata(std::string_view text) override {
    cdataSeen = true;
    pending += text;
  }
};

std::string run(const std::string& xml, size_t chunk, Tokenizer* tok = nullptr) {
  Tokenizer local;
  Tokenizer& t = tok ? *tok : local;
  Trace trace;
  for (size_t i = 0; i < xml.size(); i += chunk)
    t.feed(std::string_view(xml).substr(i, chunk), trace);
  t.finish(trace);
  trace.flush();
  return trace.out;
}

void sameInEveryChunking(const std::string& xml, const std::string& expected) {
  for (size_t chunk : {size_t(1), size_t(2), size_t(3), size_t(5), size_t(7), size_t(13),
                       size_t(64), xml.size() + 1}) {
    CAPTURE(chunk);
    CHECK(run(xml, chunk) == expected);
  }
}

}  // namespace

TEST_CASE("elements, attributes, text and entities") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<tv a='1' b=\"x &amp; y\"><title lang=\"en\">Tom &amp; Jerry &lt;3 &#233;&#xE9; "
      "&quot;q&apos;</title>"
      "<empty/><e2 k = \"v\" /></tv>";
  sameInEveryChunking(xml,
                      "[\n]<tv a=1 b=x & y><title lang=en>[Tom & Jerry <3 éé \"q']</title>"
                      "<empty></empty><e2 k=v></e2></tv>");
}

TEST_CASE("comments, CDATA, declarations and processing instructions") {
  std::string xml =
      "\xEF\xBB\xBF<?xml version=\"1.0\"?><!DOCTYPE tv SYSTEM \"xmltv.dtd\" [ <!ENTITY x \"y>\"> ]>"
      "<tv><!-- <title>not a title</title> -- still comment --><title><![CDATA[News & "
      "<Weather>]]></title>"
      "<?app ignore me?><x>a<!--c-->b</x></tv>";
  sameInEveryChunking(xml, "<tv><title>[News & <Weather>]</title><x>[ab]</x></tv>");
}

TEST_CASE("unknown and malformed references are kept as written") {
  sameInEveryChunking("<t>&nbsp; &#0; &#xD800; &#xZZ; & alone &amp</t>",
                      "<t>[&nbsp; &#0; &#xD800; &#xZZ; & alone &amp]</t>");
}

TEST_CASE("attribute values: whitespace normalized, then references decoded") {
  sameInEveryChunking("<a v=\"one\ttwo\nthree&#10;four\"/>", "<a v=one two three\nfour></a>");
}

TEST_CASE("ISO-8859-1 and windows-1252 files are read as UTF-8") {
  std::string latin = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><t>Caf\xE9 \xD1</t>";
  sameInEveryChunking(latin, "<t>[Café Ñ]</t>");
  std::string cp1252 = "<?xml version='1.0' encoding='windows-1252'?><t>\x93quoted\x94 \x80</t>";
  sameInEveryChunking(cp1252, "<t>[“quoted” €]</t>");
}

TEST_CASE("an over-long element is skipped whole") {
  std::string big(200, 'x');
  std::string xml = "<tv><keep>1</keep><huge a=\"" + big +
                    "\"><inner>no</inner><huge>nested</huge>"
                    "text</huge><huge2 a=\"" +
                    big + "\"/><keep>2</keep></tv>";
  for (size_t chunk : {size_t(1), size_t(7), size_t(64), xml.size()}) {
    CAPTURE(chunk);
    Tokenizer tok(100);
    Trace trace;
    for (size_t i = 0; i < xml.size(); i += chunk)
      tok.feed(std::string_view(xml).substr(i, chunk), trace);
    tok.finish(trace);
    trace.flush();
    CHECK(trace.out == "<tv><keep>[1]</keep><keep>[2]</keep></tv>");
    CHECK(tok.skippedElements() == 2);
  }
}

TEST_CASE("bounded memory: long text and comments stream through") {
  Tokenizer tok;
  Trace trace;
  tok.feed("<t>", trace);
  std::string piece(1000, 'a');
  for (int i = 0; i < 1000; ++i) tok.feed(piece, trace);  // 1 MB of text
  tok.feed("</t><!--", trace);
  for (int i = 0; i < 1000; ++i) tok.feed(piece, trace);  // 1 MB of comment
  tok.feed("--><end/>", trace);
  tok.finish(trace);
  trace.flush();
  CHECK(trace.out.size() == 3 + 1 + 1000000 + 1 + 4 + 11);
  CHECK(trace.out.substr(trace.out.size() - 16) == "]</t><end></end>");
  CHECK(tok.bytesFed() == 3 + 1000000 + 8 + 1000000 + 9);
}

TEST_CASE("truncated input") {
  Tokenizer tok;
  CHECK(run("<tv><t>abc</t><unfinished a=\"", 3, &tok) == "<tv><t>[abc]</t>");
  CHECK(tok.errors() == 1);
  CHECK(run("<tv>tail &amp", 4) == "<tv>[tail &amp]");
  CHECK(run("", 1).empty());
  CHECK(run("<", 1).empty());
}

#include <format>
#include <string>
#include <doctest/doctest.h>
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto TEXT_ID = *parse_node_id("09efb50d-acb7-47a2-a45e-5017439ce00e");

  const Text& import_text(Document& document, const std::string& ranges,
      const std::string& text, ImportReport* report = nullptr) {
    auto result = import_nodes(R"([{"type": "text",
      "id": "09efb50d-acb7-47a2-a45e-5017439ce00e",
      "meta": {"ux": {"singleTextObject": true, "rangedStyles": )" + ranges +
      R"(}}, "style": {"fill": {"type": "solid", "color": {"mode": "RGB",
      "value": {"r": 255, "g": 255, "b": 255}}}, "font": {
      "postscriptName": "Roboto-Regular", "family": "Roboto",
      "style": "Regular", "size": 12}}, "text": )" + text + "}]", document);
    if(report) {
      *report = std::move(result);
    }
    return *document.get(TEXT_ID).get_text();
  }

  std::string make_range(int length, const std::string& style = "Regular",
      const std::string& fill = "4294967295") {
    return R"({"length": )" + std::to_string(length) + R"(, "linkId": "",
      "fontFamily": "Roboto", "fontStyle": ")" + style + R"(",
      "postscriptName": "Roboto-)" + style + R"(", "fontSize": 12,
      "charSpacing": 0, "underline": false, "textTransform": "none",
      "textScript": "none", "fill": {"value": )" + fill + R"(},
      "strikethrough": false, "hyperlinkUrl": ""})";
  }

  std::vector<std::size_t> get_lengths(const Text& text) {
    auto lengths = std::vector<std::size_t>();
    for(auto& run : text.m_runs) {
      lengths.push_back(run.m_length);
    }
    return lengths;
  }
}

TEST_SUITE("TextImporter") {
  TEST_CASE("content") {
    auto document = Document();
    auto report = ImportReport();
    auto& text = import_text(document, "[" + make_range(4) + "]",
      R"({"frame": {"type": "positioned"}, "rawText": "a\u00e9\ud83d\ude00"})",
      &report);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(*text.m_content == u"a\u00e9\U0001F600");
    REQUIRE(text.m_content->size() == 4);
    REQUIRE(std::holds_alternative<PositionedFrame>(text.m_frame));
    REQUIRE(!text.m_layout);
    REQUIRE(text.m_base_style.m_font ==
      Font("Roboto", "Regular", "Roboto-Regular", 12));
    REQUIRE(text.m_base_style.m_color == Color(1, 1, 1, 1));
    REQUIRE(std::get<SolidFill>(text.m_style.m_fill).m_color ==
      Color(1, 1, 1, 1));
    REQUIRE(!text.m_is_flipped_vertically);
  }

  TEST_CASE("flip") {
    auto document = Document();
    auto report = ImportReport();
    auto& text = import_text(document,
      "[" + make_range(6) + R"(], "flipY": true)",
      R"({"frame": {"type": "autoHeight", "width": 90},
      "rawText": "<Body>"})", &report);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(text.m_is_flipped_vertically);
  }

  TEST_CASE("frames") {
    auto document = Document();
    auto& area = import_text(document, "[]",
      R"({"frame": {"type": "area", "width": 733, "height": 78},
      "rawText": ""})");
    REQUIRE(area.m_frame == TextFrame(AreaFrame(Size(733, 78))));
    auto other = Document();
    auto& automatic = import_text(other, "[]",
      R"({"frame": {"type": "autoHeight", "width": 279}, "rawText": ""})");
    REQUIRE(automatic.m_frame == TextFrame(AutoHeightFrame(279)));
  }

  TEST_CASE("runs") {
    auto document = Document();
    auto report = ImportReport();
    auto& text = import_text(document, "[" + make_range(2, "Bold") +
      R"(, {"length": 2, "fontFamily": "Roboto",
      "fontStyle": "Regular", "fontSize": 14, "charSpacing": 10,
      "underline": true, "strikethrough": true, "textTransform": "uppercase",
      "textScript": "superscript", "fill": {"value": 4285025223}}])",
      R"({"frame": {"type": "positioned"}, "rawText": "Last Q"})", &report);
    REQUIRE(get_lengths(text) == std::vector<std::size_t>({2, 4}));
    auto& first = text.m_runs[0].m_style;
    REQUIRE(first.m_font.m_postscript_name == "Roboto-Bold");
    REQUIRE(first.m_color == Color(1, 1, 1, 1));
    auto& second = text.m_runs[1].m_style;
    REQUIRE(second.m_font == Font("Roboto", "Regular", "Roboto-Regular", 14));
    REQUIRE(second.m_letter_spacing == 10);
    REQUIRE(second.m_is_underlined);
    REQUIRE(second.m_is_struck_through);
    REQUIRE(second.m_transform == TextTransform::UPPERCASE);
    REQUIRE(second.m_script == TextScript::SUPERSCRIPT);
    REQUIRE(second.m_color == make_argb_color(0xFF684BC7));
    REQUIRE(text.m_runs[1].m_character_style.is_nil());
  }

  TEST_CASE("run_lengths") {
    auto lengths = [] (const std::string& ranges, const char* content) {
      auto document = Document();
      return get_lengths(import_text(document, ranges,
        std::string(R"({"frame": {"type": "positioned"}, "rawText": ")") +
        content + "\"}"));
    };
    REQUIRE(lengths("[" + make_range(16) + "]", "body") ==
      std::vector<std::size_t>({4}));
    REQUIRE(lengths("[" + make_range(1) + "]", "Status") ==
      std::vector<std::size_t>({6}));
    REQUIRE(lengths("[" + make_range(0) + "," + make_range(18) + "]",
      "DurationBox") == std::vector<std::size_t>({11}));
    REQUIRE(lengths("[" + make_range(3) + "," + make_range(5) + "," +
      make_range(2) + "]", "abcd") == std::vector<std::size_t>({3, 1}));
    REQUIRE(lengths("[" + make_range(0) + "]", "").empty());
  }

  TEST_CASE("paragraph_style") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "text",
      "id": "09efb50d-acb7-47a2-a45e-5017439ce00e", "style": {"font": {
      "family": "Roboto", "style": "Regular", "size": 18},
      "textAttributes": {"lineHeight": 24, "paragraphAfterSpacing": 12,
      "paragraphAlign": "right", "letterSpacing": -10}},
      "text": {"frame": {"type": "positioned"}, "rawText": "x"}}])",
      document);
    REQUIRE(report.m_unconsumed.empty());
    auto& text = *document.get(TEXT_ID).get_text();
    REQUIRE(text.m_paragraph_style == ParagraphStyle(TextAlignment::RIGHT,
      24, 12));
    REQUIRE(text.m_base_style.m_letter_spacing == -10);
    REQUIRE(text.m_runs.empty());
  }

  TEST_CASE("layout") {
    auto document = Document();
    auto report = ImportReport();
    auto& text = import_text(document, "[" + make_range(2, "Bold",
      "4285025223") + "," + make_range(4) + "]",
      R"({"frame": {"type": "area", "width": 100, "height": 40},
      "paragraphs": [{"lines": [[{"x": 0, "y": 11, "from": 0, "to": 2,
      "style": {"font": {"postscriptName": "Roboto-Bold", "family": "Roboto",
      "style": "Bold"}, "fill": {"type": "solid", "color": {"mode": "RGB",
      "value": {"r": 104, "g": 75, "b": 199}}}}}, {"y": 11, "from": 2,
      "to": 4}, {"y": 11, "from": 4, "to": 6}]]}, {"lines": [[{"x": 5,
      "y": 25, "from": 7, "to": 8, "style": {"font": {
      "postscriptName": "Helvetica", "family": "Helvetica",
      "style": "Regular"}, "textAttributes": {"decoration": []}}}]]}],
      "rawText": "Last Q\nZ"})", &report);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(text.m_layout);
    auto& paragraphs = (*text.m_layout)->m_paragraphs;
    REQUIRE(paragraphs.size() == 2);
    REQUIRE(paragraphs[0].m_lines.size() == 1);
    REQUIRE(paragraphs[0].m_lines[0].m_runs == std::vector<StoredRun>({
      StoredRun(0, 6, Point(0, 11), std::nullopt, std::nullopt)}));
    REQUIRE(paragraphs[1].m_lines[0].m_runs == std::vector<StoredRun>({
      StoredRun(7, 8, Point(5, 25), std::nullopt, std::nullopt)}));
  }

  TEST_CASE("script_layout") {
    auto document = Document();
    auto report = ImportReport();
    auto range = make_range(1);
    auto none = std::string(R"("textScript": "none")");
    range.replace(range.find(none), none.size(),
      R"("textScript": "superscript")");
    auto shift = std::format(
      "{}", static_cast<float>(12 * 0.5833) / 0.5833 * 0.3333);
    auto& text = import_text(document, "[" + make_range(1) + "," + range + "]",
      R"({"frame": {"type": "positioned"}, "paragraphs": [{"lines": [[{"x": 0,
      "y": 11, "from": 0, "to": 1}, {"y": 11, "from": 1, "to": 2, "style": {
      "textAttributes": {"verticalAlign": )" + shift + R"(}}}]]}],
      "rawText": "AB"})", &report);
    REQUIRE(text.m_runs.back().m_style.m_script == TextScript::SUPERSCRIPT);
    REQUIRE(report.m_unconsumed.empty());
  }

  TEST_CASE("character_style_link") {
    auto package = XdTestPackage();
    package.m_resources = parse_json(R"({"documentLibrary": {"elements": [
      {"id": "c7210564-0a10-4afa-ae89-55e964bc5147",
      "type": "application/vnd.adobe.element.characterstyle+dcx",
      "name": "Body", "representations": [{"content": {"fontFamily": "Roboto",
      "fontStyle": "Regular", "fontSize": 12}}]}]}})");
    package.m_pasteboard = parse_json_array(R"([{"type": "text",
      "id": "09efb50d-acb7-47a2-a45e-5017439ce00e", "meta": {"ux": {
      "rangedStyles": [{"length": 1,
      "linkId": "c7210564-0a10-4afa-ae89-55e964bc5147"}]}},
      "text": {"rawText": "x"}}])");
    auto document = Document();
    import_package(package, document);
    auto& text = *document.get(TEXT_ID).get_text();
    REQUIRE(text.m_runs.front().m_character_style ==
      *parse_node_id("c7210564-0a10-4afa-ae89-55e964bc5147"));
  }

  TEST_CASE("hyperlink") {
    auto document = Document();
    auto report = ImportReport();
    auto& text = import_text(document, R"([{"length": 1,
      "fontFamily": "Roboto", "hyperlinkUrl": "https://example.com"},
      {"length": 1, "hyperlinkUrl": ""}])", R"({"rawText": "xy"})", &report);
    REQUIRE(!report.m_unconsumed.contains(
      "<text>.meta.ux.rangedStyles[].hyperlinkUrl:str"));
    REQUIRE(text.m_runs.size() == 2);
    REQUIRE(text.m_runs.front().m_hyperlink == "https://example.com");
    REQUIRE(text.m_runs.back().m_hyperlink.empty());
  }
}

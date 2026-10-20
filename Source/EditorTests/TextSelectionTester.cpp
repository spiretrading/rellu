#include <doctest/doctest.h>
#include "Rellu/Editor/TextSelection.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("TextSelection") {
  TEST_CASE("pending_style") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = TextSelection(document);
    auto count = 0;
    auto subscription = selection.subscribe([&] { ++count; });
    auto style = document.get(scene.m_text).get_text()->m_base_style;
    style.m_font.m_size = 40;
    selection.set_pending_style(style);
    REQUIRE(!selection.get_pending_style());
    REQUIRE(count == 0);
    selection.set(scene.m_text, 2, 2);
    REQUIRE(count == 1);
    selection.set_pending_style(style);
    REQUIRE(selection.get_pending_style() == style);
    REQUIRE(count == 2);
    selection.set_pending_style(style);
    REQUIRE(count == 2);
    selection.set(scene.m_text, 2, 2);
    REQUIRE(selection.get_pending_style() == style);
    selection.set(scene.m_text, 3, 3);
    REQUIRE(!selection.get_pending_style());
    REQUIRE(count == 3);
    selection.set_pending_style(style);
    selection.clear();
    REQUIRE(!selection.get_pending_style());
  }

  TEST_CASE("pending_hyperlink") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = TextSelection(document);
    auto count = 0;
    auto subscription = selection.subscribe([&] { ++count; });
    selection.set_pending_hyperlink("https://example.com");
    REQUIRE(!selection.get_pending_hyperlink());
    selection.set(scene.m_text, 2, 2);
    selection.set_pending_hyperlink("https://example.com");
    REQUIRE(selection.get_pending_hyperlink() ==
      std::optional<std::string>("https://example.com"));
    REQUIRE(count == 2);
    selection.set(scene.m_text, 3, 3);
    REQUIRE(!selection.get_pending_hyperlink());
  }

  TEST_CASE("last_style") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = TextSelection(document);
    REQUIRE(selection.get_last_style() == get_default_text_style());
    REQUIRE(selection.get_last_paragraph_style() == ParagraphStyle());
    auto style = document.get(scene.m_text).get_text()->m_base_style;
    auto paragraph = ParagraphStyle();
    paragraph.m_paragraph_spacing = 20;
    selection.set_last_style(style, paragraph);
    selection.clear();
    REQUIRE(selection.get_last_style() == style);
    REQUIRE(selection.get_last_paragraph_style() == paragraph);
  }

  TEST_CASE("remember") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = TextSelection(document);
    REQUIRE(selection.is_last_fill_enabled());
    REQUIRE(!selection.get_last_border());
    auto text = *document.get(scene.m_text).get_text();
    text.m_base_style.m_font.m_size = 30;
    text.m_runs.clear();
    text.m_paragraph_style.m_paragraph_spacing = 8;
    text.m_style.m_fill = disable_fill(SolidFill(Color(1, 0, 0, 1)));
    text.m_style.m_stroke = Stroke(Color(0, 0, 1, 1), 4,
      StrokeAlignment::INSIDE);
    text.m_style.m_stroke->m_is_enabled = false;
    selection.remember(text);
    REQUIRE(selection.get_last_style() == text.m_base_style);
    REQUIRE(selection.get_last_paragraph_style().m_paragraph_spacing == 8);
    REQUIRE(!selection.is_last_fill_enabled());
    auto border = Stroke(Color(0, 0, 1, 1), 4);
    border.m_is_enabled = false;
    REQUIRE(selection.get_last_border() == border);
    text.m_style.m_fill = SolidFill(Color(1, 0, 0, 1));
    text.m_style.m_stroke = std::nullopt;
    selection.remember_appearance(text);
    REQUIRE(selection.is_last_fill_enabled());
    REQUIRE(selection.get_last_border() == border);
  }
}

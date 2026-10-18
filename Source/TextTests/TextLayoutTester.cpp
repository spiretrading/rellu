#include <algorithm>
#include <cmath>
#include <set>
#include <doctest/doctest.h>
#include "Rellu/Text/TextLayout.hpp"

using namespace Rellu;

namespace {
  const FontLibrary& get_library() {
    static auto library = FontLibrary(get_system_font_directories());
    return library;
  }

  const Font FONT = Font("Arial", "Regular", "ArialMT", 20);

  Text make_text(std::u16string content) {
    auto text = Text();
    text.m_content = Shared(std::move(content));
    text.m_base_style.m_font = FONT;
    return text;
  }

  const TypefaceMetrics& get_metrics() {
    return get_library().resolve(FONT).m_typeface->get_metrics();
  }

  double get_ascent() {
    return std::floor(get_metrics().m_line_ascent * FONT.m_size + 0.5);
  }

  double get_descent() {
    return std::floor(get_metrics().m_line_descent * FONT.m_size + 0.5);
  }

  std::set<double> get_baselines(const TextLayout& layout) {
    auto baselines = std::set<double>();
    for(auto& glyph : layout.m_glyphs) {
      baselines.insert(glyph.m_origin.m_y);
    }
    return baselines;
  }

  double get_right(const TextLayout& layout) {
    auto right = -1e9;
    for(auto& glyph : layout.m_glyphs) {
      auto bounds = get_bounds(glyph.m_typeface->get_outline(glyph.m_index));
      right = std::max(
        right, glyph.m_origin.m_x + bounds.get_right() * glyph.m_size);
    }
    return right;
  }

  StoredRun make_run(std::size_t from, std::size_t to, Point position) {
    return StoredRun(from, to, position, std::nullopt, std::nullopt);
  }
}

TEST_SUITE("TextLayout") {
  TEST_CASE("transforms") {
    REQUIRE(apply_transform(u"Hello World", TextTransform::NONE) ==
      u"Hello World");
    REQUIRE(apply_transform(u"Hello World", TextTransform::UPPERCASE) ==
      u"HELLO WORLD");
    REQUIRE(apply_transform(u"Hello World", TextTransform::LOWERCASE) ==
      u"hello world");
    REQUIRE(apply_transform(u"hello wide world", TextTransform::TITLE_CASE) ==
      u"Hello Wide World");
    REQUIRE(apply_transform(u"stra\u00dfe", TextTransform::UPPERCASE) ==
      u"STRASSE");
  }

  TEST_CASE("single_line") {
    auto layout = lay_out(make_text(u"Hi you"), get_library());
    REQUIRE(layout.m_glyphs.size() == 5);
    REQUIRE(get_baselines(layout) == std::set<double>({0}));
    REQUIRE(layout.m_glyphs.front().m_origin.m_x == 0);
    REQUIRE(layout.m_glyphs.front().m_size == FONT.m_size);
    REQUIRE(layout.m_decorations.empty());
    REQUIRE(!layout.m_clip);
  }

  TEST_CASE("stored_layout") {
    auto text = make_text(u"Hello\nWorld");
    auto stored = StoredLayout();
    stored.m_paragraphs.resize(2);
    stored.m_paragraphs[0].m_lines.push_back(
      StoredLine({make_run(0, 5, Point(0, 0))}));
    stored.m_paragraphs[1].m_lines.push_back(
      StoredLine({make_run(6, 11, Point(0, 30))}));
    text.m_layout = Shared(stored);
    auto layout = lay_out(text, get_library());
    REQUIRE(layout.m_glyphs.size() == 10);
    REQUIRE(get_baselines(layout) ==
      std::set<double>({0, get_descent() + get_ascent()}));
    text.m_frame = AreaFrame(Size(200, 100));
    layout = lay_out(text, get_library());
    REQUIRE(get_baselines(layout) == std::set<double>(
      {get_ascent(), 2 * get_ascent() + get_descent()}));
    REQUIRE(layout.m_clip == Rect(0, 0, 200, 100));
  }

  TEST_CASE("stored_line_breaks") {
    auto text = make_text(u"one two three");
    text.m_frame = AreaFrame(Size(1000, 100));
    auto stored = StoredLayout();
    auto& paragraph = stored.m_paragraphs.emplace_back();
    paragraph.m_lines.push_back(StoredLine({make_run(0, 4, Point(0, 0))}));
    paragraph.m_lines.push_back(StoredLine({make_run(4, 13, Point(0, 0))}));
    text.m_layout = Shared(stored);
    REQUIRE(get_baselines(lay_out(text, get_library())).size() == 2);
    text.m_layout = std::nullopt;
    REQUIRE(get_baselines(lay_out(text, get_library())).size() == 1);
  }

  TEST_CASE("stored_overrides") {
    auto text = make_text(u"Hello");
    auto stored = StoredLayout();
    auto run = make_run(0, 5, Point(0, 0));
    auto font = FONT;
    font.m_size = 40;
    run.m_font = font;
    run.m_color = make_color(255, 0, 0);
    stored.m_paragraphs.emplace_back().m_lines.push_back(StoredLine({run}));
    text.m_layout = Shared(stored);
    auto layout = lay_out(text, get_library());
    REQUIRE(layout.m_glyphs.size() == 5);
    for(auto& glyph : layout.m_glyphs) {
      REQUIRE(glyph.m_size == 40);
      REQUIRE(glyph.m_color == make_color(255, 0, 0));
    }
  }

  TEST_CASE("wrapping") {
    auto text = make_text(u"alpha beta gamma delta epsilon");
    text.m_frame = AutoHeightFrame(120);
    auto layout = lay_out(text, get_library());
    auto baselines = get_baselines(layout);
    REQUIRE(baselines.size() > 1);
    REQUIRE(*baselines.begin() == get_ascent());
    REQUIRE(get_right(layout) <= 120);
    REQUIRE(layout.m_glyphs.size() == 26);
  }

  TEST_CASE("line_height") {
    auto text = make_text(u"a\nb\nc");
    text.m_paragraph_style.m_line_height = 33;
    text.m_paragraph_style.m_paragraph_spacing = 2;
    REQUIRE(get_baselines(lay_out(text, get_library())) ==
      std::set<double>({0, 35, 70}));
  }

  TEST_CASE("alignment") {
    auto text = make_text(u"Right");
    text.m_frame = AreaFrame(Size(300, 50));
    text.m_paragraph_style.m_alignment = TextAlignment::RIGHT;
    auto right = lay_out(text, get_library());
    REQUIRE(get_right(right) <= 300);
    REQUIRE(get_right(right) > 295);
    text.m_paragraph_style.m_alignment = TextAlignment::CENTER;
    auto center = lay_out(text, get_library());
    REQUIRE(center.m_glyphs.front().m_origin.m_x ==
      doctest::Approx(right.m_glyphs.front().m_origin.m_x / 2));
  }

  TEST_CASE("trailing_whitespace") {
    auto text = make_text(u"ab ab");
    text.m_frame = AutoHeightFrame(300);
    text.m_paragraph_style.m_alignment = TextAlignment::RIGHT;
    auto stored = StoredLayout();
    auto& paragraph = stored.m_paragraphs.emplace_back();
    paragraph.m_lines.push_back(StoredLine({make_run(0, 3, Point(0, 0))}));
    paragraph.m_lines.push_back(StoredLine({make_run(3, 5, Point(0, 0))}));
    text.m_layout = Shared(stored);
    auto wrapped = lay_out(text, get_library());
    REQUIRE(wrapped.m_glyphs.size() == 4);
    REQUIRE(wrapped.m_glyphs[0].m_origin.m_x ==
      doctest::Approx(wrapped.m_glyphs[2].m_origin.m_x));
    auto spaced = make_text(u"ab ");
    spaced.m_paragraph_style.m_alignment = TextAlignment::RIGHT;
    auto plain = make_text(u"ab");
    plain.m_paragraph_style.m_alignment = TextAlignment::RIGHT;
    REQUIRE(lay_out(spaced, get_library()).m_glyphs[0].m_origin.m_x <
      lay_out(plain, get_library()).m_glyphs[0].m_origin.m_x);
  }

  TEST_CASE("letter_spacing") {
    auto text = make_text(u"abc");
    auto plain = lay_out(text, get_library());
    text.m_base_style.m_letter_spacing = 100;
    auto spaced = lay_out(text, get_library());
    for(auto i = std::size_t(0); i != 3; ++i) {
      REQUIRE(spaced.m_glyphs[i].m_origin.m_x == doctest::Approx(
        plain.m_glyphs[i].m_origin.m_x + 2.0 * i));
    }
  }

  TEST_CASE("tab_stops") {
    auto layout = lay_out(make_text(u"a\tb\t\tc"), get_library());
    REQUIRE(layout.m_glyphs.size() == 3);
    REQUIRE(layout.m_glyphs[0].m_origin.m_x == 0);
    REQUIRE(layout.m_glyphs[1].m_origin.m_x ==
      doctest::Approx(4 * FONT.m_size));
    REQUIRE(layout.m_glyphs[2].m_origin.m_x ==
      doctest::Approx(12 * FONT.m_size));
  }

  TEST_CASE("text_transform") {
    auto text = make_text(u"abc");
    text.m_base_style.m_transform = TextTransform::UPPERCASE;
    auto upper = lay_out(text, get_library());
    auto expected = lay_out(make_text(u"ABC"), get_library());
    REQUIRE(upper.m_glyphs.size() == expected.m_glyphs.size());
    for(auto i = std::size_t(0); i != upper.m_glyphs.size(); ++i) {
      REQUIRE(upper.m_glyphs[i].m_index == expected.m_glyphs[i].m_index);
      REQUIRE(upper.m_glyphs[i].m_origin == expected.m_glyphs[i].m_origin);
    }
  }

  TEST_CASE("decorations") {
    auto text = make_text(u"line");
    text.m_base_style.m_is_underlined = true;
    text.m_base_style.m_is_struck_through = true;
    auto layout = lay_out(text, get_library());
    REQUIRE(layout.m_decorations.size() == 2);
    auto& metrics = get_metrics();
    auto& underline = layout.m_decorations[0].m_rect;
    REQUIRE(underline.m_x == 0);
    REQUIRE(underline.m_y ==
      doctest::Approx(-metrics.m_underline_offset * FONT.m_size));
    REQUIRE(underline.m_height ==
      doctest::Approx(metrics.m_underline_thickness * FONT.m_size));
    REQUIRE(underline.m_width > 0);
    auto& strikethrough = layout.m_decorations[1].m_rect;
    REQUIRE(strikethrough.m_y ==
      doctest::Approx(-metrics.m_strikeout_offset * FONT.m_size));
    REQUIRE(strikethrough.m_width == underline.m_width);
  }

  TEST_CASE("superscript") {
    auto text = make_text(u"x");
    text.m_base_style.m_script = TextScript::SUPERSCRIPT;
    auto layout = lay_out(text, get_library());
    auto& metrics = get_metrics();
    REQUIRE(layout.m_glyphs[0].m_origin.m_y ==
      doctest::Approx(-metrics.m_superscript_offset * FONT.m_size));
    REQUIRE(layout.m_glyphs[0].m_size ==
      doctest::Approx(metrics.m_superscript_size * FONT.m_size));
  }

  TEST_CASE("fallback_emboldening") {
    auto text = make_text(u"a➔b");
    auto regular = lay_out(text, get_library());
    text.m_base_style.m_font = Font("Arial", "Bold", "Arial-BoldMT", 20);
    auto bold = lay_out(text, get_library());
    REQUIRE(regular.m_glyphs.size() == 3);
    REQUIRE(bold.m_glyphs.size() == 3);
    auto& typeface = *bold.m_glyphs[1].m_typeface;
    REQUIRE(&typeface == regular.m_glyphs[1].m_typeface.get());
    auto advance = typeface.shape(u"➔", 0, 1).front().m_advance;
    auto units = typeface.get_units_per_em();
    auto get_advance = [] (const TextLayout& layout) {
      return layout.m_glyphs[2].m_origin.m_x - layout.m_glyphs[1].m_origin.m_x;
    };
    REQUIRE(get_advance(regular) == doctest::Approx(advance * 20));
    REQUIRE(get_advance(bold) ==
      doctest::Approx((advance + std::round(units / 50.0) / units) * 20));
  }

  TEST_CASE("color_glyph") {
    auto layout = lay_out(make_text(u"✅"), get_library());
    REQUIRE(layout.m_glyphs.size() == 1);
    auto& glyph = layout.m_glyphs.front();
    REQUIRE(!glyph.m_typeface->get_color_layers(glyph.m_index).empty());
    auto fills = outline(layout);
    REQUIRE(fills.size() > 1);
    for(auto& fill : fills) {
      REQUIRE(!fill.m_path.is_empty());
    }
    REQUIRE(fills.front().m_color != Color(0, 0, 0, 1));
  }

  TEST_CASE("fallback_line_metrics") {
    auto plain = make_text(u"a");
    plain.m_frame = AutoHeightFrame(200);
    auto fallback = make_text(u"a✅");
    fallback.m_frame = AutoHeightFrame(200);
    auto emoji = lay_out(fallback, get_library());
    auto& typeface = *emoji.m_glyphs[1].m_typeface;
    REQUIRE(emoji.m_glyphs[0].m_origin.m_y == std::max(get_ascent(),
      std::floor(typeface.get_metrics().m_line_ascent * FONT.m_size + 0.5)));
    REQUIRE(emoji.m_glyphs[0].m_origin.m_y >
      lay_out(plain, get_library()).m_glyphs[0].m_origin.m_y);
  }

  TEST_CASE("runs") {
    auto text = make_text(u"redblue");
    text.m_runs.push_back(TextRun(3, text.m_base_style));
    text.m_runs.back().m_style.m_color = make_color(255, 0, 0);
    text.m_runs.push_back(TextRun(4, text.m_base_style));
    text.m_runs.back().m_style.m_color = make_color(0, 0, 255);
    auto layout = lay_out(text, get_library());
    REQUIRE(layout.m_glyphs.size() == 7);
    REQUIRE(layout.m_glyphs[2].m_color == make_color(255, 0, 0));
    REQUIRE(layout.m_glyphs[3].m_color == make_color(0, 0, 255));
    auto fills = outline(layout);
    REQUIRE(fills.size() == 2);
    REQUIRE(fills[0].m_color == make_color(255, 0, 0));
    REQUIRE(fills[1].m_color == make_color(0, 0, 255));
    REQUIRE(!fills[0].m_path.is_empty());
  }

  TEST_CASE("kerning_across_runs") {
    auto whole = lay_out(make_text(u"AVAV"), get_library());
    auto text = make_text(u"AVAV");
    text.m_runs.push_back(TextRun(1, text.m_base_style));
    text.m_runs.back().m_style.m_color = make_color(255, 0, 0);
    text.m_runs.push_back(TextRun(3, text.m_base_style));
    auto split = lay_out(text, get_library());
    REQUIRE(split.m_glyphs.size() == 4);
    for(auto i = std::size_t(0); i != 4; ++i) {
      REQUIRE(std::abs(split.m_glyphs[i].m_origin.m_x -
        whole.m_glyphs[i].m_origin.m_x) < 1e-9);
    }
    auto resized = make_text(u"AVAV");
    resized.m_runs.push_back(TextRun(1, resized.m_base_style));
    resized.m_runs.push_back(TextRun(3, resized.m_base_style));
    resized.m_runs.back().m_style.m_font.m_size = 10;
    auto unkerned = lay_out(resized, get_library());
    REQUIRE(
      unkerned.m_glyphs[1].m_origin.m_x > whole.m_glyphs[1].m_origin.m_x);
  }

  TEST_CASE("measure_text") {
    auto text = make_text(u"Hello\nWorld");
    auto height = 2 * get_ascent() + 2 * get_descent();
    auto bounds = measure_text(text, get_library());
    REQUIRE(bounds.m_x == 0);
    REQUIRE(bounds.m_y == -get_ascent());
    REQUIRE(bounds.m_width > 0);
    REQUIRE(bounds.m_height == height);
    text.m_frame = AutoHeightFrame(150);
    REQUIRE(measure_text(text, get_library()) == Rect(0, 0, 150, height));
    text.m_frame = AreaFrame(Size(200, 100));
    REQUIRE(measure_text(text, get_library()) == Rect(0, 0, 200, 100));
  }

  TEST_CASE("store_layout") {
    auto text = make_text(u"Hello\nWorld");
    auto stored = store_layout(text, get_library());
    REQUIRE(stored.m_paragraphs.size() == 2);
    REQUIRE(stored.m_paragraphs[0].m_lines.size() == 1);
    REQUIRE(stored.m_paragraphs[0].m_lines[0].m_runs ==
      std::vector<StoredRun>({make_run(0, 5, Point(0, 0))}));
    REQUIRE(stored.m_paragraphs[1].m_lines.size() == 1);
    REQUIRE(stored.m_paragraphs[1].m_lines[0].m_runs ==
      std::vector<StoredRun>(
        {make_run(6, 11, Point(0, get_ascent() + get_descent()))}));
    auto unstored = lay_out(text, get_library());
    text.m_layout = Shared(stored);
    REQUIRE(store_layout(text, get_library()) == stored);
    REQUIRE(get_baselines(lay_out(text, get_library())) ==
      get_baselines(unstored));
  }
}

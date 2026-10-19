#include <cmath>
#include <utility>
#include <doctest/doctest.h>
#include "Rellu/Text/TextEditing.hpp"

using namespace Rellu;

namespace {
  using Range = std::pair<std::size_t, std::size_t>;

  const FontLibrary& get_library() {
    static auto library = FontLibrary(get_system_font_directories());
    return library;
  }

  const Font FONT = Font("Arial", "Regular", "ArialMT", 20);

  Text make_text(std::u16string content, TextFrame frame = PositionedFrame()) {
    auto text = Text();
    text.m_content = Shared(std::move(content));
    text.m_base_style.m_font = FONT;
    text.m_frame = frame;
    return text;
  }

  double get_width(std::u16string content) {
    auto lines = measure_lines(make_text(std::move(content)), get_library());
    return lines.front().m_carets.back();
  }
}

TEST_SUITE("TextEditing") {
  TEST_CASE("caret_positions") {
    auto text = make_text(u"Hello");
    auto lines = measure_lines(text, get_library());
    REQUIRE(lines.size() == 1);
    auto& line = lines.front();
    REQUIRE(line.m_start == 0);
    REQUIRE(line.m_end == 5);
    REQUIRE(line.m_is_paragraph_end);
    REQUIRE(line.m_baseline == 0);
    REQUIRE(line.m_carets.size() == 6);
    REQUIRE(line.m_carets.front() == 0);
    auto layout = lay_out(text, get_library());
    REQUIRE(layout.m_glyphs.size() == 5);
    for(auto i = std::size_t(0); i != 5; ++i) {
      REQUIRE(line.m_carets[i] ==
        doctest::Approx(layout.m_glyphs[i].m_origin.m_x));
      REQUIRE(line.m_carets[i + 1] > line.m_carets[i]);
    }
    REQUIRE(std::round(line.m_carets.back()) ==
      measure_text(text, get_library()).get_right());
  }

  TEST_CASE("aligned_carets") {
    auto text = make_text(u"Hello", AreaFrame(Size(200, 100)));
    text.m_paragraph_style.m_alignment = TextAlignment::RIGHT;
    auto lines = measure_lines(text, get_library());
    REQUIRE(lines.front().m_carets.back() == doctest::Approx(200));
    REQUIRE(lines.front().m_baseline == lines.front().m_ascent);
  }

  TEST_CASE("transformed_carets") {
    auto text = make_text(u"a\u00DFb");
    text.m_base_style.m_transform = TextTransform::UPPERCASE;
    auto lines = measure_lines(text, get_library());
    auto& carets = lines.front().m_carets;
    REQUIRE(carets.size() == 4);
    REQUIRE(carets[1] > carets[0]);
    REQUIRE(carets[2] > carets[1]);
    REQUIRE(carets[3] > carets[2]);
  }

  TEST_CASE("wrapped_lines") {
    auto width = get_width(u"aaa bbb") + 1;
    auto text = make_text(u"aaa bbb ccc\nd", AreaFrame(Size(width, 200)));
    auto lines = measure_lines(text, get_library());
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].m_start == 0);
    REQUIRE(lines[0].m_end == 8);
    REQUIRE(!lines[0].m_is_paragraph_end);
    REQUIRE(lines[1].m_start == 8);
    REQUIRE(lines[1].m_end == 11);
    REQUIRE(lines[1].m_is_paragraph_end);
    REQUIRE(lines[2].m_start == 12);
    REQUIRE(lines[2].m_end == 13);
    REQUIRE(find_line(lines, 7) == 0);
    REQUIRE(find_line(lines, 8) == 1);
    REQUIRE(find_line(lines, 11) == 1);
    REQUIRE(find_line(lines, 12) == 2);
    REQUIRE(lines[1].m_baseline > lines[0].m_baseline);
  }

  TEST_CASE("line_breaks") {
    auto lines = measure_lines(make_text(u"a\vb\n"), get_library());
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].m_end == 1);
    REQUIRE(lines[1].m_start == 2);
    REQUIRE(lines[2].m_start == 4);
    REQUIRE(lines[2].m_end == 4);
    REQUIRE(lay_out(make_text(u"a\vb"), get_library()).m_glyphs.size() == 2);
  }

  TEST_CASE("caret_rect") {
    auto lines = measure_lines(make_text(u"ab\ncd"), get_library());
    auto rect = get_caret_rect(lines, 4);
    auto& line = lines[1];
    REQUIRE(rect.m_x == doctest::Approx(line.m_carets[1]));
    REQUIRE(rect.m_y == line.m_baseline - line.m_ascent);
    REQUIRE(rect.m_width == 0);
    REQUIRE(rect.m_height == line.m_ascent + line.m_descent);
    auto empty = measure_lines(make_text(u""), get_library());
    REQUIRE(empty.size() == 1);
    REQUIRE(get_caret_rect(empty, 0).m_height > 0);
  }

  TEST_CASE("selection_rects") {
    auto lines = measure_lines(make_text(u"ab\ncd"), get_library());
    auto rects = get_selection_rects(lines, 1, 4);
    REQUIRE(rects.size() == 2);
    REQUIRE(rects[0].m_x == doctest::Approx(lines[0].m_carets[1]));
    REQUIRE(rects[0].get_right() > lines[0].m_carets[2]);
    REQUIRE(rects[1].m_x == doctest::Approx(lines[1].m_carets[0]));
    REQUIRE(rects[1].get_right() == doctest::Approx(lines[1].m_carets[1]));
    REQUIRE(get_selection_rects(lines, 2, 2).empty());
  }

  TEST_CASE("hit_test") {
    auto text = make_text(u"Hello\nWorld");
    auto lines = measure_lines(text, get_library());
    auto& content = *text.m_content;
    auto& first = lines[0];
    auto middle = (first.m_carets[1] + first.m_carets[2]) / 2;
    REQUIRE(hit_test(lines, content, Point(middle - 0.5, 0)) == 1);
    REQUIRE(hit_test(lines, content, Point(middle + 0.5, 0)) == 2);
    REQUIRE(hit_test(lines, content, Point(-50, -100)) == 0);
    REQUIRE(hit_test(lines, content, Point(1000, 0)) == 5);
    REQUIRE(hit_test(lines, content, Point(1000, 1000)) == 11);
    REQUIRE(hit_test(lines, content, Point(0, lines[1].m_baseline)) == 6);
  }

  TEST_CASE("hit_test_wrapped") {
    auto width = get_width(u"aaa bbb") + 1;
    auto text = make_text(u"aaa bbb ccc", AreaFrame(Size(width, 200)));
    auto lines = measure_lines(text, get_library());
    REQUIRE(hit_test(lines, *text.m_content,
      Point(width + 50, lines[0].m_baseline)) == 7);
    REQUIRE(get_line_end(lines, *text.m_content, 2) == 7);
    REQUIRE(get_line_start(lines, 9) == 8);
    REQUIRE(get_line_end(lines, *text.m_content, 9) == 11);
  }

  TEST_CASE("hit_test_surrogates") {
    auto text = make_text(u"a\U0001F600b");
    auto lines = measure_lines(text, get_library());
    auto& carets = lines[0].m_carets;
    auto x = (carets[1] + carets[3]) / 2;
    auto index = hit_test(lines, *text.m_content, Point(x, 0));
    REQUIRE(index != 2);
  }

  TEST_CASE("move_by_lines") {
    auto text = make_text(u"Hello\nHi\nWorld");
    auto lines = measure_lines(text, get_library());
    auto& content = *text.m_content;
    auto x = lines[0].m_carets[4];
    REQUIRE(move_by_lines(lines, content, 4, x, 1) == 8);
    REQUIRE(move_by_lines(lines, content, 8, x, 1) ==
      hit_test_line(lines, content, 2, x));
    REQUIRE(move_by_lines(lines, content, 13, x, -2) == 4);
    REQUIRE(move_by_lines(lines, content, 4, x, -1) == 0);
    REQUIRE(move_by_lines(lines, content, 13, x, 1) == content.size());
  }

  TEST_CASE("graphemes") {
    auto content = std::u16string(u"a\U0001F600e\u0301\r\nb");
    REQUIRE(get_next_grapheme(content, 0) == 1);
    REQUIRE(get_next_grapheme(content, 1) == 3);
    REQUIRE(get_next_grapheme(content, 3) == 5);
    REQUIRE(get_next_grapheme(content, 5) == 7);
    REQUIRE(get_next_grapheme(content, 8) == 8);
    REQUIRE(get_previous_grapheme(content, 7) == 5);
    REQUIRE(get_previous_grapheme(content, 5) == 3);
    REQUIRE(get_previous_grapheme(content, 3) == 1);
    REQUIRE(get_previous_grapheme(content, 0) == 0);
  }

  TEST_CASE("words") {
    auto content = std::u16string(u"Hello, big  world\nnext");
    REQUIRE(get_next_word(content, 0) == 5);
    REQUIRE(get_next_word(content, 5) == 7);
    REQUIRE(get_next_word(content, 7) == 12);
    REQUIRE(get_next_word(content, 12) == 17);
    REQUIRE(get_next_word(content, 17) == 18);
    REQUIRE(get_next_word(content, 22) == 22);
    REQUIRE(get_previous_word(content, 22) == 18);
    REQUIRE(get_previous_word(content, 18) == 17);
    REQUIRE(get_previous_word(content, 17) == 12);
    REQUIRE(get_previous_word(content, 12) == 7);
    REQUIRE(get_previous_word(content, 7) == 5);
    REQUIRE(get_previous_word(content, 3) == 0);
  }

  TEST_CASE("word_range") {
    auto content = std::u16string(u"Hello, big  world\nnext");
    REQUIRE(find_word(content, 2) == Range(0, 5));
    REQUIRE(find_word(content, 5) == Range(5, 7));
    REQUIRE(find_word(content, 8) == Range(7, 12));
    REQUIRE(find_word(content, 11) == Range(10, 12));
    REQUIRE(find_word(content, 17) == Range(12, 17));
    REQUIRE(find_word(content, 22) == Range(18, 22));
    auto vertical = std::u16string(u"one two\vthree");
    REQUIRE(find_word(vertical, 1) == Range(0, 4));
    REQUIRE(find_word(vertical, 5) == Range(4, 7));
  }

  TEST_CASE("paragraph_range") {
    auto content = std::u16string(u"one\ntwo three\nfour");
    REQUIRE(find_paragraph(content, 6) == Range(4, 13));
    REQUIRE(find_paragraph(content, 0) == Range(0, 3));
    REQUIRE(find_paragraph(content, 18) == Range(14, 18));
  }
}

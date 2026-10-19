#include <string>
#include <doctest/doctest.h>
#include "Rellu/Commands/TextCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  TextStyle make_style(double size) {
    auto style = TextStyle();
    style.m_font = Font("Roboto", "Regular", "Roboto-Regular", size);
    return style;
  }

  Text make_runs(std::u16string content, std::vector<TextRun> runs) {
    auto text = Text();
    text.m_content = Shared(std::move(content));
    text.m_base_style = runs.front().m_style;
    text.m_runs = std::move(runs);
    return text;
  }

  std::vector<std::size_t> get_lengths(const Text& text) {
    auto lengths = std::vector<std::size_t>();
    for(auto& run : text.m_runs) {
      lengths.push_back(run.m_length);
    }
    return lengths;
  }

  StoredLayout make_marker_layout() {
    auto layout = StoredLayout();
    layout.m_paragraphs.emplace_back();
    return layout;
  }

  NodeHeader make_text_header() {
    auto header = make_header("");
    header.m_transform = make_translation(20, 40);
    return header;
  }
}

TEST_SUITE("TextCommands") {
  TEST_CASE("insertion") {
    auto text = make_runs(u"Hello world", {TextRun(5, make_style(10)),
      TextRun(6, make_style(20))});
    auto start = replace_text(text, TextRange(0, 0), u"Oh ");
    REQUIRE(*start.m_content == u"Oh Hello world");
    REQUIRE(get_lengths(start) == std::vector<std::size_t>{8, 6});
    auto end_of_first = replace_text(text, TextRange(5, 5), u"!");
    REQUIRE(*end_of_first.m_content == u"Hello! world");
    REQUIRE(get_lengths(end_of_first) == std::vector<std::size_t>{6, 6});
    auto end = replace_text(text, TextRange(11, 11), u"s");
    REQUIRE(get_lengths(end) == std::vector<std::size_t>{5, 7});
    REQUIRE(!end.m_layout);
  }

  TEST_CASE("unstyled_insertion") {
    auto text = Text();
    text.m_content = Shared(std::u16string(u"ab"));
    auto edited = replace_text(text, TextRange(1, 1), u"x");
    REQUIRE(*edited.m_content == u"axb");
    REQUIRE(edited.m_runs.empty());
  }

  TEST_CASE("replacement") {
    auto text = make_runs(u"Hello world", {TextRun(5, make_style(10)),
      TextRun(6, make_style(20))});
    auto edited = replace_text(text, TextRange(3, 8), u"XY");
    REQUIRE(*edited.m_content == u"HelXYrld");
    REQUIRE(get_lengths(edited) == std::vector<std::size_t>{5, 3});
    auto second = replace_text(text, TextRange(5, 8), u"Z");
    REQUIRE(*second.m_content == u"HelloZrld");
    REQUIRE(get_lengths(second) == std::vector<std::size_t>{5, 4});
    REQUIRE(second.m_runs[1].m_style == make_style(20));
  }

  TEST_CASE("deletion_merges_runs") {
    auto text = make_runs(u"aaBBcc", {TextRun(2, make_style(10)),
      TextRun(2, make_style(20)), TextRun(2, make_style(10))});
    auto edited = replace_text(text, TextRange(2, 4), u"");
    REQUIRE(*edited.m_content == u"aacc");
    REQUIRE(edited.m_runs.size() == 1);
    REQUIRE(edited.m_runs.front().m_length == 4);
  }

  TEST_CASE("deleting_everything") {
    auto text = make_runs(u"ab", {TextRun(1, make_style(10)),
      TextRun(1, make_style(20))});
    auto edited = replace_text(text, TextRange(0, 2), u"");
    REQUIRE(edited.m_content->empty());
    REQUIRE(edited.m_runs.empty());
    REQUIRE(edited.m_base_style == make_style(10));
    REQUIRE(get_typing_run(edited, 0).m_style == make_style(10));
  }

  TEST_CASE("typing_run") {
    auto text = make_runs(u"abcd", {TextRun(2, make_style(10)),
      TextRun(2, make_style(20))});
    REQUIRE(get_typing_run(text, 0).m_style == make_style(10));
    REQUIRE(get_typing_run(text, 2).m_style == make_style(10));
    REQUIRE(get_typing_run(text, 3).m_style == make_style(20));
    REQUIRE(get_style(text, TextRange(2, 4)) == make_style(20));
  }

  TEST_CASE("style_splits_runs") {
    auto text = make_runs(u"Hello world", {TextRun(11, make_style(10))});
    auto edit = TextStyleEdit();
    edit.m_size = 30;
    auto edited = edit_text_style(text, TextRange(6, 11), edit);
    REQUIRE(get_lengths(edited) == std::vector<std::size_t>{6, 5});
    REQUIRE(edited.m_runs[1].m_style.m_font.m_size == 30);
    REQUIRE(edited.m_base_style.m_font.m_size == 10);
    auto middle = edit_text_style(text, TextRange(2, 4), edit);
    REQUIRE(get_lengths(middle) == std::vector<std::size_t>{2, 2, 7});
    auto rejoined = edit_text_style(edited, TextRange(0, 6), edit);
    REQUIRE(get_lengths(rejoined) == std::vector<std::size_t>{11});
    REQUIRE(rejoined.m_runs.front().m_style.m_font.m_size == 30);
  }

  TEST_CASE("whole_style") {
    auto text = Text();
    text.m_content = Shared(std::u16string(u"abc"));
    text.m_base_style = make_style(10);
    text.m_layout = Shared(make_marker_layout());
    auto edit = TextStyleEdit();
    edit.m_color = make_color(255, 0, 0);
    auto edited = edit_text_style(text, TextRange(0, 3), edit);
    REQUIRE(edited.m_runs.empty());
    REQUIRE(edited.m_base_style.m_color == make_color(255, 0, 0));
    REQUIRE(edited.m_style.m_fill == Fill(SolidFill(make_color(255, 0, 0))));
    REQUIRE(edited.m_layout);
    auto font = TextStyleEdit();
    font.m_family = "Arial";
    auto refont = edit_text_style(text, TextRange(0, 3), font);
    REQUIRE(refont.m_base_style.m_font.m_family == "Arial");
    REQUIRE(refont.m_base_style.m_font.m_style == "Regular");
    REQUIRE(refont.m_base_style.m_font.m_postscript_name.empty());
    REQUIRE(!refont.m_layout);
  }

  TEST_CASE("paragraph_style") {
    auto text = make_text(u"abc");
    text.m_layout = Shared(make_marker_layout());
    auto edit = ParagraphStyleEdit();
    edit.m_alignment = TextAlignment::CENTER;
    edit.m_line_height = std::optional<double>(30);
    auto edited = edit_paragraph_style(text, edit);
    REQUIRE(edited.m_paragraph_style.m_alignment == TextAlignment::CENTER);
    REQUIRE(edited.m_paragraph_style.m_line_height == 30);
    REQUIRE(!edited.m_layout);
    REQUIRE(edit_paragraph_style(text, ParagraphStyleEdit()).m_layout);
  }

  TEST_CASE("name") {
    REQUIRE(make_text_name(u"Hello") == "Hello");
    REQUIRE(make_text_name(u"caf\u00E9 \U0001F600") ==
      "caf\xC3\xA9 \xF0\x9F\x98\x80");
    auto content = std::u16string(255, u'a') + u"\U0001F600";
    REQUIRE(make_text_name(content) == std::string(255, 'a'));
    REQUIRE(make_text_name(std::u16string(300, u'b')).size() == 256);
  }

  TEST_CASE("edit_text") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto text = replace_text(
      *document.get(scene.m_text).get_text(), TextRange(5, 5), u"!");
    require_undoable(history,
      std::make_unique<EditTextCommand>(scene.m_text, text, "Hello!"));
    REQUIRE(history.get_undo_label() == "Edit Text");
    REQUIRE(*document.get(scene.m_text).get_text()->m_content == u"Hello!");
    REQUIRE(document.get(scene.m_text).m_header.m_name == "Hello!");
  }

  TEST_CASE("typing_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto header = make_text_header();
    auto id = header.m_id;
    auto empty = make_text(u"");
    REQUIRE(history.execute(std::make_unique<EditTextCommand>(
      TextInsertion(scene.m_artboard, header), empty)));
    REQUIRE(history.get_undo_label() == "Add Text");
    REQUIRE(document.get(id).m_parent == scene.m_artboard);
    auto typed = empty;
    for(auto character : std::u16string(u"abc")) {
      typed = replace_text(typed, TextRange(typed.m_content->size(),
        typed.m_content->size()), std::u16string(1, character));
      REQUIRE(history.merge(std::make_unique<EditTextCommand>(
        id, typed, make_text_name(*typed.m_content))));
    }
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(*document.get(id).get_text()->m_content == u"abc");
    REQUIRE(document.get(id).m_header.m_name == "abc");
    REQUIRE(history.undo());
    REQUIRE(!document.find(id));
    REQUIRE(history.redo());
    REQUIRE(*document.get(id).get_text()->m_content == u"abc");
  }

  TEST_CASE("empty_removal") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto header = make_text_header();
    auto id = header.m_id;
    REQUIRE(history.execute(std::make_unique<EditTextCommand>(
      TextInsertion(scene.m_artboard, header), make_text(u""))));
    REQUIRE(!history.merge(std::make_unique<EditTextCommand>(id)));
    REQUIRE(!document.find(id));
    REQUIRE(history.get_undo_count() == 0);
    require_undoable(history, std::make_unique<EditTextCommand>(scene.m_text));
    REQUIRE(history.get_undo_label() == "Delete");
    REQUIRE(!document.find(scene.m_text));
  }

  TEST_CASE("text_style_command") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto layouts = 0;
    auto layouter = [&] (const Text&) {
      ++layouts;
      return make_marker_layout();
    };
    auto edit = TextStyleEdit();
    edit.m_size = 24;
    require_undoable(history, std::make_unique<SetTextStyleCommand>(
      std::vector{scene.m_text, scene.m_rectangle}, edit, layouter));
    REQUIRE(history.get_undo_label() == "Change Font Size");
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(text.m_runs.front().m_style.m_font.m_size == 24);
    REQUIRE(text.m_layout);
    REQUIRE(**text.m_layout == make_marker_layout());
    REQUIRE(layouts > 0);
    auto count = layouts;
    auto color = TextStyleEdit();
    color.m_color = make_color(0, 0, 255);
    require_undoable(history, std::make_unique<SetTextStyleCommand>(
      scene.m_text, TextRange(1, 3), color, layouter));
    REQUIRE(history.get_undo_label() == "Change Text Color");
    REQUIRE(layouts == count);
    REQUIRE(get_lengths(*document.get(scene.m_text).get_text()) ==
      std::vector<std::size_t>{1, 2, 2});
  }

  TEST_CASE("text_style_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_text};
    auto size = TextStyleEdit();
    size.m_size = 20;
    REQUIRE(history.merge(std::make_unique<SetTextStyleCommand>(nodes, size)));
    size.m_size = 22;
    REQUIRE(history.merge(std::make_unique<SetTextStyleCommand>(nodes, size)));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get(scene.m_text).get_text()->m_runs.front().m_style.
      m_font.m_size == 22);
  }

  TEST_CASE("paragraph_style_command") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto edit = ParagraphStyleEdit();
    edit.m_paragraph_spacing = 8;
    require_undoable(history, std::make_unique<SetParagraphStyleCommand>(
      std::vector{scene.m_text}, edit));
    REQUIRE(history.get_undo_label() == "Change Paragraph Spacing");
    REQUIRE(document.get(scene.m_text).get_text()->m_paragraph_style.
      m_paragraph_spacing == 8);
  }

  TEST_CASE("frame_command") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto measurer = [] (const Text& text) {
      if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
        return Rect(0, 0, area->m_size.m_width, area->m_size.m_height);
      }
      return Rect(0, -12, 60, 16);
    };
    auto before = document.get(scene.m_text).m_header.m_transform;
    require_undoable(history, std::make_unique<SetTextFrameCommand>(
      std::vector{scene.m_text}, TextFrameKind::AUTO_WIDTH, measurer));
    REQUIRE(history.get_undo_label() == "Change Text Frame");
    auto& node = document.get(scene.m_text);
    REQUIRE(std::holds_alternative<PositionedFrame>(
      node.get_text()->m_frame));
    REQUIRE(node.m_header.m_transform ==
      before * make_translation(0, 12));
    require_undoable(history, std::make_unique<SetTextFrameCommand>(
      std::vector{scene.m_text}, TextFrameKind::FIXED_SIZE, measurer));
    auto& area = document.get(scene.m_text);
    REQUIRE(std::get<AreaFrame>(area.get_text()->m_frame).m_size ==
      Size(60, 16));
    REQUIRE(area.m_header.m_transform == before);
  }
}

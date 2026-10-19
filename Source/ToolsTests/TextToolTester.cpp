#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <doctest/doctest.h>
#include <QClipboard>
#include <QGuiApplication>
#include "Rellu/Text/TextEditing.hpp"
#include "Rellu/Tools/TextTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const FontLibrary& get_library() {
    return get_default_font_library();
  }

  struct Board {
    NodeId m_artboard;
    NodeId m_text;
  };

  Text make_label(std::u16string content) {
    auto text = Text();
    text.m_content = Shared(std::move(content));
    text.m_base_style.m_font = Font("Arial", "Regular", "ArialMT", 20);
    return text;
  }

  Board build_board(Document& document, std::u16string content) {
    auto board = Board();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 600, 400);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    board.m_artboard =
      transaction.append(NodeId(), make_header("Board"), artboard);
    board.m_text = transaction.append(board.m_artboard,
      make_header(make_text_name(content), make_translation(50, 350)),
      make_label(content));
    transaction.commit();
    document.clear_history();
    return board;
  }

  TextTool& get_text_tool(ToolManager& tools) {
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto name = tools.get_tool_name();
    tools.set_tool("text");
    auto& tool = static_cast<TextTool&>(tools.get_tool());
    tools.set_tool(name);
    return tool;
  }

  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void click(ToolManager& tools, Point position,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(
      make_event(position, Qt::LeftButton, Qt::LeftButton, modifiers));
    tools.release(
      make_event(position, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  void double_click(ToolManager& tools, Point position) {
    click(tools, position);
    tools.double_click(
      make_event(position, Qt::LeftButton, Qt::LeftButton));
    tools.release(make_event(position, Qt::LeftButton, Qt::NoButton));
  }

  void drag(ToolManager& tools, Point from, Point to) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton));
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(
        interpolate(from, to, i / 4.0), Qt::NoButton, Qt::LeftButton));
    }
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton));
  }

  bool press_key(ToolManager& tools, int key,
      Qt::KeyboardModifiers modifiers = {}, std::u16string text = u"") {
    return tools.key_press(KeyEvent(key, modifiers, false, std::move(text)));
  }

  void type(ToolManager& tools, std::u16string_view text) {
    for(auto character : text) {
      auto key = static_cast<int>(character);
      if(character >= u'a' && character <= u'z') {
        key = Qt::Key_A + (character - u'a');
      }
      press_key(tools, key, {}, std::u16string(1, character));
    }
  }

  const TextSelection& get_selection(Editor& editor) {
    return editor.get_text_selection();
  }

  const Text& get_text(const Document& document, const NodeId& id) {
    return *document.get(id).get_text();
  }

  const std::u16string& get_content(
      const Document& document, const NodeId& id) {
    return *get_text(document, id).m_content;
  }

  Point get_caret_point(const Document& document, const NodeId& id,
      std::size_t index) {
    auto lines = measure_lines(get_text(document, id), get_library());
    auto rect = get_caret_rect(lines, index);
    return get_world_transform(document, id).map(
      Point(rect.m_x, rect.m_y + rect.m_height / 2));
  }
}

TEST_SUITE("TextTool") {
  TEST_CASE("registry") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto changes = 0;
    auto subscription = tools.subscribe_tool([&] {
      ++changes;
    });
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(tools.has_tool("text"));
    REQUIRE_THROWS_AS(tools.set_tool("missing"), std::out_of_range);
    REQUIRE_THROWS_AS(tools.add_tool("text",
      std::make_unique<TextTool>(get_library())), std::invalid_argument);
    tools.set_tool("text");
    REQUIRE(tools.get_tool_name() == "text");
    REQUIRE(changes == 1);
    REQUIRE(dynamic_cast<TextTool*>(&tools.get_tool()));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::TEXT);
    press_key(tools, Qt::Key_Escape);
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(changes == 2);
  }

  TEST_CASE("point_text") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    click(tools, Point(300.4, 300.2));
    REQUIRE(tool.is_editing(tools.get_context()));
    auto id = get_selection(editor).get_node();
    auto& node = document.get(id);
    REQUIRE(node.m_parent == board.m_artboard);
    REQUIRE(editor.get_selection().get_nodes() == std::vector{id});
    auto& text = *node.get_text();
    REQUIRE(std::holds_alternative<PositionedFrame>(text.m_frame));
    REQUIRE(text.m_base_style == get_default_text_style());
    REQUIRE(text.m_style.m_fill ==
      Fill(SolidFill(get_default_text_style().m_color)));
    auto ascent = measure_lines(text, get_library()).front().m_ascent;
    REQUIRE(node.m_header.m_transform == make_translation(200, 200 + ascent));
    type(tools, u"Hi there");
    REQUIRE(get_content(document, id) == u"Hi there");
    REQUIRE(document.get(id).m_header.m_name == "Hi there");
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Add Text");
    auto& typed = get_text(document, id);
    REQUIRE(typed.m_layout);
    auto relaid = typed;
    relaid.m_layout = std::nullopt;
    REQUIRE(**typed.m_layout == store_layout(relaid, get_library()));
    REQUIRE(measure_text(typed, get_library()).m_width > 50);
    REQUIRE(press_key(tools, Qt::Key_Escape));
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(!get_selection(editor).is_editing());
    REQUIRE(editor.get_selection().get_nodes() == std::vector{id});
    REQUIRE(editor.undo());
    REQUIRE(!document.find(id));
  }

  TEST_CASE("area_text") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    drag(tools, Point(200, 200), Point(260, 300));
    REQUIRE(tool.is_editing(tools.get_context()));
    auto id = get_selection(editor).get_node();
    auto& node = document.get(id);
    REQUIRE(node.m_header.m_transform == make_translation(100, 100));
    REQUIRE(std::get<AreaFrame>(node.get_text()->m_frame).m_size ==
      Size(60, 100));
    type(tools, u"aaaa bbbb cccc dddd");
    REQUIRE(measure_lines(get_text(document, id), get_library()).size() > 1);
  }

  TEST_CASE("empty_text") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    get_text_tool(tools);
    tools.set_tool("text");
    click(tools, Point(300, 300));
    auto id = get_selection(editor).get_node();
    REQUIRE(document.find(id));
    REQUIRE(press_key(tools, Qt::Key_Escape));
    REQUIRE(!document.find(id));
    REQUIRE(editor.get_history().get_undo_count() == 0);
    REQUIRE(document.get(board.m_artboard).m_children.size() == 1);
    REQUIRE(tools.get_tool_name() == "select");
  }

  TEST_CASE("click_outside") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    click(tools, Point(300, 300));
    auto id = get_selection(editor).get_node();
    type(tools, u"x");
    click(tools, Point(650, 450));
    REQUIRE(!tool.is_editing(tools.get_context()));
    REQUIRE(tools.get_tool_name() == "select");
    REQUIRE(get_content(document, id) == u"x");
  }

  TEST_CASE("selection_change") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    click(tools, Point(300, 300));
    auto id = get_selection(editor).get_node();
    REQUIRE(tool.is_editing(tools.get_context()));
    editor.get_selection().select(board.m_text);
    REQUIRE(!tool.is_editing(tools.get_context()));
    REQUIRE(!press_key(tools, Qt::Key_A, {}, u"a"));
    REQUIRE(!get_selection(editor).is_editing());
    REQUIRE(!document.find(id));
  }

  TEST_CASE("select_tool_double_click") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    auto point = get_caret_point(document, board.m_text, 3);
    double_click(tools, point);
    REQUIRE(tools.get_tool_name() == "text");
    REQUIRE(tool.is_editing(tools.get_context()));
    REQUIRE(get_selection(editor).get_node() == board.m_text);
    REQUIRE(get_selection(editor).get_caret() == 3);
    REQUIRE(editor.get_selection().get_nodes() == std::vector{board.m_text});
  }

  TEST_CASE("click_on_text") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    click(tools, get_caret_point(document, board.m_text, 6));
    REQUIRE(tool.is_editing(tools.get_context()));
    REQUIRE(get_selection(editor).get_node() == board.m_text);
    REQUIRE(get_selection(editor).get_caret() == 6);
    REQUIRE(document.get(board.m_artboard).m_children.size() == 1);
  }

  TEST_CASE("deleting") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 11, 11);
    press_key(tools, Qt::Key_Backspace);
    REQUIRE(get_content(document, board.m_text) == u"Hello worl");
    REQUIRE(document.get(board.m_text).m_header.m_name == "Hello worl");
    press_key(tools, Qt::Key_Backspace, Qt::ControlModifier);
    REQUIRE(get_content(document, board.m_text) == u"Hello ");
    press_key(tools, Qt::Key_Home);
    press_key(tools, Qt::Key_Delete);
    REQUIRE(get_content(document, board.m_text) == u"ello ");
    press_key(tools, Qt::Key_Delete, Qt::ControlModifier);
    REQUIRE(get_content(document, board.m_text) == u"");
    REQUIRE(press_key(tools, Qt::Key_Escape));
    REQUIRE(!document.find(board.m_text));
    REQUIRE(editor.undo());
    REQUIRE(get_content(document, board.m_text) == u"Hello ");
  }

  TEST_CASE("navigation") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 11, 11);
    auto& selection = get_selection(editor);
    press_key(tools, Qt::Key_Home);
    REQUIRE(selection.get_caret() == 0);
    press_key(tools, Qt::Key_End);
    REQUIRE(selection.get_caret() == 11);
    press_key(tools, Qt::Key_Left, Qt::ControlModifier);
    REQUIRE(selection.get_caret() == 6);
    press_key(tools, Qt::Key_Left, Qt::ShiftModifier);
    REQUIRE(selection.get_range() == TextRange(5, 6));
    press_key(tools, Qt::Key_Right);
    REQUIRE(selection.get_range() == TextRange(6, 6));
    press_key(tools, Qt::Key_Right);
    REQUIRE(selection.get_caret() == 7);
    press_key(tools, Qt::Key_Home, Qt::ShiftModifier);
    REQUIRE(selection.get_range() == TextRange(0, 7));
    press_key(tools, Qt::Key_Left);
    REQUIRE(selection.get_range() == TextRange(0, 0));
    press_key(tools, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    REQUIRE(selection.get_range() == TextRange(0, 11));
    press_key(tools, Qt::Key_A, Qt::ControlModifier);
    REQUIRE(selection.get_range() == TextRange(0, 11));
  }

  TEST_CASE("line_navigation") {
    auto document = Document();
    auto board = build_board(document, u"abc\nde\nfghi");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 3, 3);
    auto& selection = get_selection(editor);
    press_key(tools, Qt::Key_Down);
    REQUIRE(selection.get_caret() == 6);
    press_key(tools, Qt::Key_Down);
    auto lines = measure_lines(get_text(document, board.m_text),
      get_library());
    auto& content = get_content(document, board.m_text);
    REQUIRE(selection.get_caret() ==
      hit_test_line(lines, content, 2, lines[0].m_carets[3]));
    press_key(tools, Qt::Key_Up);
    press_key(tools, Qt::Key_Up);
    REQUIRE(selection.get_caret() == 3);
    press_key(tools, Qt::Key_Up);
    REQUIRE(selection.get_caret() == 0);
  }

  TEST_CASE("paragraphs") {
    auto document = Document();
    auto board = build_board(document, u"ab");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 1, 1);
    press_key(tools, Qt::Key_Return);
    press_key(tools, Qt::Key_Return, Qt::ShiftModifier);
    press_key(tools, Qt::Key_Tab);
    REQUIRE(get_content(document, board.m_text) == u"a\n\v\tb");
    REQUIRE(measure_lines(get_text(document, board.m_text),
      get_library()).size() == 3);
  }

  TEST_CASE("typing_over_selection") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 0, 5);
    type(tools, u"J");
    REQUIRE(get_content(document, board.m_text) == u"J world");
    REQUIRE(get_selection(editor).get_range() == TextRange(1, 1));
  }

  TEST_CASE("undo_merging") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 11, 11);
    auto& history = editor.get_history();
    type(tools, u"!!!");
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Edit Text");
    press_key(tools, Qt::Key_Left);
    type(tools, u"?");
    REQUIRE(history.get_undo_count() == 2);
    press_key(tools, Qt::Key_Backspace);
    press_key(tools, Qt::Key_Backspace);
    REQUIRE(history.get_undo_count() == 3);
    REQUIRE(get_content(document, board.m_text) == u"Hello world!!");
    REQUIRE(editor.undo());
    REQUIRE(get_content(document, board.m_text) == u"Hello world!!?!");
    REQUIRE(editor.undo());
    REQUIRE(get_content(document, board.m_text) == u"Hello world!!!");
    REQUIRE(editor.undo());
    REQUIRE(get_content(document, board.m_text) == u"Hello world");
  }

  TEST_CASE("clipboard") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 6, 11);
    REQUIRE(press_key(tools, Qt::Key_C, Qt::ControlModifier));
    REQUIRE(QGuiApplication::clipboard()->text() == "world");
    press_key(tools, Qt::Key_Home);
    REQUIRE(press_key(tools, Qt::Key_V, Qt::ControlModifier));
    REQUIRE(get_content(document, board.m_text) == u"worldHello world");
    REQUIRE(get_selection(editor).get_caret() == 5);
    press_key(tools, Qt::Key_A, Qt::ControlModifier);
    REQUIRE(press_key(tools, Qt::Key_X, Qt::ControlModifier));
    REQUIRE(QGuiApplication::clipboard()->text() == "worldHello world");
    REQUIRE(get_content(document, board.m_text) == u"");
    QGuiApplication::clipboard()->setText("a\r\nb");
    press_key(tools, Qt::Key_V, Qt::ControlModifier);
    REQUIRE(get_content(document, board.m_text) == u"a\nb");
  }

  TEST_CASE("input_method") {
    auto document = Document();
    auto board = build_board(document, u"Hello");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    auto& context = tools.get_context();
    tool.edit(context, board.m_text, 5, 5);
    tool.input_method(context, InputMethodEvent(u"", u"ka", 2, 0, 0));
    REQUIRE(get_content(document, board.m_text) == u"Helloka");
    REQUIRE(tool.get_preedit() == TextRange(5, 7));
    REQUIRE(get_selection(editor).get_caret() == 7);
    tool.input_method(context, InputMethodEvent(u"", u"kan", 3, 0, 0));
    REQUIRE(get_content(document, board.m_text) == u"Hellokan");
    tool.input_method(context, InputMethodEvent(u"\u304B\u3093", u"", 0, 0, 0));
    REQUIRE(get_content(document, board.m_text) == u"Hello\u304B\u3093");
    REQUIRE(!tool.get_preedit());
    REQUIRE(get_selection(editor).get_caret() == 7);
    REQUIRE(editor.get_history().get_undo_count() == 1);
    tool.input_method(context, InputMethodEvent(u"", u"a", 1, 0, 0));
    REQUIRE(get_content(document, board.m_text) == u"Hello\u304B\u3093a");
    tool.commit(context);
    REQUIRE(get_content(document, board.m_text) == u"Hello\u304B\u3093");
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }

  TEST_CASE("mouse_selection") {
    auto document = Document();
    auto board = build_board(document, u"Hello big world\nnext line");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 0, 0);
    auto& selection = get_selection(editor);
    auto point = get_caret_point(document, board.m_text, 7);
    tools.double_click(make_event(point, Qt::LeftButton, Qt::LeftButton));
    tools.release(make_event(point, Qt::LeftButton, Qt::NoButton));
    REQUIRE(selection.get_range() == TextRange(6, 9));
    click(tools, point);
    REQUIRE(selection.get_range() == TextRange(0, 15));
    click(tools, get_caret_point(document, board.m_text, 2));
    REQUIRE(selection.get_range() == TextRange(2, 2));
    click(tools, get_caret_point(document, board.m_text, 13),
      Qt::ShiftModifier);
    REQUIRE(selection.get_range() == TextRange(2, 13));
    drag(tools, get_caret_point(document, board.m_text, 1),
      get_caret_point(document, board.m_text, 20));
    REQUIRE(selection.get_anchor() == 1);
    REQUIRE(selection.get_caret() == 20);
  }

  TEST_CASE("caret_drawing") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_view(Matrix(), 2, Size(1000, 800));
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 3, 3);
    auto find_caret = [&] {
      auto overlay = tools.draw();
      return std::count_if(overlay.m_shapes.begin(), overlay.m_shapes.end(),
        [] (const auto& shape) {
          return shape.m_size.m_height == 0.5 &&
            shape.m_fill == make_color(0, 0, 0);
        });
    };
    REQUIRE(find_caret() == 1);
    tool.blink();
    REQUIRE(!tool.is_caret_visible());
    REQUIRE(find_caret() == 0);
    press_key(tools, Qt::Key_Right, Qt::ShiftModifier);
    REQUIRE(tool.is_caret_visible());
    REQUIRE(find_caret() == 0);
    auto overlay = tools.draw();
    REQUIRE(std::any_of(overlay.m_shapes.begin(), overlay.m_shapes.end(),
      [] (const auto& shape) {
        return shape.m_fill.m_alpha > 0 && shape.m_fill.m_alpha < 1;
      }));
    auto caret = tool.get_caret_rect(tools.get_context());
    REQUIRE(caret);
    REQUIRE(caret->m_height > 0);
  }

  TEST_CASE("text_keys") {
    auto document = Document();
    auto board = build_board(document, u"Hello world");
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    auto& context = tools.get_context();
    auto letter = KeyEvent(Qt::Key_V, {}, false, u"v");
    REQUIRE(!tool.is_text_key(context, letter));
    tools.set_tool("text");
    tool.edit(context, board.m_text, 0, 0);
    REQUIRE(tool.is_text_key(context, letter));
    REQUIRE(tool.is_text_key(context, KeyEvent(Qt::Key_Delete, {})));
    REQUIRE(tool.is_text_key(context,
      KeyEvent(Qt::Key_C, Qt::ControlModifier, false, u"\x03")));
    REQUIRE(!tool.is_text_key(context,
      KeyEvent(Qt::Key_Z, Qt::ControlModifier, false, u"\x1A")));
    REQUIRE(!tool.is_text_key(context,
      KeyEvent(Qt::Key_S, Qt::ControlModifier, false, u"\x13")));
  }

  TEST_CASE("typing_performance") {
    auto document = Document();
    auto board = build_board(document, std::u16string(2000, u'a'));
    {
      auto transaction = Transaction(document);
      for(auto i = 0; i != 20000; ++i) {
        transaction.append(board.m_artboard, make_header("Rectangle",
          make_translation(i % 500, (i / 500) % 300)),
          make_rectangle_shape(10, 10));
      }
      auto text = make_label(std::u16string(2000, u'a'));
      for(auto i = std::size_t(0); i < 2000; i += 9) {
        auto content = std::u16string(*text.m_content);
        content[i] = u' ';
        text.m_content = Shared(std::move(content));
      }
      text.m_frame = AutoHeightFrame(400);
      transaction.replace_payload(board.m_text, text);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tool = get_text_tool(tools);
    tools.set_tool("text");
    tool.edit(tools.get_context(), board.m_text, 1000, 1000);
    auto start = std::chrono::steady_clock::now();
    auto count = 20;
    for(auto i = 0; i != count; ++i) {
      type(tools, u"b");
      tools.draw();
    }
    auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count() / count;
    MESSAGE("Typed into a 2000 character paragraph among 20000 nodes in " <<
      elapsed << " ms per character");
    REQUIRE(get_content(document, board.m_text).size() == 2020);
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }
}

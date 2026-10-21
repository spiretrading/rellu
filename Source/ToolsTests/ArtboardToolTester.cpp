#include <doctest/doctest.h>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Tools/ArtboardTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId build_artboard(Document& document) {
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 300, 200);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    auto id = transaction.append(
      NodeId(), make_header("Artboard \xE2\x80\x93 4"), artboard);
    transaction.commit();
    document.clear_history();
    return id;
  }

  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void drag(ToolManager& tools, Point from, Point to,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton, modifiers));
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(interpolate(from, to, i / 4.0), Qt::NoButton,
        Qt::LeftButton, modifiers));
    }
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  void click(ToolManager& tools, Point position) {
    tools.press(make_event(position, Qt::LeftButton, Qt::LeftButton));
    tools.release(make_event(position, Qt::LeftButton, Qt::NoButton));
  }
}

TEST_SUITE("ArtboardTool") {
  TEST_CASE("draw") {
    auto document = Document();
    auto existing = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("artboard");
    drag(tools, Point(800, 100), Point(500, 400));
    auto& nodes = editor.get_selection().get_nodes();
    REQUIRE(nodes.size() == 1);
    auto& node = document.get(nodes.front());
    REQUIRE(node.m_parent.is_nil());
    REQUIRE(document.get_canvas() == std::vector{existing, node.get_id()});
    REQUIRE(node.m_header.m_name == "Artboard \xE2\x80\x93 1");
    REQUIRE(node.m_header.m_transform == Matrix());
    auto& artboard = *node.get_artboard();
    REQUIRE(artboard.m_frame == Rect(500, 100, 300, 300));
    REQUIRE(artboard.m_background ==
      Fill(SolidFill(make_color(255, 255, 255))));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Draw Artboard");
    REQUIRE(tools.get_tool_name() == "artboard");
    drag(tools, Point(600, 600), Point(700, 650), Qt::ShiftModifier);
    auto& square = document.get(editor.get_selection().get_primary());
    REQUIRE(square.get_artboard()->m_frame == Rect(600, 600, 100, 100));
    REQUIRE(square.m_header.m_name == "Artboard \xE2\x80\x93 2");
  }

  TEST_CASE("drawn_grid") {
    auto document = Document();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("artboard");
    drag(tools, Point(0, 0), Point(2299, 1711));
    auto& node = document.get(editor.get_selection().get_primary());
    REQUIRE(!node.m_header.m_constraints.m_is_enabled);
    auto& artboard = *node.get_artboard();
    REQUIRE(artboard.m_frame == Rect(0, 0, 2299, 1711));
    REQUIRE(!artboard.m_viewport_height);
    REQUIRE(artboard.m_layout_grid);
    auto& grid = *artboard.m_layout_grid;
    REQUIRE(grid.m_type == GridType::SQUARE);
    REQUIRE(!grid.m_is_visible);
    REQUIRE(grid.m_default_width == 2299);
    REQUIRE(grid.m_column_count == 12);
    REQUIRE(grid.m_gutter == 16);
    REQUIRE(grid.m_margins == Margins(0, 137, 0, 137));
    REQUIRE(grid.m_square_size == Size(8, 8));
    REQUIRE(grid.m_square_row_color == Color(
      199 / 255.0, 199 / 255.0, 199 / 255.0, 128 / 255.0));
    REQUIRE(grid.m_column_color == Color(0, 1, 1, 64 / 255.0));
  }

  TEST_CASE("handles") {
    auto document = Document();
    build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("artboard");
    drag(tools, Point(500, 100), Point(800, 400));
    auto drawn = editor.get_selection().get_primary();
    auto overlay = tools.draw();
    auto handles = std::size_t(0);
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius > 0) {
        ++handles;
      }
    }
    REQUIRE(handles == 8);
    tools.move(make_event(Point(803.5, 250), Qt::NoButton, Qt::NoButton));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::RESIZE);
    tools.move(make_event(Point(804, 250), Qt::NoButton, Qt::NoButton));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::ARTBOARD);
    drag(tools, Point(800, 250), Point(850, 250));
    REQUIRE(document.get(drawn).get_artboard()->m_frame ==
      Rect(500, 100, 350, 300));
    REQUIRE(document.get_canvas().size() == 2);
    REQUIRE(editor.get_history().get_undo_label() == "Resize");
    REQUIRE(tools.get_tool_name() == "artboard");
  }

  TEST_CASE("click") {
    auto document = Document();
    auto existing = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("artboard");
    set_last_used_preset(ArtboardPreset("iPhone 14 Pro", Size(393, 852)));
    click(tools, Point(105, 90));
    REQUIRE(editor.get_selection().get_nodes() == std::vector{existing});
    REQUIRE(!editor.get_history().can_undo());
    click(tools, Point(200, 200));
    auto& created = document.get(editor.get_selection().get_primary());
    REQUIRE(created.m_header.m_name == "iPhone 14 Pro \xE2\x80\x93 1");
    REQUIRE(created.get_artboard()->m_frame == Rect(-363, 100, 393, 852));
    REQUIRE(created.get_artboard()->m_viewport_height == 852);
    REQUIRE(editor.get_history().get_undo_label() == "Create Artboard");
    REQUIRE(document.get_canvas() ==
      std::vector{existing, created.get_id()});
    set_last_used_preset(ArtboardPreset("Web 1920", Size(1920, 1080)));
    click(tools, Point(800, 700));
    auto& beside = document.get(editor.get_selection().get_primary());
    REQUIRE(beside.m_header.m_name == "Web 1920 \xE2\x80\x93 1");
    REQUIRE(beside.get_artboard()->m_frame == Rect(470, 100, 1920, 1080));
    click(tools, Point(500, 2000));
    REQUIRE(document.get(editor.get_selection().get_primary()).
      get_artboard()->m_frame == Rect(470, 1270, 1920, 1080));
  }

  TEST_CASE("captured_objects") {
    auto document = Document();
    auto existing = build_artboard(document);
    auto transaction = Transaction(document);
    auto inside = transaction.append(NodeId(),
      make_header("Inside", make_translation(520, 120)),
      make_rectangle_shape(40, 40));
    auto partial = transaction.append(NodeId(),
      make_header("Partial", make_translation(790, 120)),
      make_rectangle_shape(40, 40));
    auto outside = transaction.append(NodeId(),
      make_header("Outside", make_translation(900, 120)),
      make_rectangle_shape(40, 40));
    auto hidden_header = make_header("Hidden", make_translation(600, 200));
    hidden_header.m_is_visible = false;
    auto hidden =
      transaction.append(NodeId(), hidden_header, make_rectangle_shape(20, 20));
    auto locked_header = make_header("Locked", make_translation(600, 380));
    locked_header.m_is_locked = true;
    auto locked =
      transaction.append(NodeId(), locked_header, make_rectangle_shape(20, 40));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("artboard");
    drag(tools, Point(500, 100), Point(800, 400));
    auto& nodes = editor.get_selection().get_nodes();
    REQUIRE(nodes.size() == 1);
    auto drawn = nodes.front();
    REQUIRE(document.get_canvas() == std::vector{existing, outside, drawn});
    REQUIRE(document.get(drawn).m_children ==
      std::vector{inside, partial, hidden, locked});
    REQUIRE(*get_world_bounds(document, inside) == Rect(520, 120, 40, 40));
    REQUIRE(*get_world_bounds(document, partial) == Rect(790, 120, 40, 40));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.undo());
    REQUIRE(document.get_canvas() ==
      std::vector{existing, inside, partial, outside, hidden, locked});
    REQUIRE(*get_world_bounds(document, inside) == Rect(520, 120, 40, 40));
  }
}

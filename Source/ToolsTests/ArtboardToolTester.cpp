#include <doctest/doctest.h>
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
    REQUIRE(node.m_header.m_name == "Artboard \xE2\x80\x93 5");
    REQUIRE(node.m_header.m_transform == Matrix());
    auto& artboard = *node.get_artboard();
    REQUIRE(artboard.m_frame == Rect(500, 100, 300, 300));
    REQUIRE(artboard.m_background ==
      Fill(SolidFill(make_color(255, 255, 255))));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Artboard");
    REQUIRE(tools.get_tool_name() == "artboard");
    drag(tools, Point(600, 600), Point(700, 650), Qt::ShiftModifier);
    REQUIRE(document.get(editor.get_selection().get_primary()).
      get_artboard()->m_frame == Rect(600, 600, 100, 100));
  }

  TEST_CASE("click") {
    auto document = Document();
    auto existing = build_artboard(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("artboard");
    click(tools, Point(200, 200));
    REQUIRE(editor.get_selection().get_nodes() == std::vector{existing});
    click(tools, Point(800, 700));
    REQUIRE(editor.get_selection().is_empty());
    REQUIRE(document.get_canvas() == std::vector{existing});
    REQUIRE(!editor.get_history().can_undo());
  }

  TEST_CASE("enclosed_objects") {
    auto document = Document();
    auto existing = build_artboard(document);
    auto transaction = Transaction(document);
    auto inside = transaction.append(NodeId(),
      make_header("Inside", make_translation(520, 120)),
      make_rectangle_shape(40, 40));
    auto partial = transaction.append(NodeId(),
      make_header("Partial", make_translation(780, 120)),
      make_rectangle_shape(40, 40));
    auto outside = transaction.append(NodeId(),
      make_header("Outside", make_translation(900, 120)),
      make_rectangle_shape(40, 40));
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
    REQUIRE(document.get_canvas() ==
      std::vector{existing, partial, outside, drawn});
    REQUIRE(document.get(drawn).m_children == std::vector{inside});
    REQUIRE(*get_world_bounds(document, inside) == Rect(520, 120, 40, 40));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.undo());
    REQUIRE(document.get_canvas() ==
      std::vector{existing, inside, partial, outside});
    REQUIRE(*get_world_bounds(document, inside) == Rect(520, 120, 40, 40));
  }
}

#include <doctest/doctest.h>
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Tools/PrototypeTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Boards {
    NodeId m_first;
    NodeId m_second;
    NodeId m_button;
  };

  Boards build_boards(Document& document) {
    auto boards = Boards();
    auto transaction = Transaction(document);
    auto first = Artboard();
    first.m_frame = Rect(100, 100, 400, 300);
    boards.m_first =
      transaction.append(NodeId(), make_header("First"), first);
    auto second = Artboard();
    second.m_frame = Rect(600, 100, 200, 200);
    boards.m_second =
      transaction.append(NodeId(), make_header("Second"), second);
    boards.m_button = transaction.append(boards.m_first,
      make_header("Button", make_translation(50, 50)),
      make_rectangle_shape(100, 50));
    transaction.commit();
    document.clear_history();
    return boards;
  }

  PointerEvent make_event(
      Point position, Qt::MouseButton button, Qt::MouseButtons buttons) {
    return PointerEvent(position, button, buttons, Qt::KeyboardModifiers());
  }

  void click(ToolManager& tools, Point position) {
    tools.press(make_event(position, Qt::LeftButton, Qt::LeftButton));
    tools.release(make_event(position, Qt::LeftButton, Qt::NoButton));
  }

  void drag(ToolManager& tools, Point from, Point to) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(to, Qt::NoButton, Qt::LeftButton));
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton));
  }

  struct Setup {
    Document m_document;
    Boards m_boards;
    std::unique_ptr<Editor> m_editor;
    std::unique_ptr<ToolManager> m_tools;

    Setup()
        : m_boards(build_boards(m_document)),
          m_editor(std::make_unique<Editor>(m_document)),
          m_tools(std::make_unique<ToolManager>(*m_editor)) {
      m_tools->set_view(Matrix(), 1, Size(1000, 800));
      m_tools->set_tool("prototype");
    }
  };

  PrototypeTool& get_tool(ToolManager& tools) {
    return dynamic_cast<PrototypeTool&>(tools.get_tool());
  }
}

TEST_SUITE("PrototypeTool") {
  TEST_CASE("selection") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& selection = setup.m_editor->get_selection();
    REQUIRE(tools.get_tool_name() == "prototype");
    click(tools, Point(200, 175));
    REQUIRE(selection.get_nodes() == std::vector{setup.m_boards.m_button});
    auto handle = get_tool(tools).get_handle(tools.get_context());
    REQUIRE(handle);
    REQUIRE(*handle == Point(250, 175));
    click(tools, Point(550, 50));
    REQUIRE(selection.is_empty());
    REQUIRE(!get_tool(tools).get_handle(tools.get_context()));
    click(tools, Point(610, 90));
    REQUIRE(selection.get_nodes() == std::vector{setup.m_boards.m_second});
    REQUIRE(get_tool(tools).get_handle(tools.get_context()) ==
      Point(800, 200));
  }

  TEST_CASE("wire") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& document = setup.m_document;
    auto& boards = setup.m_boards;
    click(tools, Point(200, 175));
    drag(tools, Point(251, 176), Point(700, 200));
    REQUIRE(!tools.is_busy());
    auto interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 1);
    REQUIRE(interactions.front()->m_action == Action(ArtboardTransition(
      boards.m_second, make_default_animation())));
    REQUIRE(std::holds_alternative<TapTrigger>(
      interactions.front()->m_trigger));
    REQUIRE(document.get_prototype().m_flows ==
      std::vector<Flow>({Flow(boards.m_first, "Flow 1")}));
    auto& history = setup.m_editor->get_history();
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Add Interaction");
    drag(tools, Point(250, 175), Point(300, 300));
    interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 1);
    REQUIRE(std::get<ArtboardTransition>(
      interactions.front()->m_action).m_destination == boards.m_first);
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(setup.m_editor->undo());
    REQUIRE(setup.m_editor->undo());
    REQUIRE(document.get_prototype().m_interactions.empty());
    REQUIRE(document.get_prototype().m_flows.empty());
  }

  TEST_CASE("wire_dropped_outside") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    click(tools, Point(200, 175));
    drag(tools, Point(250, 175), Point(550, 50));
    REQUIRE(setup.m_document.get_prototype().m_interactions.empty());
    REQUIRE(setup.m_editor->get_history().get_undo_count() == 0);
  }

  TEST_CASE("escape") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    click(tools, Point(200, 175));
    tools.press(make_event(Point(250, 175), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(700, 200), Qt::NoButton, Qt::LeftButton));
    REQUIRE(tools.is_busy());
    REQUIRE(tools.key_press(
      KeyEvent(Qt::Key_Escape, Qt::KeyboardModifiers(), false)));
    REQUIRE(!tools.is_busy());
    tools.release(make_event(Point(700, 200), Qt::LeftButton, Qt::NoButton));
    REQUIRE(setup.m_document.get_prototype().m_interactions.empty());
  }

  TEST_CASE("markers") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& document = setup.m_document;
    auto& boards = setup.m_boards;
    auto marker = get_tool(tools).get_marker(tools.get_context(),
      boards.m_first);
    REQUIRE(marker == Rect(84, 100, 16, 24));
    click(tools, Point(90, 110));
    REQUIRE(document.get_prototype().m_flows.empty());
    click(tools, Point(200, 175));
    click(tools, Point(90, 110));
    REQUIRE(document.get_prototype().m_flows ==
      std::vector<Flow>({Flow(boards.m_first, "Flow 1")}));
    setup.m_editor->get_selection().clear();
    click(tools, Point(90, 110));
    REQUIRE(document.get_prototype().m_flows.empty());
    REQUIRE(setup.m_editor->get_history().get_undo_count() == 2);
  }

  TEST_CASE("draw") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto empty = tools.draw();
    auto images = empty.m_images.size();
    click(tools, Point(200, 175));
    drag(tools, Point(250, 175), Point(700, 200));
    auto overlay = tools.draw();
    REQUIRE(overlay.m_images.size() == images + 2);
    auto handles = 0;
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius == PrototypeTool::HANDLE_RADIUS &&
          shape.m_center == Point(250, 175)) {
        ++handles;
      }
    }
    REQUIRE(handles == 1);
    REQUIRE(overlay.m_shapes.size() > 24);
    REQUIRE(get_tool(tools).find_destination(
      tools.get_context(), Point(700, 200)) == setup.m_boards.m_second);
  }

  TEST_CASE("wire_to_empty_interaction") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& document = setup.m_document;
    auto& boards = setup.m_boards;
    setup.m_editor->execute(std::make_unique<AddInteractionCommand>(
      make_empty_interaction(boards.m_button)));
    click(tools, Point(200, 175));
    drag(tools, Point(251, 176), Point(700, 200));
    auto interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 1);
    REQUIRE(interactions.front()->m_action == Action(ArtboardTransition(
      boards.m_second, make_default_animation())));
  }
}

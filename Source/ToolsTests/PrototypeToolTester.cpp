#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>
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
    NodeId m_target;
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
    boards.m_target = transaction.append(boards.m_first,
      make_header("Target", make_translation(0, 200)),
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
      boards.m_second, make_empty_animation())));
    REQUIRE(std::holds_alternative<TapTrigger>(
      interactions.front()->m_trigger));
    REQUIRE(document.get_prototype().m_flows ==
      std::vector<Flow>({Flow(boards.m_first, "Flow 1")}));
    auto& history = setup.m_editor->get_history();
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Draw Wire");
    REQUIRE(setup.m_editor->get_context().get_interactions() ==
      std::vector{interactions.front()->m_id});
    drag(tools, Point(250, 175), Point(700, 200));
    interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 2);
    REQUIRE(std::holds_alternative<DragTrigger>(interactions[1]->m_trigger));
    REQUIRE(interactions[1]->m_action == Action(ArtboardTransition(
      boards.m_second, Animation(Transition::AUTO_ANIMATE, Easing::EASE_OUT,
        DEFAULT_DURATION))));
    drag(tools, Point(250, 175), Point(150, 325));
    interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 3);
    REQUIRE(std::holds_alternative<KeyTrigger>(interactions[2]->m_trigger));
    auto scroll = std::get_if<ScrollTransition>(&interactions[2]->m_action);
    REQUIRE(scroll);
    REQUIRE(scroll->m_destination == boards.m_target);
    REQUIRE(scroll->m_animation.m_transition == Transition::DISSOLVE);
    drag(tools, Point(250, 175), Point(300, 120));
    REQUIRE(get_interactions(document, boards.m_button).size() == 3);
    REQUIRE(history.get_undo_count() == 3);
    REQUIRE(setup.m_editor->undo());
    REQUIRE(setup.m_editor->undo());
    REQUIRE(setup.m_editor->undo());
    REQUIRE(document.get_prototype().m_interactions.empty());
    REQUIRE(document.get_prototype().m_flows.empty());
  }

  TEST_CASE("handle_click") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& document = setup.m_document;
    auto& boards = setup.m_boards;
    auto& edit = setup.m_editor->get_context();
    auto& tool = get_tool(tools);
    edit.set_prototyping(true);
    click(tools, Point(200, 175));
    click(tools, Point(250, 175));
    REQUIRE(get_interactions(document, boards.m_button).empty());
    auto& history = setup.m_editor->get_history();
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Create Wire");
    REQUIRE(edit.get_pending().size() == 1);
    auto pending = edit.get_pending().front();
    REQUIRE(setup.m_editor->undo());
    REQUIRE(edit.get_pending().empty());
    REQUIRE(edit.get_interactions().empty());
    REQUIRE(get_interactions(document, boards.m_button).empty());
    REQUIRE(history.get_redo_label() == "Create Wire");
    REQUIRE(setup.m_editor->redo());
    REQUIRE(edit.get_pending() == std::vector{pending});
    REQUIRE(get_interactions(document, boards.m_button).empty());
    REQUIRE(pending == [&] {
      auto empty = make_empty_interaction(boards.m_button);
      empty.m_id = pending.m_id;
      return empty;
    }());
    REQUIRE(edit.get_interactions() == std::vector{pending.m_id});
    auto wires = tool.get_wires(tools.get_context());
    REQUIRE(wires.size() == 1);
    REQUIRE(wires.front().m_is_selected);
    REQUIRE(wires.front().m_knob);
    drag(tools, *wires.front().m_knob, Point(700, 200));
    REQUIRE(edit.get_pending().empty());
    auto interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 1);
    REQUIRE(interactions.front()->m_id == pending.m_id);
    REQUIRE(interactions.front()->m_action == Action(
      ArtboardTransition(boards.m_second, make_empty_animation())));
    REQUIRE(setup.m_editor->get_history().get_undo_label() == "Draw Wire");
    REQUIRE(edit.get_interactions() == std::vector{pending.m_id});
    click(tools, Point(250, 175));
    REQUIRE(edit.get_pending().size() == 1);
    REQUIRE(std::holds_alternative<DragTrigger>(
      edit.get_pending().front().m_trigger));
    click(tools, Point(550, 500));
    REQUIRE(edit.get_pending().empty());
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
    REQUIRE(marker == Rect(79, 99, 20, 104.0 / 3));
    auto empty = tools.draw().m_images.size();
    click(tools, Point(90, 110));
    REQUIRE(document.get_prototype().m_flows.empty());
    click(tools, Point(200, 175));
    REQUIRE(tools.draw().m_images.size() > empty);
    click(tools, Point(90, 110));
    REQUIRE(document.get_prototype().m_flows ==
      std::vector<Flow>({Flow(boards.m_first, "Flow 1")}));
    setup.m_editor->get_selection().clear();
    click(tools, Point(90, 110));
    REQUIRE(document.get_prototype().m_flows.empty());
    REQUIRE(setup.m_editor->get_history().get_undo_count() == 2);
  }

  TEST_CASE("flow_label") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& editor = *setup.m_editor;
    auto& boards = setup.m_boards;
    auto& selection = editor.get_selection();
    REQUIRE(editor.execute(std::make_unique<AddInteractionCommand>(
      Interaction(NodeId(), boards.m_button, TapTrigger(), ArtboardTransition(
        boards.m_second, make_default_animation())))));
    REQUIRE(!tool.get_flow_label(tools.get_context(), boards.m_first));
    click(tools, Point(200, 175));
    click(tools, Point(90, 110));
    REQUIRE(find_flow(setup.m_document, boards.m_first));
    REQUIRE(editor.get_history().get_undo_label() == "Mark as flow root");
    REQUIRE(selection.get_nodes() ==
      std::vector({boards.m_second, boards.m_first}));
    auto label = tool.get_flow_label(tools.get_context(), boards.m_first);
    REQUIRE(label);
    REQUIRE(label->get_top_left() == Point(100, 43));
    REQUIRE(label->m_height == 28);
    selection.clear();
    click(tools, label->get_center());
    REQUIRE(selection.get_nodes() ==
      std::vector({boards.m_second, boards.m_first}));
    tools.set_view(Matrix(0.1, 0, 0, 0.1, 0, 0), 1, Size(1000, 800));
    REQUIRE(tool.get_flow_label(tools.get_context(),
      boards.m_first)->get_top_left() == Point(10, -29));
    click(tools, Point(5, 12));
    REQUIRE(!find_flow(setup.m_document, boards.m_first));
    REQUIRE(editor.get_history().get_undo_label() == "Unmark as flow root");
  }

  TEST_CASE("marker_sizes") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& first = setup.m_boards.m_first;
    auto marker_at = [&] (double zoom) {
      tools.set_view(Matrix(zoom, 0, 0, zoom, 0, 0), 1, Size(1000, 800));
      return *tool.get_marker(tools.get_context(), first);
    };
    REQUIRE(marker_at(1).get_size() == PrototypeTool::LARGE_MARKER_SIZE);
    REQUIRE(marker_at(0.75).get_size() == PrototypeTool::LARGE_MARKER_SIZE);
    REQUIRE(marker_at(0.74).get_size() == PrototypeTool::MEDIUM_MARKER_SIZE);
    REQUIRE(marker_at(0.5) == Rect(33, 49, 16, 28));
    REQUIRE(marker_at(0.33).get_size() == PrototypeTool::MEDIUM_MARKER_SIZE);
    REQUIRE(marker_at(0.32).get_size() == PrototypeTool::SMALL_MARKER_SIZE);
    REQUIRE(marker_at(0.1) == Rect(-1, 9, 10, 52.0 / 3));
  }

  TEST_CASE("draw") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto empty = tools.draw();
    auto images = empty.m_images.size();
    REQUIRE(tool.get_wires(tools.get_context()).empty());
    click(tools, Point(200, 175));
    drag(tools, Point(250, 175), Point(700, 200));
    auto overlay = tools.draw();
    REQUIRE(overlay.m_images.size() == images + 4);
    auto handles = 0;
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_radius == PrototypeTool::HANDLE_RADIUS &&
          shape.m_center == Point(250, 175)) {
        ++handles;
      }
    }
    REQUIRE(handles == 1);
    auto wires = tool.get_wires(tools.get_context());
    REQUIRE(wires.size() == 1);
    auto& wire = wires.front();
    REQUIRE(wire.m_owner == setup.m_boards.m_button);
    REQUIRE(wire.m_destination == setup.m_boards.m_second);
    REQUIRE(wire.m_is_selected);
    REQUIRE(wire.m_interactions == std::vector({setup.m_editor->get_context().
      get_interaction()}));
    REQUIRE(wire.m_start == Point(250, 175));
    REQUIRE(wire.m_end == Point(587, 200));
    REQUIRE(std::abs(wire.m_middle.m_x - 418.5) < 1e-9);
    REQUIRE(std::abs(wire.m_middle.m_y - 187.5) < 1e-9);
    REQUIRE(!wire.m_knob);
    REQUIRE(tool.find_destination(
      tools.get_context(), Point(700, 200)) == setup.m_boards.m_second);
  }

  TEST_CASE("wire_selection") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& editor = *setup.m_editor;
    auto& boards = setup.m_boards;
    auto& selection = editor.get_selection();
    auto& edit = editor.get_context();
    auto add = [&] (const NodeId& owner, Trigger trigger, Action action) {
      auto command = std::make_unique<AddInteractionCommand>(Interaction(
        NodeId(), owner, std::move(trigger), std::move(action)));
      auto id = command->get_interaction().m_id;
      REQUIRE(editor.execute(std::move(command)));
      return id;
    };
    auto transition =
      Action(ArtboardTransition(boards.m_second, make_default_animation()));
    auto tap = add(boards.m_button, TapTrigger(), transition);
    auto back = add(boards.m_button, KeyTrigger("K"), PreviousArtboard());
    auto target = add(boards.m_target, TapTrigger(), transition);
    click(tools, Point(200, 175));
    REQUIRE(selection.get_nodes() == std::vector{boards.m_button});
    auto wires = tool.get_wires(tools.get_context());
    REQUIRE(wires.size() == 3);
    REQUIRE(wires[0].m_interactions == std::vector{target});
    REQUIRE(wires[1].m_interactions == std::vector{tap});
    REQUIRE(wires[2].m_interactions == std::vector{back});
    REQUIRE(std::ranges::none_of(wires, &WireShape::m_is_selected));
    REQUIRE(wires[0].m_start == Point(150, 287));
    REQUIRE(wires[0].m_end == Point(700, 313));
    REQUIRE(wires[2].m_end == Point(310, 135));
    REQUIRE(wires[2].m_knob == Point(310, 135));
    click(tools, wires[2].m_middle);
    REQUIRE(selection.get_nodes() == std::vector{boards.m_button});
    REQUIRE(edit.get_interaction() == back);
    wires = tool.get_wires(tools.get_context());
    REQUIRE(wires.back().m_interactions == std::vector{back});
    REQUIRE(wires.back().m_is_selected);
    click(tools, wires[0].m_middle);
    REQUIRE(selection.get_nodes() == std::vector{boards.m_target});
    REQUIRE(edit.get_interaction() == target);
    wires = tool.get_wires(tools.get_context());
    auto wire = *std::ranges::find_if(wires, [&] (const auto& wire) {
      return wire.m_interactions == std::vector{tap};
    });
    tools.move(make_event(wire.m_middle, Qt::NoButton, Qt::NoButton));
    REQUIRE(!tool.get_hover_knob());
    tools.move(make_event(wire.m_middle, Qt::NoButton, Qt::NoButton));
    auto knob = tool.get_hover_knob();
    REQUIRE(knob);
    REQUIRE(std::hypot(knob->m_x - wire.m_middle.m_x,
      knob->m_y - wire.m_middle.m_y) < 1);
    REQUIRE(tool.get_tool_tip() == "Tap");
    REQUIRE(edit.get_hover().is_nil());
    click(tools, wire.m_middle);
    REQUIRE(selection.get_nodes() == std::vector{boards.m_button});
    REQUIRE(edit.get_interaction() == tap);
    REQUIRE(!tool.get_hover_knob());
    REQUIRE(tool.get_tool_tip().empty());
    auto count = editor.get_history().get_undo_count();
    click(tools, wire.m_middle);
    REQUIRE(selection.get_nodes() == std::vector{boards.m_button});
    REQUIRE(edit.get_interaction() == tap);
    REQUIRE(editor.get_history().get_undo_count() == count);
    tools.move(make_event(wire.m_middle + Point(0, 1), Qt::NoButton,
      Qt::NoButton));
    REQUIRE(!tool.get_hover_knob());
    tools.move(make_event(Point(550, 500), Qt::NoButton, Qt::NoButton));
    REQUIRE(!tool.get_hover_knob());
    tools.move(make_event(wire.m_middle, Qt::NoButton, Qt::NoButton));
    REQUIRE(tool.get_hover_knob());
    auto tip = std::ranges::count_if(tools.draw().m_images,
      [&] (const auto& image) {
        return image.m_position == wire.m_middle + Point(15, -30);
      });
    REQUIRE(tip == 1);
    tools.leave();
    REQUIRE(!tool.get_hover_knob());
  }

  TEST_CASE("multiple_wires") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& editor = *setup.m_editor;
    auto& boards = setup.m_boards;
    auto& edit = editor.get_context();
    auto add = [&] (Trigger trigger, Action action) {
      auto command = std::make_unique<AddInteractionCommand>(Interaction(
        NodeId(), boards.m_button, std::move(trigger), std::move(action)));
      auto id = command->get_interaction().m_id;
      REQUIRE(editor.execute(std::move(command)));
      return id;
    };
    auto tap = add(TapTrigger(),
      ArtboardTransition(boards.m_second, make_default_animation()));
    auto back = add(KeyTrigger("K"), PreviousArtboard());
    auto count_tints = [&] {
      return std::ranges::count_if(tools.draw().m_images,
        [] (const auto& image) {
          return image.m_size.has_value();
        });
    };
    click(tools, Point(200, 175));
    REQUIRE(edit.get_interactions().empty());
    REQUIRE(count_tints() == 1);
    auto middle = [&] (const InteractionId& id) {
      for(auto& wire : tool.get_wires(tools.get_context())) {
        if(wire.m_interactions == std::vector{id}) {
          return wire.m_middle;
        }
      }
      return Point(0, 0);
    };
    auto shift_click = [&] (Point position) {
      tools.press(PointerEvent(
        position, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier));
      tools.release(PointerEvent(
        position, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier));
    };
    auto tap_middle = middle(tap);
    auto back_middle = middle(back);
    click(tools, tap_middle);
    REQUIRE(edit.get_interactions() == std::vector{tap});
    REQUIRE(count_tints() == 0);
    shift_click(back_middle);
    REQUIRE(edit.get_interactions() == std::vector({tap, back}));
    REQUIRE(std::ranges::all_of(tool.get_wires(tools.get_context()),
      &WireShape::m_is_selected));
    shift_click(tap_middle);
    REQUIRE(edit.get_interactions() == std::vector{back});
    shift_click(back_middle);
    REQUIRE(edit.get_interactions().empty());
    REQUIRE(std::ranges::none_of(tool.get_wires(tools.get_context()),
      &WireShape::m_is_selected));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{boards.m_button});
  }

  TEST_CASE("wire_knob") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& editor = *setup.m_editor;
    auto& boards = setup.m_boards;
    for(auto trigger : {Trigger(TapTrigger()), Trigger(DragTrigger())}) {
      REQUIRE(editor.execute(std::make_unique<AddInteractionCommand>(
        Interaction(NodeId(), boards.m_button, trigger, ArtboardTransition(
          boards.m_second, make_default_animation())))));
    }
    click(tools, Point(200, 175));
    auto wires = tool.get_wires(tools.get_context());
    REQUIRE(wires.size() == 1);
    REQUIRE(wires.front().m_is_selected);
    REQUIRE(wires.front().m_interactions.size() == 2);
    REQUIRE(wires.front().m_knob);
    tools.move(make_event(*wires.front().m_knob + Point(0, 20), Qt::NoButton,
      Qt::NoButton));
    REQUIRE(!tool.get_hover_knob());
    REQUIRE(tool.get_tool_tip().empty());
    tools.move(make_event(*wires.front().m_knob, Qt::NoButton, Qt::NoButton));
    REQUIRE(tool.get_hover_knob());
    REQUIRE(tool.get_tool_tip() == "Tap, Drag");
    auto knob = *wires.front().m_knob;
    tools.press(PointerEvent(
      knob, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier));
    tools.release(PointerEvent(
      knob, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier));
    REQUIRE(!tool.get_wires(tools.get_context()).front().m_is_selected);
    REQUIRE(setup.m_editor->get_context().get_interactions().empty());
  }

  TEST_CASE("scroll_offset") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& document = setup.m_document;
    auto& boards = setup.m_boards;
    auto& editor = *setup.m_editor;
    auto& target = boards.m_target;
    auto command = std::make_unique<AddInteractionCommand>(Interaction(
      NodeId(), boards.m_button, TapTrigger(),
      ScrollTransition(target, -40, make_default_animation())));
    auto id = command->get_interaction().m_id;
    REQUIRE(editor.execute(std::move(command)));
    auto get_offset = [&] {
      return std::get<ScrollTransition>(
        get_interactions(document, boards.m_button).front()->m_action).
          m_offset;
    };
    auto& tool = get_tool(tools);
    REQUIRE(!tool.get_offset_handle(tools.get_context()));
    click(tools, Point(200, 175));
    REQUIRE(tool.get_offset_handle(tools.get_context()) ==
      Rect(80, 253, 20, 14));
    auto idle = tools.draw();
    auto& history = editor.get_history();
    auto count = history.get_undo_count();
    tools.press(make_event(Point(90, 260), Qt::LeftButton, Qt::LeftButton));
    REQUIRE(tools.is_busy());
    REQUIRE(tools.get_cursor() ==
      ToolCursor(CursorKind::RESIZE, std::numbers::pi / 2));
    tools.move(make_event(Point(90, 230), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_offset() == -70);
    tools.move(make_event(Point(90, 200), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_offset() == -100);
    REQUIRE(tools.draw().m_images.size() == idle.m_images.size() + 1);
    tools.release(make_event(Point(90, 200), Qt::LeftButton, Qt::NoButton));
    REQUIRE(!tools.is_busy());
    REQUIRE(history.get_undo_count() == count + 1);
    REQUIRE(history.get_undo_label() == "Y-Offset");
    REQUIRE(tool.get_offset_handle(tools.get_context()) ==
      Rect(80, 193, 20, 14));
    drag(tools, Point(90, 200), Point(90, -500));
    REQUIRE(get_offset() == -200);
    REQUIRE(history.get_undo_count() == count + 2);
    REQUIRE(editor.undo());
    REQUIRE(get_offset() == -100);
    tools.press(make_event(Point(90, 200), Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(90, 250), Qt::NoButton, Qt::LeftButton));
    REQUIRE(get_offset() == -50);
    REQUIRE(tools.key_press(
      KeyEvent(Qt::Key_Escape, Qt::KeyboardModifiers(), false)));
    REQUIRE(get_offset() == -100);
    tools.release(make_event(Point(90, 250), Qt::LeftButton, Qt::NoButton));
    REQUIRE(history.get_undo_count() == count + 1);
    auto other = std::make_unique<AddInteractionCommand>(Interaction(
      NodeId(), boards.m_button, KeyTrigger(),
      ArtboardTransition(boards.m_second, make_default_animation())));
    auto other_id = other->get_interaction().m_id;
    REQUIRE(editor.execute(std::move(other)));
    editor.get_selection().select(boards.m_button);
    REQUIRE(!tool.get_offset_handle(tools.get_context()));
    editor.get_context().set_interaction(id);
    REQUIRE(tool.get_offset_handle(tools.get_context()));
    editor.get_context().set_interaction(other_id);
    REQUIRE(!tool.get_offset_handle(tools.get_context()));
  }

  TEST_CASE("wire_to_empty_interaction") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& document = setup.m_document;
    auto& boards = setup.m_boards;
    auto command = std::make_unique<AddInteractionCommand>(
      make_empty_interaction(boards.m_button));
    auto empty = command->get_interaction().m_id;
    setup.m_editor->execute(std::move(command));
    click(tools, Point(200, 175));
    drag(tools, Point(251, 176), Point(700, 200));
    auto interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 2);
    REQUIRE(interactions.front()->m_action ==
      Action(ArtboardTransition(NodeId(), make_empty_animation())));
    REQUIRE(std::holds_alternative<DragTrigger>(interactions[1]->m_trigger));
    setup.m_editor->get_context().set_interaction(empty);
    auto wires = tool.get_wires(tools.get_context());
    auto pending = *std::ranges::find_if(wires, [&] (const auto& wire) {
      return wire.m_interactions == std::vector{empty};
    });
    REQUIRE(pending.m_knob);
    drag(tools, *pending.m_knob, Point(700, 200));
    interactions = get_interactions(document, boards.m_button);
    REQUIRE(interactions.size() == 2);
    REQUIRE(interactions.front()->m_action == Action(
      ArtboardTransition(boards.m_second, make_empty_animation())));
    REQUIRE(setup.m_editor->get_history().get_undo_label() == "Draw Wire");
  }

  TEST_CASE("rewire") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& editor = *setup.m_editor;
    auto& document = setup.m_document;
    auto& boards = setup.m_boards;
    auto command = std::make_unique<AddInteractionCommand>(Interaction(
      NodeId(), boards.m_button, TapTrigger(), ArtboardTransition(
        boards.m_second, make_default_animation())));
    auto tap = command->get_interaction().m_id;
    REQUIRE(editor.execute(std::move(command)));
    click(tools, Point(200, 175));
    REQUIRE(tool.get_wires(tools.get_context()).front().m_is_selected);
    auto middle = tool.get_wires(tools.get_context()).front().m_middle;
    tools.press(make_event(middle, Qt::LeftButton, Qt::LeftButton));
    tools.move(make_event(Point(150, 325), Qt::NoButton, Qt::LeftButton));
    REQUIRE(tools.is_busy());
    REQUIRE(tool.get_wires(tools.get_context()).size() == 1);
    REQUIRE(tools.key_press(
      KeyEvent(Qt::Key_Escape, Qt::KeyboardModifiers(), false)));
    tools.release(make_event(Point(150, 325), Qt::LeftButton, Qt::NoButton));
    REQUIRE(std::get<ArtboardTransition>(get_interactions(
      document, boards.m_button).front()->m_action).m_destination ==
        boards.m_second);
    auto count = editor.get_history().get_undo_count();
    drag(tools, middle, Point(150, 325));
    auto scroll = std::get_if<ScrollTransition>(
      &get_interactions(document, boards.m_button).front()->m_action);
    REQUIRE(scroll);
    REQUIRE(scroll->m_destination == boards.m_target);
    REQUIRE(editor.get_history().get_undo_count() == count + 1);
    REQUIRE(editor.get_history().get_undo_label() == "Draw Wire");
    REQUIRE(editor.get_context().get_interactions() == std::vector{tap});
    REQUIRE(editor.undo());
    REQUIRE(std::get<ArtboardTransition>(get_interactions(
      document, boards.m_button).front()->m_action).m_destination ==
        boards.m_second);
    drag(tools, middle, Point(550, 500));
    REQUIRE(get_interactions(document, boards.m_button).empty());
    REQUIRE(editor.get_history().get_undo_label() == "Draw Wire");
  }

  TEST_CASE("shift_other_node") {
    auto setup = Setup();
    auto& tools = *setup.m_tools;
    auto& tool = get_tool(tools);
    auto& editor = *setup.m_editor;
    auto& boards = setup.m_boards;
    auto add = [&] (const NodeId& owner) {
      auto command = std::make_unique<AddInteractionCommand>(Interaction(
        NodeId(), owner, TapTrigger(), ArtboardTransition(
          boards.m_second, make_default_animation())));
      auto id = command->get_interaction().m_id;
      REQUIRE(editor.execute(std::move(command)));
      return id;
    };
    auto tap = add(boards.m_button);
    auto other = add(boards.m_target);
    click(tools, Point(200, 175));
    auto middle = [&] (const InteractionId& id) {
      for(auto& wire : tool.get_wires(tools.get_context())) {
        if(wire.m_interactions == std::vector{id}) {
          return wire.m_middle;
        }
      }
      return Point(0, 0);
    };
    auto other_middle = middle(other);
    tools.press(PointerEvent(
      other_middle, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier));
    tools.release(PointerEvent(
      other_middle, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{boards.m_button});
    REQUIRE(editor.get_context().get_interactions() ==
      std::vector({tap, other}));
    REQUIRE(std::ranges::all_of(tool.get_wires(tools.get_context()),
      &WireShape::m_is_selected));
    click(tools, middle(tap));
    REQUIRE(editor.get_context().get_interactions() == std::vector{tap});
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector{boards.m_button});
  }
}

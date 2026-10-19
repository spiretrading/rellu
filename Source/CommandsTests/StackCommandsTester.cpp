#include <doctest/doctest.h>
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/StackCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Box {
    NodeId m_group;
    NodeId m_a;
    NodeId m_b;
    NodeId m_c;
  };

  Box build_box(Document& document) {
    auto box = Box();
    auto transaction = Transaction(document);
    box.m_group = transaction.append(
      NodeId(), make_header("Box", make_translation(100, 100)), Group());
    box.m_a = transaction.append(box.m_group, make_header("A"),
      make_rectangle_shape(100, 20));
    box.m_b = transaction.append(box.m_group,
      make_header("B", make_translation(0, 100)),
      make_rectangle_shape(100, 30));
    box.m_c = transaction.append(box.m_group,
      make_header("C", make_translation(0, 50)),
      make_rectangle_shape(100, 40));
    transaction.commit();
    document.clear_history();
    return box;
  }

  Rect get_frame(const Document& document, const NodeId& id) {
    return *get_bounds(document, id, get_transform(document, id));
  }

  void require_frame(
      const Document& document, const NodeId& id, const Rect& expected) {
    auto frame = get_frame(document, id);
    REQUIRE(frame.m_x == doctest::Approx(expected.m_x));
    REQUIRE(frame.m_y == doctest::Approx(expected.m_y));
    REQUIRE(frame.m_width == doctest::Approx(expected.m_width));
    REQUIRE(frame.m_height == doctest::Approx(expected.m_height));
  }

  const Group& get_group(const Document& document, const NodeId& id) {
    return *document.get(id).get_group();
  }
}

TEST_SUITE("StackCommands") {
  TEST_CASE("enable") {
    auto document = Document();
    auto box = build_box(document);
    auto history = History(document);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{box.m_group}, Orientation::VERTICAL, estimate_text_bounds));
    REQUIRE(history.get_undo_label() == "Stack");
    auto& stack = *get_group(document, box.m_group).m_stack;
    REQUIRE(stack.m_orientation == Orientation::VERTICAL);
    REQUIRE(document.get(box.m_group).m_children ==
      std::vector{box.m_b, box.m_c, box.m_a});
    REQUIRE(stack.m_spacing.at(box.m_a) == 30);
    REQUIRE(stack.m_spacing.at(box.m_c) == 10);
    REQUIRE(!stack.m_spacing.contains(box.m_b));
    require_frame(document, box.m_a, Rect(0, 0, 100, 20));
    require_frame(document, box.m_c, Rect(0, 50, 100, 40));
    require_frame(document, box.m_b, Rect(0, 100, 100, 30));
    history.undo();
    REQUIRE(!get_group(document, box.m_group).m_stack);
    REQUIRE(document.get(box.m_group).m_children ==
      std::vector{box.m_a, box.m_b, box.m_c});
  }

  TEST_CASE("direction") {
    auto document = Document();
    auto box = build_box(document);
    auto history = History(document);
    REQUIRE(get_natural_orientation(document, box.m_group,
      estimate_text_bounds) == Orientation::VERTICAL);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{box.m_group}, Orientation::VERTICAL, estimate_text_bounds));
    history.execute(std::make_unique<SetStackCommand>(std::vector{box.m_group},
      Orientation::HORIZONTAL, estimate_text_bounds));
    require_frame(document, box.m_a, Rect(0, 0, 100, 20));
    require_frame(document, box.m_c, Rect(130, 50, 100, 40));
    require_frame(document, box.m_b, Rect(240, 100, 100, 30));
    REQUIRE(get_natural_orientation(document, box.m_group,
      estimate_text_bounds) == Orientation::HORIZONTAL);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{box.m_group}, std::nullopt, estimate_text_bounds));
    REQUIRE(!get_group(document, box.m_group).m_stack);
  }

  TEST_CASE("spacing") {
    auto document = Document();
    auto box = build_box(document);
    auto history = History(document);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{box.m_group}, Orientation::VERTICAL, estimate_text_bounds));
    history.execute(std::make_unique<SetStackSpacingCommand>(
      std::vector{box.m_group}, 5, estimate_text_bounds));
    REQUIRE(history.get_undo_label() == "Spacing");
    require_frame(document, box.m_c, Rect(0, 25, 100, 40));
    require_frame(document, box.m_b, Rect(0, 70, 100, 30));
    history.merge(std::make_unique<SetStackSpacingCommand>(
      std::vector{box.m_group}, 8, estimate_text_bounds));
    require_frame(document, box.m_b, Rect(0, 76, 100, 30));
    REQUIRE(history.get_undo_count() == 2);
    history.execute(std::make_unique<SetStackSpacingCommand>(
      box.m_group, box.m_a, 0, estimate_text_bounds));
    require_frame(document, box.m_c, Rect(0, 20, 100, 40));
    require_frame(document, box.m_b, Rect(0, 68, 100, 30));
    auto& stack = *get_group(document, box.m_group).m_stack;
    REQUIRE(stack.m_spacing.at(box.m_a) == 0);
    REQUIRE(stack.m_spacing.at(box.m_c) == 8);
  }

  TEST_CASE("padding") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(NodeId(), make_header("Box"), Group());
    auto background = transaction.append(
      group, make_header("Background"), make_rectangle_shape(300, 300));
    auto content = transaction.append(group,
      make_header("Content", make_translation(40, 50)),
      make_rectangle_shape(100, 60));
    transaction.append(group,
      make_header("Outside", make_translation(-30, 10)),
      make_rectangle_shape(10, 10));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    REQUIRE(!get_padding(document, group));
    history.execute(std::make_unique<SetPaddingCommand>(
      std::vector{group}, true, estimate_text_bounds));
    REQUIRE(history.get_undo_label() == "Padding");
    REQUIRE(get_padding(document, group) == Margins(10, 160, 190, 0));
    require_frame(document, background, Rect(-30, 0, 330, 300));
    history.execute(std::make_unique<SetPaddingValueCommand>(
      std::vector{group}, PaddingSide::ALL, 10, estimate_text_bounds));
    REQUIRE(get_padding(document, group) == Margins(10, 10, 10, 10));
    require_frame(document, background, Rect(-40, 0, 190, 120));
    history.merge(std::make_unique<SetPaddingValueCommand>(
      std::vector{group}, PaddingSide::ALL, 12, estimate_text_bounds));
    REQUIRE(history.get_undo_count() == 2);
    history.execute(std::make_unique<SetPaddingValueCommand>(
      std::vector{group}, PaddingSide::LEFT, -5, estimate_text_bounds));
    REQUIRE(get_padding(document, group) == Margins(12, 12, 12, 0));
    require_frame(document, content, Rect(40, 50, 100, 60));
    history.execute(std::make_unique<SetPaddingCommand>(
      std::vector{group}, false, estimate_text_bounds));
    REQUIRE(!get_padding(document, group));
    REQUIRE(!get_group(document, group).m_padding);
  }

  TEST_CASE("stack_padding") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_padding = Padding(Margins(5, 5, 5, 5), true, true);
    auto id = transaction.append(NodeId(), make_header("Box"), group);
    transaction.append(id, make_header("Background"),
      make_rectangle_shape(110, 80));
    auto first = transaction.append(id,
      make_header("First", make_translation(5, 45)),
      make_rectangle_shape(100, 30));
    auto second = transaction.append(id,
      make_header("Second", make_translation(5, 5)),
      make_rectangle_shape(100, 30));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{id}, Orientation::VERTICAL, estimate_text_bounds));
    auto& payload = get_group(document, id);
    REQUIRE(!payload.m_padding);
    REQUIRE(payload.m_stack->m_has_background);
    REQUIRE(payload.m_stack->m_padding == Margins(5, 5, 5, 5));
    REQUIRE(payload.m_stack->m_spacing.at(second) == 10);
    require_frame(document, first, Rect(5, 45, 100, 30));
    REQUIRE(get_padding(document, id) == Margins(5, 5, 5, 5));
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{id}, std::nullopt, estimate_text_bounds));
    REQUIRE(get_group(document, id).m_padding ==
      Padding(Margins(5, 5, 5, 5), true, true));
  }

  TEST_CASE("move_items") {
    auto document = Document();
    auto box = build_box(document);
    auto history = History(document);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{box.m_group}, Orientation::VERTICAL, estimate_text_bounds));
    REQUIRE(get_stack_items(document, box.m_group) ==
      std::vector{box.m_a, box.m_c, box.m_b});
    REQUIRE(!make_stack_nudge(document, std::vector{box.m_c}, Point(1, 0)));
    REQUIRE(!make_stack_nudge(document, std::vector{box.m_group},
      Point(0, 1)));
    auto down = make_stack_nudge(document, std::vector{box.m_c}, Point(0, 1));
    REQUIRE(down);
    REQUIRE(down->get_label() == "Reorder");
    history.execute(std::move(down));
    REQUIRE(get_stack_items(document, box.m_group) ==
      std::vector{box.m_a, box.m_b, box.m_c});
    require_frame(document, box.m_b, Rect(0, get_frame(document, box.m_a).
      get_bottom() + get_stack_margin(document,
        *get_group(document, box.m_group).m_stack, box.m_a), 100, 30));
    history.execute(make_stack_nudge(
      document, std::vector{box.m_a, box.m_b}, Point(0, 1)));
    REQUIRE(get_stack_items(document, box.m_group) ==
      std::vector{box.m_c, box.m_a, box.m_b});
    history.execute(make_stack_nudge(
      document, std::vector{box.m_b}, Point(0, -1)));
    REQUIRE(get_stack_items(document, box.m_group) ==
      std::vector{box.m_c, box.m_b, box.m_a});
  }
}

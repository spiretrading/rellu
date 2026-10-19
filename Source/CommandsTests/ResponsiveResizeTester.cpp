#include <chrono>
#include <string>
#include <doctest/doctest.h>
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/ResponsiveResize.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_rectangle(Transaction& transaction, const NodeId& parent,
      std::string name, const Rect& rect) {
    return transaction.append(parent,
      make_header(std::move(name), make_translation(rect.m_x, rect.m_y)),
      make_rectangle_shape(rect.m_width, rect.m_height));
  }

  NodeId add_artboard(
      Transaction& transaction, const Rect& frame, bool is_enabled) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    auto header = make_header("Artboard");
    header.m_constraints.m_is_enabled = is_enabled;
    return transaction.append(NodeId(), std::move(header), artboard);
  }

  Rect get_bounds(const Document& document, const NodeId& id) {
    return *get_world_bounds(document, id);
  }

  void require_bounds(
      const Document& document, const NodeId& id, const Rect& expected) {
    auto bounds = get_bounds(document, id);
    REQUIRE(bounds.m_x == doctest::Approx(expected.m_x));
    REQUIRE(bounds.m_y == doctest::Approx(expected.m_y));
    REQUIRE(bounds.m_width == doctest::Approx(expected.m_width));
    REQUIRE(bounds.m_height == doctest::Approx(expected.m_height));
  }

  Matrix make_local_mapping(const Document& document, const NodeId& id,
      const Rect& from, const Rect& to) {
    auto world = get_world_transform(document, id);
    return world * make_rect_mapping(from, to) * *invert(world);
  }

  std::unique_ptr<TransformCommand> make_drag(const Document& document,
      const NodeId& id, const Rect& from, const Rect& to, ResizeRule rule) {
    return std::make_unique<TransformCommand>(std::vector{id},
      make_local_mapping(document, id, from, to), "Resize",
      TransformMode::DRAG, rule, estimate_text_bounds);
  }

  Constraints make_constraints(bool left, bool right, bool top, bool bottom,
      bool width, bool height) {
    auto constraints = Constraints();
    constraints.m_left = left;
    constraints.m_right = right;
    constraints.m_top = top;
    constraints.m_bottom = bottom;
    constraints.m_width = width;
    constraints.m_height = height;
    return constraints;
  }

  struct Pair {
    NodeId m_container;
    NodeId m_first;
    NodeId m_second;
  };

  Pair build_pair(Document& document, Group group) {
    auto pair = Pair();
    auto transaction = Transaction(document);
    pair.m_container = transaction.append(NodeId(),
      make_header("Container", make_translation(100, 100)), std::move(group));
    pair.m_first = add_rectangle(
      transaction, pair.m_container, "Rectangle", Rect(0, 0, 20, 10));
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(10, 5), 10, 5);
    pair.m_second = transaction.append(pair.m_container,
      make_header("Ellipse", make_translation(60, 0)), ellipse);
    transaction.commit();
    document.clear_history();
    return pair;
  }

  Group make_main() {
    auto group = Group();
    group.m_component_instance =
      ComponentInstance(ComponentId(), StateId(), Size(80, 10));
    group.m_component_instance->m_is_main = true;
    return group;
  }

  void apply(Document& document, std::unique_ptr<Command> command) {
    auto transaction = Transaction(document);
    command->apply(document, transaction);
    transaction.commit();
  }
}

TEST_SUITE("ResponsiveResize") {
  TEST_CASE("inference") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300), true);
    auto corner = add_rectangle(
      transaction, artboard, "Corner", Rect(10, 10, 40, 20));
    auto centered = add_rectangle(
      transaction, artboard, "Centered", Rect(60, 100, 280, 40));
    auto filling = add_rectangle(
      transaction, artboard, "Filling", Rect(0, 0, 400, 250));
    auto contained = add_rectangle(
      transaction, artboard, "Contained", Rect(60, 40, 10, 10));
    auto area = make_text(u"Text");
    area.m_frame = AreaFrame(Size(280, 50));
    auto text = transaction.append(artboard,
      make_header("Text", make_translation(100, 100)), area);
    auto custom = make_header("Custom", make_translation(0, 0));
    custom.m_constraints.m_is_custom = true;
    custom.m_constraints.m_right = true;
    custom.m_constraints.m_top = true;
    custom.m_constraints.m_bottom = true;
    auto auto_height = make_text(u"Text");
    auto_height.m_frame = AutoHeightFrame(100);
    auto label = transaction.append(artboard, std::move(custom), auto_height);
    transaction.commit();
    auto require_constraints = [&] (const NodeId& id, bool left, bool right,
        bool top, bool bottom, bool width, bool height) {
      auto constraints = infer_constraints(document, id);
      REQUIRE(constraints.m_left == left);
      REQUIRE(constraints.m_right == right);
      REQUIRE(constraints.m_top == top);
      REQUIRE(constraints.m_bottom == bottom);
      REQUIRE(constraints.m_width == width);
      REQUIRE(constraints.m_height == height);
    };
    require_constraints(corner, true, false, true, false, true, true);
    require_constraints(centered, true, true, false, false, false, true);
    require_constraints(filling, true, true, true, true, false, false);
    require_constraints(contained, true, false, true, false, true, true);
    require_constraints(text, true, true, false, false, false, true);
    require_constraints(label, false, true, false, false, false, true);
    REQUIRE(infer_constraints(document, label).m_is_custom);
    REQUIRE(!infer_constraints(document, corner).m_is_custom);
  }

  TEST_CASE("artboard") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(100, 50, 400, 300), true);
    auto left =
      add_rectangle(transaction, artboard, "Left", Rect(10, 10, 40, 20));
    auto right =
      add_rectangle(transaction, artboard, "Right", Rect(350, 10, 40, 20));
    auto bar =
      add_rectangle(transaction, artboard, "Bar", Rect(0, 280, 400, 20));
    auto middle =
      add_rectangle(transaction, artboard, "Middle", Rect(180, 140, 40, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    require_undoable(history,
      make_bounds_command(document, artboard, Rect(0, 0, 600, 300)));
    REQUIRE(document.get(artboard).get_artboard()->m_frame ==
      Rect(100, 50, 600, 300));
    require_bounds(document, left, Rect(110, 60, 40, 20));
    require_bounds(document, right, Rect(650, 60, 40, 20));
    require_bounds(document, bar, Rect(100, 330, 600, 20));
    require_bounds(document, middle, Rect(380, 190, 40, 20));
    auto& constraints = document.get(left).m_header.m_constraints;
    REQUIRE(constraints == make_constraints(true, false, true, false, true,
      true));
    REQUIRE(history.undo());
    REQUIRE(document.get(left).m_header.m_constraints == Constraints());
    REQUIRE(history.redo());
    require_undoable(history,
      make_bounds_command(document, artboard, Rect(-100, 0, 700, 300)));
    require_bounds(document, left, Rect(10, 60, 40, 20));
    require_bounds(document, right, Rect(650, 60, 40, 20));
  }

  TEST_CASE("artboard_grid") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 300);
    artboard.m_viewport_height = 300;
    auto grid = LayoutGrid();
    grid.m_type = GridType::LAYOUT;
    grid.m_column_count = 4;
    grid.m_gutter = 10;
    grid.m_default_width = 400;
    grid.m_margins = Margins(0, 36, 0, 36);
    artboard.m_layout_grid = grid;
    artboard.m_guides = {Guide(Orientation::HORIZONTAL, 250),
      Guide(Orientation::VERTICAL, 250), Guide(Orientation::VERTICAL, 350),
      Guide(Orientation::HORIZONTAL, 0.5)};
    auto id =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    require_undoable(history,
      make_bounds_command(document, id, Rect(0, 0, 300, 200)));
    auto& resized = *document.get(id).get_artboard();
    REQUIRE(resized.m_viewport_height == 200);
    REQUIRE(resized.m_guides ==
      std::vector{Guide(Orientation::VERTICAL, 250)});
    REQUIRE(resized.m_layout_grid->m_default_width == 300);
    REQUIRE(resized.m_layout_grid->m_gutter == 10);
    REQUIRE(resized.m_layout_grid->m_margins == Margins(0, 37, 0, 37));
  }

  TEST_CASE("pixel_snapping") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300), true);
    auto middle =
      add_rectangle(transaction, artboard, "Middle", Rect(181, 140, 40, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(make_drag(document, artboard, Rect(0, 0, 400, 300),
      Rect(0, 0, 501, 300), ResizeRule::RESPONSIVE));
    require_bounds(document, middle, Rect(232, 140, 40, 20));
    REQUIRE(history.undo());
    history.execute(
      make_bounds_command(document, artboard, Rect(0, 0, 501, 300)));
    require_bounds(document, middle, Rect(231.7805556, 140, 40, 20));
  }

  TEST_CASE("instance") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    history.execute(
      make_bounds_command(document, scene.m_instance, Rect(0, 0, 160, 30)));
    require_bounds(document, scene.m_reference, Rect(400, 250, 160, 30));
    REQUIRE(!document.get(scene.m_reference).get_reference());
    REQUIRE(std::get<RectangleShape>(document.get(scene.m_background).
      get_shape()->m_geometry).m_bounds == Rect(0, 0, 80, 30));
    REQUIRE(history.undo());
    REQUIRE(document.get(scene.m_reference).get_reference());
    history.execute(
      make_bounds_command(document, scene.m_instance, Rect(-20, 0, 80, 30)));
    require_bounds(document, scene.m_reference, Rect(380, 250, 80, 30));
    REQUIRE(document.get(scene.m_reference).get_reference());
  }

  TEST_CASE("disabled_artboard") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard =
      add_artboard(transaction, Rect(100, 50, 400, 300), false);
    auto left =
      add_rectangle(transaction, artboard, "Left", Rect(10, 10, 40, 20));
    auto right =
      add_rectangle(transaction, artboard, "Right", Rect(350, 10, 40, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    require_undoable(history, make_drag(document, artboard,
      Rect(0, 0, 400, 300), Rect(-115, -20, 515, 320),
      ResizeRule::RESPONSIVE));
    REQUIRE(document.get(artboard).get_artboard()->m_frame ==
      Rect(-15, 30, 515, 320));
    require_bounds(document, left, Rect(-5, 40, 40, 20));
    require_bounds(document, right, Rect(335, 40, 40, 20));
    REQUIRE(history.undo());
    require_undoable(history,
      make_bounds_command(document, artboard, Rect(0, 0, 420, 200)));
    require_bounds(document, left, Rect(110, 60, 40, 20));
    require_bounds(document, right, Rect(450, 60, 40, 20));
  }

  TEST_CASE("suppressed") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300), true);
    auto right =
      add_rectangle(transaction, artboard, "Right", Rect(350, 10, 40, 20));
    transaction.commit();
    auto pair = build_pair(document, Group());
    auto history = History(document);
    require_undoable(history, make_drag(document, artboard,
      Rect(0, 0, 400, 300), Rect(-100, 0, 500, 300), ResizeRule::SUPPRESSED));
    require_bounds(document, right, Rect(250, 10, 40, 20));
    REQUIRE(document.get(right).m_header.m_constraints.m_right);
    require_undoable(history, make_drag(document, pair.m_container,
      Rect(0, 0, 80, 10), Rect(0, 0, 160, 20), ResizeRule::SUPPRESSED));
    require_bounds(document, pair.m_first, Rect(100, 100, 40, 20));
    require_bounds(document, pair.m_second, Rect(220, 100, 40, 20));
  }

  TEST_CASE("shrunk_artboard") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300), true);
    auto left =
      add_rectangle(transaction, artboard, "Left", Rect(10, 10, 40, 20));
    auto right =
      add_rectangle(transaction, artboard, "Right", Rect(350, 10, 40, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, artboard, Rect(0, 0, 30, 300)));
    require_bounds(document, left, Rect(-5, 10, 40, 20));
    require_bounds(document, right, Rect(-5, 10, 40, 20));
  }

  TEST_CASE("overhanging_child") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300), true);
    auto overhang = add_rectangle(
      transaction, artboard, "Overhang", Rect(-50, 10, 100, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, artboard, Rect(0, 0, 800, 300)));
    require_bounds(document, overhang, Rect(0, 10, 100, 20));
  }

  TEST_CASE("group") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(
      NodeId(), make_header("Group", make_translation(100, 100)), Group());
    auto left = add_rectangle(transaction, group, "Left", Rect(0, 0, 40, 20));
    auto right =
      add_rectangle(transaction, group, "Right", Rect(160, 0, 40, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    require_undoable(history,
      make_bounds_command(document, group, Rect(0, 0, 300, 20)));
    REQUIRE(document.get(group).m_header.m_transform ==
      make_translation(100, 100));
    require_bounds(document, left, Rect(100, 100, 40, 20));
    require_bounds(document, right, Rect(360, 100, 40, 20));
    REQUIRE(history.undo());
    require_undoable(history,
      make_bounds_command(document, group, Rect(-100, 0, 300, 20)));
    require_bounds(document, left, Rect(0, 100, 40, 20));
    require_bounds(document, right, Rect(260, 100, 40, 20));
    REQUIRE(document.get(right).m_header.m_transform ==
      make_translation(260, 0));
  }

  TEST_CASE("nested_group") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto outer = transaction.append(NodeId(), make_header("Outer"), Group());
    auto inner = transaction.append(outer, make_header("Inner"), Group());
    auto first =
      add_rectangle(transaction, inner, "First", Rect(0, 0, 20, 20));
    auto second =
      add_rectangle(transaction, inner, "Second", Rect(180, 0, 20, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, outer, Rect(0, 0, 400, 20)));
    require_bounds(document, first, Rect(0, 0, 20, 20));
    require_bounds(document, second, Rect(380, 0, 20, 20));
    REQUIRE(document.get(inner).m_header.m_constraints.m_left);
    REQUIRE(document.get(inner).m_header.m_constraints.m_right);
    REQUIRE(document.get(second).m_header.m_constraints.m_right);
  }

  TEST_CASE("mask") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto role = Group();
    role.m_mask = Mask();
    role.m_mask->m_shapes.push_back(
      MaskShape(Matrix(), RectangleShape(Rect(0, 0, 100, 100))));
    auto mask = transaction.append(NodeId(), make_header("Mask"), role);
    auto first =
      add_rectangle(transaction, mask, "First", Rect(0, 0, 50, 50));
    auto second =
      add_rectangle(transaction, mask, "Second", Rect(50, 50, 50, 50));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    require_undoable(history,
      make_bounds_command(document, mask, Rect(0, 0, 200, 100)));
    require_bounds(document, first, Rect(0, 0, 100, 50));
    require_bounds(document, second, Rect(100, 50, 100, 50));
    require_bounds(document, mask, Rect(0, 0, 200, 100));
    REQUIRE(!document.get(second).m_header.m_constraints.m_right);
  }

  TEST_CASE("main_component") {
    auto document = Document();
    auto pair = build_pair(document, make_main());
    apply(document, make_drag(document, pair.m_container,
      Rect(0, 0, 80, 10), Rect(0, 0, -10, 10), ResizeRule::RESPONSIVE));
    require_bounds(document, pair.m_first, Rect(90, 100, 20, 10));
    require_bounds(document, pair.m_second, Rect(80, 100, 20, 10));
    auto world = get_world_transform(document, pair.m_container);
    REQUIRE(world.m_a > 0);
    REQUIRE(world.m_b == 0);
    REQUIRE(world.m_d > 0);
    REQUIRE(std::get<RectangleShape>(document.get(pair.m_first).get_shape()->
      m_geometry).m_bounds == Rect(0, 0, 20, 10));
    auto& first = document.get(pair.m_first).m_header.m_constraints;
    REQUIRE(first.m_is_custom);
    REQUIRE(first.m_left);
    REQUIRE(!first.m_right);
    auto& second = document.get(pair.m_second).m_header.m_constraints;
    REQUIRE(second.m_is_custom);
    REQUIRE(second.m_right);
    REQUIRE(!second.m_left);
    auto far = Document();
    auto far_pair = build_pair(far, make_main());
    apply(far, make_drag(far, far_pair.m_container, Rect(0, 0, 80, 10),
      Rect(0, 0, -80, 10), ResizeRule::RESPONSIVE));
    require_bounds(far, far_pair.m_first, Rect(20, 100, 20, 10));
    require_bounds(far, far_pair.m_second, Rect(80, 100, 20, 10));
  }

  TEST_CASE("group_flip") {
    auto document = Document();
    auto pair = build_pair(document, Group());
    apply(document, make_drag(document, pair.m_container,
      Rect(0, 0, 80, 10), Rect(0, 0, -80, 10), ResizeRule::RESPONSIVE));
    require_bounds(document, pair.m_first, Rect(80, 100, 20, 10));
    require_bounds(document, pair.m_second, Rect(20, 100, 20, 10));
    REQUIRE(get_world_transform(document, pair.m_container).
      get_determinant() > 0);
  }

  TEST_CASE("multiple") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto left = add_rectangle(
      transaction, NodeId(), "Left", Rect(0, 0, 40, 20));
    auto right = add_rectangle(
      transaction, NodeId(), "Right", Rect(160, 0, 40, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    require_undoable(history, make_resize_command(std::vector{left, right},
      Rect(0, 0, 200, 20), Rect(0, 0, 300, 20)));
    require_bounds(document, left, Rect(0, 0, 40, 20));
    require_bounds(document, right, Rect(260, 0, 40, 20));
    REQUIRE(history.undo());
    require_undoable(history, std::make_unique<TransformCommand>(
      std::vector{left, right}, make_rect_mapping(Rect(0, 0, 200, 20),
        Rect(0, 0, -200, 20)), "Resize", TransformMode::DRAG,
      ResizeRule::RESPONSIVE, estimate_text_bounds));
    require_bounds(document, left, Rect(-40, 0, 40, 20));
    require_bounds(document, right, Rect(-200, 0, 40, 20));
  }

  TEST_CASE("stack") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto role = Group();
    role.m_stack = Stack(Orientation::HORIZONTAL);
    auto stack = transaction.append(
      NodeId(), make_header("Stack", make_translation(100, 100)), role);
    auto second =
      add_rectangle(transaction, stack, "Second", Rect(50, 0, 30, 10));
    auto first =
      add_rectangle(transaction, stack, "First", Rect(0, 0, 40, 10));
    auto payload = *document.get(stack).get_group();
    payload.m_stack->m_spacing.emplace(first, 10);
    transaction.replace_payload(stack, payload);
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, stack, Rect(0, 0, 80, 30)));
    require_bounds(document, first, Rect(100, 100, 40, 30));
    require_bounds(document, second, Rect(150, 100, 30, 30));
    history.execute(
      make_bounds_command(document, stack, Rect(-50, 0, 160, 30)));
    require_bounds(document, first, Rect(100, 100, 40, 30));
    require_bounds(document, second, Rect(150, 100, 30, 30));
  }

  TEST_CASE("padding") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto role = Group();
    role.m_padding = Padding(Margins(10, 10, 10, 10), true, true);
    auto box = transaction.append(NodeId(), make_header("Box"), role);
    auto background = add_rectangle(
      transaction, box, "Background", Rect(0, 0, 120, 60));
    auto content =
      add_rectangle(transaction, box, "Content", Rect(10, 10, 100, 40));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, box, Rect(0, 0, 220, 60)));
    require_bounds(document, content, Rect(10, 10, 200, 40));
    require_bounds(document, background, Rect(0, 0, 220, 60));
    REQUIRE(history.get_undo_count() == 1);
  }

  TEST_CASE("repeat_grid") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300), true);
    auto role = Group();
    role.m_repeat_grid = RepeatGrid(Size(200, 40), 0, 0, 2, 1, Size(400, 40));
    auto grid = transaction.append(artboard, make_header("Grid"), role);
    for(auto i = 0; i != 2; ++i) {
      auto cell = transaction.append(
        grid, make_header("", make_translation(200 * i, 0)), Group());
      add_rectangle(transaction, cell, "Cell", Rect(0, 0, 200, 40));
    }
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, artboard, Rect(0, 0, 600, 300)));
    REQUIRE(document.get(grid).m_children.size() == 3);
    REQUIRE(document.get(grid).get_group()->m_repeat_grid->m_size ==
      Size(600, 40));
    require_bounds(document, grid, Rect(0, 0, 600, 40));
  }

  TEST_CASE("aspect_lock") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300), true);
    auto header = make_header("Locked", make_translation(60, 100));
    header.m_aspect_lock = Size(280, 40);
    auto locked = transaction.append(
      artboard, std::move(header), make_rectangle_shape(280, 40));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, artboard, Rect(0, 0, 600, 300)));
    require_bounds(document, locked, Rect(60, 100, 280, 40));
  }

  TEST_CASE("custom_flip") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(NodeId(), make_header("Group"), Group());
    auto header = make_header("Pinned", make_translation(0, 0));
    header.m_constraints.m_is_custom = true;
    header.m_constraints.m_left = true;
    header.m_constraints.m_width = true;
    auto pinned = transaction.append(
      group, std::move(header), make_rectangle_shape(20, 20));
    add_rectangle(transaction, group, "Other", Rect(80, 0, 20, 20));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    require_undoable(history,
      make_flip_command(document, std::vector{group}, true));
    auto& constraints = document.get(pinned).m_header.m_constraints;
    REQUIRE(constraints.m_right);
    REQUIRE(!constraints.m_left);
    require_bounds(document, pinned, Rect(80, 0, 20, 20));
  }

  TEST_CASE("large_group") {
    constexpr auto COUNT = 10000;
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 2000, 2000), true);
    auto group = transaction.append(artboard, make_header("Group"), Group());
    auto last = NodeId();
    for(auto i = 0; i != COUNT; ++i) {
      last = add_rectangle(transaction, group, "Rectangle",
        Rect(20 * (i % 100), 20 * (i / 100), 10, 10));
    }
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    auto before = take_snapshot(document);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(history.execute(
      make_bounds_command(document, artboard, Rect(0, 0, 3000, 2000))));
    auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
    auto message = "Resizing an artboard holding " + std::to_string(COUNT) +
      " nodes: " + std::to_string(elapsed) + " ms";
    MESSAGE(message);
    require_bounds(document, last, Rect(2980, 1980, 10, 10));
    REQUIRE(history.undo());
    REQUIRE(take_snapshot(document) == before);
  }
}

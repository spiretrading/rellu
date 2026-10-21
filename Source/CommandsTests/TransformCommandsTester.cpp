#include <chrono>
#include <cmath>
#include <numbers>
#include <string>
#include <doctest/doctest.h>
#include "Rellu/Commands/TransformCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const RectangleShape& get_rectangle(
      const Document& document, const NodeId& id) {
    return std::get<RectangleShape>(document.get(id).get_shape()->m_geometry);
  }

  NodeHeader make_fixed_artboard_header() {
    auto header = make_header("Artboard");
    header.m_constraints.m_is_enabled = false;
    return header;
  }

  void disable_constraints(Document& document, const NodeId& id) {
    auto transaction = Transaction(document);
    auto header = document.get(id).m_header;
    header.m_constraints.m_is_enabled = false;
    transaction.replace_header(std::move(header));
    transaction.commit();
    document.clear_history();
  }
}

TEST_SUITE("TransformCommands") {
  TEST_CASE("rotation_of_moved_and_resized_nodes") {
    auto document = Document();
    auto turn = NodeId();
    auto quarter = NodeId();
    {
      auto transaction = Transaction(document);
      auto header = make_header("Turn", make_translation(10, 10));
      header.m_rotation = 360;
      turn = transaction.append(NodeId(), header, make_rectangle_shape(40, 20));
      header = make_header("Quarter",
        make_translation(100, 100) * make_rotation(-std::numbers::pi / 2));
      header.m_rotation = -90;
      quarter =
        transaction.append(NodeId(), header, make_rectangle_shape(40, 20));
      transaction.commit();
    }
    REQUIRE(document.get(turn).m_header.m_rotation == 360);
    auto history = History(document);
    require_undoable(history, make_nudge_command({turn, quarter}, Point(1, 0)));
    REQUIRE(document.get(turn).m_header.m_rotation == 360);
    REQUIRE(document.get(quarter).m_header.m_rotation == -90);
    require_undoable(history, make_bounds_command(
      document, quarter, Rect(0, 0, 50, 20)));
    REQUIRE(document.get(quarter).m_header.m_rotation == -90);
    auto bounds = *get_world_bounds(document, turn);
    require_undoable(history, make_resize_command({turn}, bounds,
      Rect(bounds.m_x, bounds.m_y, bounds.m_width + 10, bounds.m_height)));
    REQUIRE(document.get(turn).m_header.m_rotation == 0);
  }

  TEST_CASE("rotation_drag") {
    auto drag = RotationDrag({0}, {0});
    drag.update(10.4, false);
    REQUIRE(drag.get_turn() == 10);
    REQUIRE(drag.get_rotations() == std::vector<double>{10});
    drag.update(350, false);
    REQUIRE(drag.get_turn() == 350);
    REQUIRE(drag.get_rotations() == std::vector<double>{-10});
    drag.update(270, false);
    REQUIRE(drag.get_rotations() == std::vector<double>{-90});
    drag.update(5, false);
    REQUIRE(drag.get_rotations() == std::vector<double>{5});
    drag.update(359.6, false);
    REQUIRE(drag.get_turn() == 0);
    REQUIRE(drag.get_rotations() == std::vector<double>{0});
    auto snapped = RotationDrag({-90}, {-90});
    snapped.update(8, true);
    REQUIRE(snapped.get_turn() == 15);
    REQUIRE(snapped.get_rotations() == std::vector<double>{-75});
    snapped.update(95, true);
    REQUIRE(snapped.get_turn() == 90);
    REQUIRE(snapped.get_rotations() == std::vector<double>{0});
    auto closing = RotationDrag({-270}, {90});
    closing.update(270, false);
    REQUIRE(closing.get_rotations() == std::vector<double>{-360});
    closing.update(260, false);
    REQUIRE(closing.get_rotations() == std::vector<double>{-10});
    auto full = RotationDrag({360}, {0});
    full.update(0.2, false);
    REQUIRE(full.get_rotations() == std::vector<double>{360});
    full.update(30, false);
    REQUIRE(full.get_rotations() == std::vector<double>{30});
    auto several = RotationDrag({30, -90}, {30, -90});
    several.update(20, false);
    REQUIRE(several.get_rotations() == std::vector<double>{50, 290});
  }

  TEST_CASE("rotation_drag_fraction") {
    auto drag = RotationDrag({30.4}, {30.4});
    REQUIRE(drag.get_fractions().size() == 1);
    REQUIRE(drag.get_fractions().front() == doctest::Approx(0.4));
    drag.update(90, false);
    REQUIRE(drag.get_turn() == 90);
    REQUIRE(drag.get_rotations() == std::vector<double>{120});
    auto negative = RotationDrag({-0.5}, {-0.5});
    REQUIRE(negative.get_fractions().front() == doctest::Approx(-0.5));
    negative.update(359, false);
    REQUIRE(negative.get_rotations() == std::vector<double>{-1});
    auto whole = RotationDrag({180.00001525878906}, {180});
    REQUIRE(whole.get_fractions().front() ==
      doctest::Approx(0.00001525878906));
    whole.update(10, false);
    REQUIRE(whole.get_rotations() == std::vector<double>{190});
  }

  TEST_CASE("rotate_command_adjustments") {
    auto document = Document();
    auto id = NodeId();
    auto center = Point(120, 110);
    {
      auto transaction = Transaction(document);
      auto header = make_header("Turned", make_rotation(
        30.4 * std::numbers::pi / 180, center) * make_translation(100, 100));
      header.m_rotation = 30.4;
      id = transaction.append(NodeId(), header, make_rectangle_shape(40, 20));
      transaction.commit();
    }
    auto command = make_rotate_command(std::vector{id}, std::numbers::pi / 4,
      center, std::vector<double>{75});
    command->set_adjustments(
      std::vector{make_rotation(-0.4 * std::numbers::pi / 180, center)});
    REQUIRE(command->merge(*make_rotate_command(std::vector{id},
      std::numbers::pi / 4, center, std::vector<double>{120})));
    REQUIRE(command->get_adjustments().size() == 1);
    auto history = History(document);
    require_undoable(history, std::move(command));
    auto& header = document.get(id).m_header;
    REQUIRE(header.m_rotation == 120);
    REQUIRE(get_rotation(header.m_transform) == doctest::Approx(120));
    REQUIRE(distance(header.m_transform.map(Point(20, 10)), center) < 1e-9);
  }

  TEST_CASE("rotate_command_rotations") {
    auto document = Document();
    auto id = NodeId();
    {
      auto transaction = Transaction(document);
      id = transaction.append(NodeId(), make_header("Turned"),
        make_rectangle_shape(40, 20));
      transaction.commit();
    }
    auto history = History(document);
    require_undoable(history, make_rotate_command(std::vector{id},
      -std::numbers::pi / 2, Point(20, 10), std::vector<double>{-90}));
    REQUIRE(document.get(id).m_header.m_rotation == -90);
    auto command = make_rotate_command(std::vector{id}, -std::numbers::pi / 2,
      Point(20, 10), std::vector<double>{-180});
    REQUIRE(command->merge(*make_rotate_command(std::vector{id},
      -std::numbers::pi / 2, Point(20, 10), std::vector<double>{-270})));
    REQUIRE(command->get_rotations() == std::vector<double>{-270});
    auto merged = History(document);
    require_undoable(merged, std::move(command));
    REQUIRE(document.get(id).m_header.m_rotation == -270);
    REQUIRE(is_congruent(-270, document.get(id).m_header.m_transform));
  }

  TEST_CASE("translate") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto ellipse = get_world_transform(document, scene.m_ellipse);
    require_undoable(history, std::make_unique<TranslateCommand>(
      std::vector{scene.m_rectangle, scene.m_ellipse}, Point(5, -3)));
    REQUIRE(history.get_undo_label() == "Move");
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(115, 57));
    require_near(get_world_transform(document, scene.m_ellipse),
      make_translation(5, -3) * ellipse);
  }

  TEST_CASE("translate_nested") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto ellipse = get_world_transform(document, scene.m_ellipse);
    require_undoable(history, std::make_unique<TranslateCommand>(
      std::vector{scene.m_ellipse, scene.m_group}, Point(10, 0)));
    require_near(get_world_transform(document, scene.m_ellipse),
      make_translation(10, 0) * ellipse);
    REQUIRE(document.get(scene.m_ellipse).m_header.m_transform ==
      make_translation(5, 5));
  }

  TEST_CASE("translate_artboard") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<TranslateCommand>(
      std::vector{scene.m_artboard}, Point(10, 20)));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_frame ==
      Rect(110, 70, 400, 300));
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(120, 80));
  }

  TEST_CASE("translate_reference") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<TranslateCommand>(
      std::vector{scene.m_reference}, Point(1, 2)));
    REQUIRE(document.get(scene.m_reference).get_reference()->m_transform ==
      make_translation(1, 2));
    REQUIRE(document.get(scene.m_background).m_header.m_transform ==
      Matrix());
    REQUIRE(get_world_bounds(document, scene.m_reference) ==
      Rect(401, 252, 80, 30));
  }

  TEST_CASE("nudge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle};
    REQUIRE(history.merge(make_nudge_command(nodes, Point(1, 0))));
    REQUIRE(history.merge(make_nudge_command(nodes, Point(1, 0))));
    REQUIRE(history.merge(make_nudge_command(nodes, Point(0, 10))));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Nudge");
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(112, 70));
    REQUIRE(history.merge(
      std::make_unique<TranslateCommand>(nodes, Point(1, 1))));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(110, 60));
  }

  TEST_CASE("large_nudge") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 400, 300);
    auto id = transaction.append(NodeId(), make_header("Board"), artboard);
    transaction.commit();
    auto right = Point(1, 0);
    auto up = Point(0, -1);
    REQUIRE(get_large_nudge(document, id, Point(103, 50), right) ==
      Point(10, 0));
    REQUIRE(get_large_nudge(document, NodeId(), Point(103, 50), up) ==
      Point(0, -10));
    artboard.m_layout_grid = LayoutGrid();
    artboard.m_layout_grid->m_square_size = Size(8, 6);
    auto change = Transaction(document);
    change.replace_payload(id, artboard);
    change.commit();
    REQUIRE(get_large_nudge(document, id, Point(103, 50), right) ==
      Point(10, 0));
    artboard.m_layout_grid->m_is_visible = true;
    auto show = Transaction(document);
    show.replace_payload(id, artboard);
    show.commit();
    REQUIRE(get_large_nudge(document, id, Point(103, 50), right) ==
      Point(5, 0));
    REQUIRE(get_large_nudge(document, id, Point(103, 50), Point(-1, 0)) ==
      Point(-3, 0));
    REQUIRE(get_large_nudge(document, id, Point(108, 50), right) ==
      Point(8, 0));
    REQUIRE(get_large_nudge(document, id, Point(108, 50), Point(-1, 0)) ==
      Point(-8, 0));
    REQUIRE(get_large_nudge(document, id, Point(96, 52), Point(-1, 0)) ==
      Point(-4, 0));
    REQUIRE(get_large_nudge(document, id, Point(103, 52), up) ==
      Point(0, -2));
    REQUIRE(get_large_nudge(document, id, Point(103, 52), Point(0, 1)) ==
      Point(0, 4));
    artboard.m_layout_grid->m_square_size = Size(8, 8);
    auto square = Transaction(document);
    square.replace_payload(id, artboard);
    square.commit();
    REQUIRE(get_large_nudge(document, id, Point(103, 352), Point(0, 1)) ==
      Point(0, 2));
    REQUIRE(get_large_nudge(document, id, Point(103, 352), up) ==
      Point(0, -6));
    artboard.m_layout_grid->m_type = GridType::LAYOUT;
    auto layout = Transaction(document);
    layout.replace_payload(id, artboard);
    layout.commit();
    REQUIRE(get_large_nudge(document, id, Point(103, 50), right) ==
      Point(10, 0));
  }

  TEST_CASE("keep_on_canvas") {
    auto& canvas = CANVAS_BOUNDS;
    auto bounds = Rect(canvas.get_right() - 15, 0, 10, 10);
    REQUIRE(keep_on_canvas(bounds, Point(3, 7)) == Point(3, 7));
    REQUIRE(keep_on_canvas(bounds, Point(10, 2)) == Point(5, 2));
    REQUIRE(keep_on_canvas(Rect(canvas.get_left() + 2,
      canvas.get_bottom() - 20, 10, 10), Point(-10, 30)) == Point(-2, 10));
    auto wide = Rect(canvas.get_left() - 10, 0, canvas.m_width + 20, 10);
    REQUIRE(keep_on_canvas(wide, Point(5, 0)) == Point(5, 0));
    REQUIRE(keep_on_canvas(wide, Point(canvas.m_width, 0)) ==
      Point(canvas.m_width, 0));
    REQUIRE(keep_on_canvas(wide, Point(canvas.m_width + 10, 0)) ==
      Point(canvas.m_width, 0));
    REQUIRE(keep_on_canvas(Rect(0, 0, 14, 80), Point(99999, 99999)) ==
      Point(24986, 24920));
    REQUIRE(keep_on_canvas(Rect(0, 0, 14, 80), Point(-99999, -99999)) ==
      Point(-25000, -25000));
  }

  TEST_CASE("resize") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, make_resize_command(
      {scene.m_rectangle}, Rect(110, 60, 40, 20), Rect(100, 60, 80, 40)));
    REQUIRE(history.get_undo_label() == "Resize");
    REQUIRE(get_rectangle(document, scene.m_rectangle).m_bounds ==
      Rect(0, 0, 80, 40));
    REQUIRE(document.get(scene.m_rectangle).m_header.m_transform ==
      make_translation(0, 10));
    REQUIRE(document.get(scene.m_rectangle).get_shape()->m_style.m_stroke->
      m_width == 4);
    REQUIRE(get_world_bounds(document, scene.m_rectangle) ==
      Rect(100, 60, 80, 40));
  }

  TEST_CASE("resize_rotated") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto world = get_world_transform(document, scene.m_text);
    require_undoable(history,
      make_bounds_command(document, scene.m_text, Rect(0, 0, 200, 40)));
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(std::get<AreaFrame>(text.m_frame).m_size.m_width ==
      doctest::Approx(200));
    REQUIRE(std::get<AreaFrame>(text.m_frame).m_size.m_height ==
      doctest::Approx(40));
    require_near(get_world_transform(document, scene.m_text), world);
  }

  TEST_CASE("resize_skew") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto world = get_world_transform(document, scene.m_text);
    auto bounds = *get_world_bounds(document, scene.m_text);
    auto target = Rect(bounds.m_x, bounds.m_y, 2 * bounds.m_width,
      bounds.m_height);
    require_undoable(
      history, make_resize_command({scene.m_text}, bounds, target));
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(std::get<AreaFrame>(text.m_frame).m_size == Size(100, 40));
    require_near(get_world_transform(document, scene.m_text),
      make_rect_mapping(bounds, target) * world);
  }

  TEST_CASE("resize_group") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto group = document.get(scene.m_group).m_header.m_transform;
    auto bounds = *get_local_bounds(document, scene.m_group);
    REQUIRE(bounds.m_x == doctest::Approx(0));
    require_undoable(history, make_bounds_command(document, scene.m_group,
      Rect(bounds.m_x, bounds.m_y, 2 * bounds.m_width, bounds.m_height)));
    require_near(document.get(scene.m_group).m_header.m_transform, group);
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(std::get<AreaFrame>(text.m_frame).m_size.m_width ==
      doctest::Approx(200));
    require_near(document.get(scene.m_text).m_header.m_transform,
      make_translation(0, 30));
    auto& ellipse = std::get<EllipseShape>(
      document.get(scene.m_ellipse).get_shape()->m_geometry);
    REQUIRE(ellipse.m_center.m_x == doctest::Approx(10));
    REQUIRE(ellipse.m_x_radius == doctest::Approx(10));
    require_near(document.get(scene.m_ellipse).m_header.m_transform,
      make_translation(5, 5));
  }

  TEST_CASE("scale_group") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto group = document.get(scene.m_group).m_header.m_transform;
    auto bounds = *get_local_bounds(document, scene.m_group);
    auto world = get_world_transform(document, scene.m_group);
    auto target =
      Rect(bounds.m_x, bounds.m_y, 2 * bounds.m_width, bounds.m_height);
    require_undoable(history, std::make_unique<TransformCommand>(
      std::vector{scene.m_group},
      world * make_rect_mapping(bounds, target) * *invert(world), "Resize",
      TransformMode::RESIZE, ResizeRule::SUPPRESSED, estimate_text_bounds));
    require_near(document.get(scene.m_group).m_header.m_transform, group);
    auto& text = *document.get(scene.m_text).get_text();
    REQUIRE(std::get<AreaFrame>(text.m_frame).m_size.m_width ==
      doctest::Approx(200));
    auto& ellipse = std::get<EllipseShape>(
      document.get(scene.m_ellipse).get_shape()->m_geometry);
    REQUIRE(ellipse.m_center.m_x == doctest::Approx(20));
    REQUIRE(ellipse.m_center.m_y == doctest::Approx(10));
    REQUIRE(ellipse.m_x_radius == doctest::Approx(20));
    REQUIRE(ellipse.m_y_radius == doctest::Approx(5));
    require_near(document.get(scene.m_ellipse).m_header.m_transform,
      make_translation(10, 5));
  }

  TEST_CASE("resize_mask") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history,
      make_bounds_command(document, scene.m_mask, Rect(0, 0, 40, 40)));
    auto& mask = *document.get(scene.m_mask).get_group()->m_mask;
    require_near(mask.m_shapes.front().m_transform, make_scale(2, 2));
    REQUIRE(get_world_bounds(document, scene.m_mask) ==
      Rect(150, 200, 40, 40));
    REQUIRE(get_local_bounds(document, scene.m_path) == Rect(0, 0, 60, 60));
  }

  TEST_CASE("resize_artboard") {
    auto document = Document();
    auto scene = build_scene(document);
    disable_constraints(document, scene.m_artboard);
    auto history = History(document);
    require_undoable(history, make_bounds_command(
      document, scene.m_artboard, Rect(-50, 0, 450, 300)));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_frame ==
      Rect(50, 50, 450, 300));
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(60, 60));
  }

  TEST_CASE("positioned_text") {
    auto document = Document();
    auto text = NodeId();
    {
      auto transaction = Transaction(document);
      auto payload = make_text(u"Label");
      payload.m_frame = PositionedFrame();
      text = transaction.append(NodeId(), make_header("Label"), payload);
      transaction.commit();
    }
    auto history = History(document);
    require_undoable(history, make_resize_command(
      {text}, Rect(0, 0, 10, 10), Rect(5, 0, 25, 20)));
    auto& resized = *document.get(text).get_text();
    REQUIRE(resized.m_base_style.m_font.m_size == 28);
    REQUIRE(resized.m_runs.front().m_style.m_font.m_size == 28);
    REQUIRE(document.get(text).m_header.m_transform ==
      make_translation(5, 0));
    history.execute(make_resize_command(
      {text}, Rect(5, -22.4, 70, 33.6), Rect(5, -22.4, 140, 33.6)));
    REQUIRE(document.get(text).get_text()->m_base_style.m_font.m_size == 28);
  }

  TEST_CASE("rotate") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto rotation = make_rotation(std::numbers::pi / 2, Point(130, 70));
    require_undoable(history, make_rotate_command(
      {scene.m_rectangle}, std::numbers::pi / 2, Point(130, 70)));
    REQUIRE(history.get_undo_label() == "Rotate");
    require_near(get_world_transform(document, scene.m_rectangle),
      rotation * make_translation(110, 60));
    REQUIRE(get_rectangle(document, scene.m_rectangle).m_bounds ==
      Rect(0, 0, 40, 20));
    REQUIRE(!history.execute(make_rotate_command(
      {scene.m_artboard}, std::numbers::pi / 2, Point(0, 0))));
  }

  TEST_CASE("merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle};
    auto before = take_snapshot(document);
    REQUIRE(history.merge(make_resize_command(
      nodes, Rect(110, 60, 40, 20), Rect(110, 60, 60, 20))));
    REQUIRE(history.merge(make_resize_command(
      nodes, Rect(110, 60, 60, 20), Rect(110, 60, 80, 40))));
    REQUIRE(history.merge(make_rotate_command(nodes, 0.1, Point(0, 0))));
    REQUIRE(history.merge(make_rotate_command(nodes, 0.2, Point(0, 0))));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(get_rectangle(document, scene.m_rectangle).m_bounds ==
      Rect(0, 0, 80, 40));
    require_near(get_world_transform(document, scene.m_rectangle),
      make_rotation(0.3) * make_translation(110, 60));
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(take_snapshot(document) == before);
  }

  TEST_CASE("scale") {
    constexpr auto COUNT = 10000;
    auto document = Document();
    auto nodes = std::vector<NodeId>();
    {
      auto transaction = Transaction(document);
      auto artboard = Artboard();
      artboard.m_frame = Rect(0, 0, 1000, 1000);
      auto parent =
        transaction.append(NodeId(), make_header("Artboard"), artboard);
      auto group = transaction.append(parent, make_header("Group"), Group());
      auto shape = make_rectangle_shape(10, 10);
      for(auto i = 0; i != COUNT; ++i) {
        nodes.push_back(transaction.append(group,
          make_header("Rectangle", make_translation(i % 100, i / 100)),
          shape));
      }
      transaction.commit();
    }
    auto history = History(document);
    auto before = take_snapshot(document);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(history.execute(
      std::make_unique<TranslateCommand>(nodes, Point(3, 4))));
    auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
    auto message = "Moving " + std::to_string(COUNT) + " nodes: " +
      std::to_string(elapsed) + " ms";
    MESSAGE(message);
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get(nodes.back()).m_header.m_transform ==
      make_translation(102, 103));
    REQUIRE(history.undo());
    REQUIRE(take_snapshot(document) == before);
    REQUIRE(history.redo());
    REQUIRE(document.get(nodes.front()).m_header.m_transform ==
      make_translation(3, 4));
  }

  TEST_CASE("mirror") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto rectangle = make_rectangle_shape(40, 20);
    std::get<RectangleShape>(rectangle.m_geometry).m_radii =
      CornerRadii(1, 2, 3, 4);
    auto image = ImageFill();
    image.m_size = Size(10, 10);
    rectangle.m_style.m_fill = image;
    auto rectangle_id = transaction.append(NodeId(),
      make_header("Rectangle", make_translation(10, 10)), rectangle);
    auto triangle = Path();
    triangle.move_to(Point(0, 0));
    triangle.line_to(Point(30, 0));
    triangle.line_to(Point(30, 30));
    triangle.close();
    auto path = transaction.append(NodeId(),
      make_header("Path", make_translation(100, 0)),
      Shape(PathShape(Shared(std::move(triangle))), Style()));
    auto group = transaction.append(
      NodeId(), make_header("Group", make_translation(300, 0)), Group());
    auto line = transaction.append(group, make_header("Line"),
      Shape(LineShape(Point(0, 0), Point(10, 5)), Style()));
    auto text = transaction.append(NodeId(),
      make_header("Text", make_translation(200, 0)), make_text(u"Hello"));
    transaction.commit();
    document.clear_history();
    auto text_bounds = *get_world_bounds(document, text);
    auto history = History(document);
    require_undoable(history, std::make_unique<TransformCommand>(
      std::vector{rectangle_id, path, group, text}, make_scale(-1, 1),
      "Flip"));
    for(auto& id : {rectangle_id, path, group, line}) {
      REQUIRE(get_world_transform(document, id).get_determinant() > 0);
    }
    REQUIRE(get_world_transform(document, text).get_determinant() > 0);
    REQUIRE(document.get(text).get_text()->m_is_flipped_vertically);
    REQUIRE(*get_world_bounds(document, rectangle_id) ==
      Rect(-50, 10, 40, 20));
    REQUIRE(get_rectangle(document, rectangle_id).m_radii ==
      CornerRadii(2, 1, 4, 3));
    auto& fill = std::get<ImageFill>(
      document.get(rectangle_id).get_shape()->m_style.m_fill);
    REQUIRE(fill.m_is_flipped_horizontally);
    REQUIRE(!fill.m_is_flipped_vertically);
    auto outline = transform(*std::get<PathShape>(
      document.get(path).get_shape()->m_geometry).m_path,
      get_world_transform(document, path));
    REQUIRE(get_bounds(outline) == Rect(-130, 0, 30, 30));
    REQUIRE(contains(outline, Point(-128, 25)));
    REQUIRE(!contains(outline, Point(-102, 25)));
    auto& segment =
      std::get<LineShape>(document.get(line).get_shape()->m_geometry);
    auto world = get_world_transform(document, line);
    REQUIRE(world.map(segment.m_start).m_x == doctest::Approx(-300));
    REQUIRE(world.map(segment.m_start).m_y == doctest::Approx(0));
    REQUIRE(world.map(segment.m_end).m_x == doctest::Approx(-310));
    REQUIRE(world.map(segment.m_end).m_y == doctest::Approx(5));
    require_near(*get_world_bounds(document, text),
      Rect(-text_bounds.get_right(), text_bounds.m_y, text_bounds.m_width,
        text_bounds.m_height));
  }

  TEST_CASE("mirror_text") {
    auto document = Document();
    auto text = NodeId();
    {
      auto transaction = Transaction(document);
      auto payload = make_text(u"Label");
      payload.m_frame = AutoHeightFrame(100);
      text = transaction.append(
        NodeId(), make_header("Label", make_translation(10, 0)), payload);
      transaction.commit();
    }
    auto bounds = *get_world_bounds(document, text);
    auto history = History(document);
    require_undoable(history, make_resize_command({text}, bounds,
      Rect(bounds.m_x, bounds.m_y, -20, bounds.m_height)));
    REQUIRE(std::get<AutoHeightFrame>(
      document.get(text).get_text()->m_frame).m_width == doctest::Approx(20));
    auto world = get_world_transform(document, text);
    REQUIRE(world.get_determinant() > 0);
    REQUIRE(std::atan2(world.m_b, world.m_a) ==
      doctest::Approx(std::numbers::pi));
    REQUIRE(document.get(text).get_text()->m_is_flipped_vertically);
    require_near(*get_world_bounds(document, text),
      Rect(bounds.m_x - 20, bounds.m_y, 20, bounds.m_height));
  }

  TEST_CASE("mirror_instance") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto role = Group();
    role.m_component_instance =
      ComponentInstance(ComponentId(), StateId(), Size(80, 10));
    role.m_component_instance->m_is_main = true;
    auto instance = transaction.append(
      NodeId(), make_header("Main", make_translation(100, 100)), role);
    auto left = transaction.append(instance, make_header("Left"),
      make_rectangle_shape(20, 10));
    auto right = transaction.append(instance,
      make_header("Right", make_translation(60, 0)),
      make_rectangle_shape(20, 10));
    transform_node(document, transaction, instance,
      make_translation(280, 0) * make_scale(-1, 1));
    transaction.commit();
    REQUIRE(get_world_transform(document, instance) ==
      make_translation(100, 100));
    REQUIRE(*get_world_bounds(document, left) == Rect(100, 100, 20, 10));
    REQUIRE(*get_world_bounds(document, right) == Rect(160, 100, 20, 10));
  }

  TEST_CASE("mirror_mask") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto role = Group();
    role.m_mask = Mask();
    role.m_mask->m_shapes.push_back(MaskShape(make_translation(10, 0),
      RectangleShape(Rect(0, 0, 30, 20), CornerRadii(5, 0, 0, 0))));
    auto mask = transaction.append(
      NodeId(), make_header("Mask", make_translation(100, 100)), role);
    auto content = transaction.append(mask, make_header("Content"),
      make_rectangle_shape(60, 20));
    transform_node(document, transaction, mask,
      make_translation(260, 0) * make_scale(-1, 1));
    transaction.commit();
    REQUIRE(get_world_transform(document, mask).get_determinant() > 0);
    auto& shape = document.get(mask).get_group()->m_mask->m_shapes.front();
    REQUIRE(shape.m_transform.get_determinant() > 0);
    REQUIRE(get_world_transform(document, mask).map(shape.m_transform.map(
      std::get<RectangleShape>(shape.m_geometry).m_bounds)) ==
        Rect(120, 100, 30, 20));
    REQUIRE(std::get<RectangleShape>(shape.m_geometry).m_radii ==
      CornerRadii(0, 5, 0, 0));
    REQUIRE(*get_world_bounds(document, content) == Rect(100, 100, 60, 20));
  }

  TEST_CASE("resize_artboard_contents") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 300);
    auto board =
      transaction.append(NodeId(), make_fixed_artboard_header(), artboard);
    auto inner = transaction.append(board,
      make_header("Inner", make_translation(10, 10)),
      make_rectangle_shape(40, 40));
    auto outer = transaction.append(board,
      make_header("Outer", make_translation(350, 10)),
      make_rectangle_shape(40, 40));
    auto loose = transaction.append(NodeId(),
      make_header("Loose", make_translation(500, 10)),
      make_rectangle_shape(40, 40));
    auto other = Artboard();
    other.m_frame = Rect(0, 1000, 100, 100);
    auto distant = transaction.append(NodeId(), make_header("Other"), other);
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, board, Rect(0, 0, 200, 300)));
    REQUIRE(document.get_canvas() ==
      std::vector{board, loose, distant, outer});
    REQUIRE(document.get(board).m_children == std::vector{inner});
    REQUIRE(*get_world_bounds(document, outer) == Rect(350, 10, 40, 40));
    history.execute(
      make_bounds_command(document, board, Rect(0, 0, 510, 300)));
    REQUIRE(document.get_canvas() == std::vector{board, distant});
    REQUIRE(document.get(board).m_children ==
      std::vector{inner, loose, outer});
    REQUIRE(*get_world_bounds(document, loose) == Rect(500, 10, 40, 40));
    history.undo();
    history.undo();
    REQUIRE(document.get_canvas() == std::vector{board, loose, distant});
    REQUIRE(document.get(board).m_children == std::vector{inner, outer});
  }

  TEST_CASE("release_to_artboard") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 300);
    auto board =
      transaction.append(NodeId(), make_fixed_artboard_header(), artboard);
    auto shape = transaction.append(board,
      make_header("Shape", make_translation(350, 10)),
      make_rectangle_shape(40, 40));
    auto other = Artboard();
    other.m_frame = Rect(380, 0, 100, 100);
    auto neighbour = transaction.append(NodeId(), make_header("Other"), other);
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, board, Rect(0, 0, 200, 300)));
    REQUIRE(document.get(neighbour).m_children == std::vector{shape});
    REQUIRE(*get_world_bounds(document, shape) == Rect(350, 10, 40, 40));
  }

  TEST_CASE("dragged_artboard") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 300);
    auto board =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    auto loose = transaction.append(NodeId(),
      make_header("Loose", make_translation(420, 10)),
      make_rectangle_shape(40, 40));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(std::make_unique<TransformCommand>(std::vector{board},
      make_scale(1.25, 1), "Resize", TransformMode::DRAG));
    REQUIRE(document.get(board).get_artboard()->m_frame ==
      Rect(0, 0, 500, 300));
    REQUIRE(document.get(loose).m_parent.is_nil());
    REQUIRE(history.merge(std::make_unique<TransformCommand>(
      std::vector{board}, Matrix(), "Resize", TransformMode::SETTLE)));
    REQUIRE(document.get(loose).m_parent == board);
    REQUIRE(history.get_undo_count() == 1);
  }
}

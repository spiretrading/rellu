#include <chrono>
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
}

TEST_SUITE("TransformCommands") {
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
    auto history = History(document);
    require_undoable(history, make_bounds_command(
      document, scene.m_artboard, Rect(-50, 0, 450, 300)));
    REQUIRE(document.get(scene.m_artboard).get_artboard()->m_frame ==
      Rect(50, 50, 450, 300));
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(110, 60));
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
      {text}, Rect(0, 0, 10, 10), Rect(0, 0, 20, 10)));
    REQUIRE(document.get(text).m_header.m_transform == make_scale(2, 1));
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
}

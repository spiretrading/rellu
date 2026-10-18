#include <doctest/doctest.h>
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("Bounds") {
  TEST_CASE("transforms") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(get_transform(document, scene.m_artboard) ==
      make_translation(100, 50));
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(110, 60));
    REQUIRE(get_parent_transform(document, scene.m_rectangle) ==
      make_translation(100, 50));
    REQUIRE(get_parent_transform(document, scene.m_loose) == Matrix());
    REQUIRE(get_world_transform(document, NodeId()) == Matrix());
    require_near(get_world_transform(document, scene.m_ellipse),
      make_translation(300, 150) * make_rotation(std::numbers::pi / 6) *
        make_translation(5, 5));
    REQUIRE(get_world_transform(document, scene.m_reference) ==
      make_translation(400, 250));
  }

  TEST_CASE("reference_transform") {
    auto document = Document();
    auto scene = build_scene(document);
    {
      auto transaction = Transaction(document);
      auto reference = *document.get(scene.m_reference).get_reference();
      reference.m_transform = make_translation(5, 6);
      transaction.replace_payload(scene.m_reference, reference);
      transaction.commit();
    }
    REQUIRE(get_transform(document, scene.m_reference) ==
      make_translation(5, 6));
    REQUIRE(get_world_bounds(document, scene.m_reference) ==
      Rect(405, 256, 80, 30));
  }

  TEST_CASE("local_bounds") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(get_local_bounds(document, scene.m_rectangle) ==
      Rect(0, 0, 40, 20));
    REQUIRE(get_local_bounds(document, scene.m_artboard) ==
      Rect(0, 0, 400, 300));
    REQUIRE(get_local_bounds(document, scene.m_text) == Rect(0, 0, 100, 40));
    REQUIRE(get_local_bounds(document, scene.m_mask) == Rect(0, 0, 20, 20));
    REQUIRE(!get_local_bounds(document, generate_node_id()));
    auto group = NodeId();
    {
      auto transaction = Transaction(document);
      group = transaction.append(NodeId(), make_header("Empty"), Group());
      transaction.commit();
    }
    REQUIRE(!get_local_bounds(document, group));
  }

  TEST_CASE("world_bounds") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(get_world_bounds(document, scene.m_rectangle) ==
      Rect(110, 60, 40, 20));
    REQUIRE(get_world_bounds(document, scene.m_reference) ==
      Rect(400, 250, 80, 30));
    REQUIRE(get_world_bounds(document, scene.m_instance) ==
      Rect(400, 250, 80, 30));
    REQUIRE(get_world_bounds(document, scene.m_mask) ==
      Rect(150, 200, 20, 20));
    require_near(*get_world_bounds(document, scene.m_text),
      get_world_transform(document, scene.m_text).map(Rect(0, 0, 100, 40)));
    require_near(*get_world_bounds(document,
      std::vector{scene.m_rectangle, scene.m_reference}),
      Rect(110, 60, 370, 220));
  }

  TEST_CASE("hidden_descendants") {
    auto document = Document();
    auto scene = build_scene(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_ellipse).m_header;
      header.m_is_visible = false;
      transaction.replace_header(header);
      transaction.commit();
    }
    require_near(*get_world_bounds(document, scene.m_group),
      *get_world_bounds(document, scene.m_text));
    REQUIRE(get_world_bounds(document, scene.m_ellipse));
  }

  TEST_CASE("stroke_alignment") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(get_world_bounds(document, scene.m_rectangle,
      BoundsType::VISUAL) == Rect(108, 58, 44, 24));
    auto align = [&] (StrokeAlignment alignment) {
      auto transaction = Transaction(document);
      auto shape = *document.get(scene.m_rectangle).get_shape();
      shape.m_style.m_stroke->m_alignment = alignment;
      transaction.replace_payload(scene.m_rectangle, shape);
      transaction.commit();
    };
    align(StrokeAlignment::INSIDE);
    REQUIRE(get_world_bounds(document, scene.m_rectangle,
      BoundsType::VISUAL) == Rect(110, 60, 40, 20));
    align(StrokeAlignment::OUTSIDE);
    REQUIRE(get_world_bounds(document, scene.m_rectangle,
      BoundsType::VISUAL) == Rect(106, 56, 48, 28));
    REQUIRE(get_world_bounds(document, scene.m_rectangle) ==
      Rect(110, 60, 40, 20));
  }

  TEST_CASE("line") {
    auto document = Document();
    auto line = NodeId();
    {
      auto transaction = Transaction(document);
      auto shape = Shape();
      shape.m_geometry = LineShape(Point(0, 0), Point(50, 0));
      shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
      line = transaction.append(NodeId(), make_header("Line"), shape);
      transaction.commit();
    }
    REQUIRE(get_world_bounds(document, line) == Rect(0, 0, 50, 0));
    REQUIRE(get_world_bounds(document, line, BoundsType::VISUAL) ==
      Rect(-1, -1, 52, 2));
  }

  TEST_CASE("text_measurer") {
    auto document = Document();
    auto scene = build_scene(document);
    auto measurer = [] (const Text&) {
      return Rect(0, 0, 10, 10);
    };
    REQUIRE(get_local_bounds(document, scene.m_text, BoundsType::GEOMETRY,
      measurer) == Rect(0, 0, 10, 10));
  }

  TEST_CASE("text_estimate") {
    auto text = make_text(u"Hello\nHi");
    REQUIRE(estimate_text_bounds(text) == Rect(0, 0, 100, 40));
    text.m_frame = AutoHeightFrame(80);
    require_near(estimate_text_bounds(text), Rect(0, 0, 80, 2 * 1.2 * 14));
    text.m_frame = PositionedFrame();
    require_near(estimate_text_bounds(text),
      Rect(0, -0.8 * 14, 5 * 0.5 * 14, 2 * 1.2 * 14));
    text.m_paragraph_style.m_alignment = TextAlignment::RIGHT;
    require_near(estimate_text_bounds(text),
      Rect(-5 * 0.5 * 14, -0.8 * 14, 5 * 0.5 * 14, 2 * 1.2 * 14));
  }

  TEST_CASE("rect_mapping") {
    REQUIRE(make_rect_mapping(Rect(0, 0, 10, 10), Rect(5, 5, 20, 40)) ==
      Matrix(2, 0, 0, 4, 5, 5));
    REQUIRE(make_rect_mapping(Rect(10, 0, 0, 10), Rect(20, 0, 0, 20)) ==
      Matrix(1, 0, 0, 2, 10, 0));
  }
}

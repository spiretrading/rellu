#include <doctest/doctest.h>
#include "Rellu/Commands/CornerRadiusCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_polygon(Document& document, const NodeId& parent) {
    auto transaction = Transaction(document);
    auto polygon = Shape();
    polygon.m_geometry = PolygonShape(3, Size(10, 10));
    auto id = transaction.append(parent, make_header("Polygon 1"), polygon);
    transaction.commit();
    document.clear_history();
    return id;
  }

  const CornerRadii& get_radii(const Document& document, const NodeId& id) {
    return std::get<RectangleShape>(
      document.get(id).get_shape()->m_geometry).m_radii;
  }

  double get_polygon_radius(const Document& document, const NodeId& id) {
    return std::get<PolygonShape>(
      document.get(id).get_shape()->m_geometry).m_corner_radius;
  }
}

TEST_SUITE("CornerRadiusCommands") {
  TEST_CASE("uniform_radius") {
    auto document = Document();
    auto scene = build_scene(document);
    auto polygon = add_polygon(document, scene.m_artboard);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetCornerRadiusCommand>(
      std::vector{scene.m_rectangle, polygon, scene.m_text}, 5));
    REQUIRE(history.get_undo_label() == "Change Corner Radius");
    REQUIRE(get_radii(document, scene.m_rectangle) == CornerRadii(5, 5, 5, 5));
    REQUIRE(get_polygon_radius(document, polygon) == 5);
    REQUIRE(history.execute(std::make_unique<SetCornerRadiusCommand>(
      std::vector{scene.m_rectangle}, -3)));
    REQUIRE(get_radii(document, scene.m_rectangle) == CornerRadii(0, 0, 0, 0));
    REQUIRE(!history.execute(std::make_unique<SetCornerRadiusCommand>(
      std::vector{scene.m_text}, 4)));
  }

  TEST_CASE("corner_radii") {
    auto document = Document();
    auto scene = build_scene(document);
    auto polygon = add_polygon(document, scene.m_artboard);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetCornerRadiusCommand>(
      std::vector{scene.m_rectangle, polygon}, CornerRadii(1, 2, 3, 4)));
    REQUIRE(get_radii(document, scene.m_rectangle) == CornerRadii(1, 2, 3, 4));
    REQUIRE(get_polygon_radius(document, polygon) == 0);
    require_undoable(history, std::make_unique<SetCornerRadiusCommand>(
      std::vector{scene.m_rectangle}, Corner::BOTTOM_RIGHT, 9));
    REQUIRE(get_radii(document, scene.m_rectangle) == CornerRadii(1, 2, 9, 4));
  }

  TEST_CASE("radius_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto polygon = add_polygon(document, scene.m_artboard);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle, polygon};
    REQUIRE(history.merge(std::make_unique<SetCornerRadiusCommand>(nodes, 1)));
    REQUIRE(history.merge(std::make_unique<SetCornerRadiusCommand>(nodes, 12)));
    REQUIRE(history.merge(std::make_unique<SetCornerRadiusCommand>(
      nodes, Corner::TOP_LEFT, 3)));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(get_radii(document, scene.m_rectangle) ==
      CornerRadii(3, 12, 12, 12));
    REQUIRE(get_polygon_radius(document, polygon) == 12);
    REQUIRE(history.merge(std::make_unique<SetCornerRadiusCommand>(
      std::vector{scene.m_rectangle}, 2)));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(get_radii(document, scene.m_rectangle) == CornerRadii(0, 0, 0, 0));
    REQUIRE(get_polygon_radius(document, polygon) == 0);
  }
}

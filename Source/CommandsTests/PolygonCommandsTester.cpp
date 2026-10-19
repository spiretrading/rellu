#include <doctest/doctest.h>
#include "Rellu/Commands/PolygonCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("PolygonCommands") {
  TEST_CASE("corner_count") {
    auto document = Document();
    auto scene = build_scene(document);
    auto polygon = NodeId();
    {
      auto transaction = Transaction(document);
      auto shape = Shape();
      auto geometry = PolygonShape();
      geometry.m_corner_count = 6;
      geometry.m_size = Size(40, 40);
      geometry.m_vertices = {Point(0, 0), Point(40, 0), Point(40, 40)};
      shape.m_geometry = geometry;
      polygon = transaction.append(
        scene.m_artboard, make_header("Polygon"), shape);
      transaction.commit();
    }
    auto history = History(document);
    require_undoable(history, std::make_unique<SetCornerCountCommand>(
      std::vector{polygon, scene.m_rectangle}, 8));
    REQUIRE(history.get_undo_label() == "Change Corner Count");
    auto& edited = std::get<PolygonShape>(
      document.get(polygon).get_shape()->m_geometry);
    REQUIRE(edited.m_corner_count == 8);
    REQUIRE(edited.m_vertices.empty());
    REQUIRE(history.merge(std::make_unique<SetCornerCountCommand>(
      std::vector{polygon, scene.m_rectangle}, 1)));
    REQUIRE(std::get<PolygonShape>(document.get(polygon).get_shape()->
      m_geometry).m_corner_count == SetCornerCountCommand::MINIMUM_CORNERS);
  }
}

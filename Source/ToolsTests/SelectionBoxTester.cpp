#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Tools/SelectionBox.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("SelectionBox") {
  TEST_CASE("handles") {
    auto box = SelectionBox(Rect(0, 0, 100, 50), make_translation(10, 20));
    REQUIRE(box.get_point(Handle::TOP_LEFT) == Point(0, 0));
    REQUIRE(box.get_point(Handle::RIGHT) == Point(100, 25));
    REQUIRE(box.get_point(Handle::BOTTOM) == Point(50, 50));
    REQUIRE(box.get_corners()[2] == Point(110, 70));
    REQUIRE(box.get_center() == Point(60, 45));
    REQUIRE(is_corner(Handle::BOTTOM_LEFT));
    REQUIRE(!is_corner(Handle::LEFT));
    REQUIRE(get_opposite(Handle::TOP_LEFT) == Handle::BOTTOM_RIGHT);
    REQUIRE(get_opposite(Handle::LEFT) == Handle::RIGHT);
  }

  TEST_CASE("selection_box") {
    auto document = Document();
    auto scene = build_scene(document);
    auto single = *get_selection_box(document, std::vector{scene.m_group});
    require_near(single.m_transform, get_world_transform(document,
      scene.m_group));
    REQUIRE(single.m_rect == *get_local_bounds(document, scene.m_group));
    auto multiple = *get_selection_box(
      document, std::vector{scene.m_rectangle, scene.m_loose});
    REQUIRE(multiple.m_transform == Matrix());
    REQUIRE(multiple.m_rect == Rect(-50, -50, 200, 130));
    REQUIRE(*get_selection_box(document, std::vector{scene.m_rectangle,
      scene.m_artboard}) == *get_selection_box(
        document, std::vector{scene.m_artboard}));
    REQUIRE(!get_selection_box(document, std::vector<NodeId>()));
  }

  TEST_CASE("resize") {
    auto rect = Rect(0, 0, 100, 50);
    REQUIRE(resize(rect, Handle::BOTTOM_RIGHT, Point(150, 80), false, false,
      1) == Rect(0, 0, 150, 80));
    REQUIRE(resize(rect, Handle::TOP_LEFT, Point(-10, 10), false, false, 1) ==
      Rect(-10, 10, 110, 40));
    REQUIRE(resize(rect, Handle::RIGHT, Point(120, 500), false, false, 1) ==
      Rect(0, 0, 120, 50));
    REQUIRE(resize(rect, Handle::BOTTOM_RIGHT, Point(200, 60), true, false,
      1) == Rect(0, 0, 200, 100));
    REQUIRE(resize(rect, Handle::RIGHT, Point(200, 0), true, false, 1) ==
      Rect(0, -25, 200, 100));
    REQUIRE(resize(rect, Handle::RIGHT, Point(120, 0), false, true, 1) ==
      Rect(-20, 0, 140, 50));
    REQUIRE(resize(rect, Handle::BOTTOM_RIGHT, Point(150, 75), true, true,
      1) == Rect(-50, -25, 200, 100));
    REQUIRE(resize(rect, Handle::LEFT, Point(300, 0), false, false, 1) ==
      Rect(99, 0, 1, 50));
    REQUIRE(resize(Rect(0, 0, 100, 0), Handle::BOTTOM, Point(50, 30), false,
      false, 1) == Rect(0, 0, 100, 0));
  }
}

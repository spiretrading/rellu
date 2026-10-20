#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Tools/SelectionBox.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto EVERY = std::vector<Handle>(HANDLES.begin(), HANDLES.end());
  const auto CORNERS = std::vector<Handle>({Handle::TOP_LEFT,
    Handle::TOP_RIGHT, Handle::BOTTOM_RIGHT, Handle::BOTTOM_LEFT});

  HandleLayout make_layout(const std::vector<Handle>& resizers,
      const std::vector<Handle>& rotators) {
    auto layout = HandleLayout();
    for(auto handle : resizers) {
      layout.m_resizers[static_cast<std::size_t>(handle)] = true;
    }
    for(auto handle : rotators) {
      layout.m_rotators[static_cast<std::size_t>(handle)] = true;
    }
    return layout;
  }

  Group make_stack(Orientation orientation) {
    auto group = Group();
    group.m_stack = Stack(orientation);
    return group;
  }

  Group make_scroll_group(ScrollDirection direction) {
    auto group = Group();
    group.m_scroll_group = ScrollGroup(direction, Size(100, 100));
    return group;
  }

  Text make_text(const TextFrame& frame) {
    auto text = Tests::make_text(u"Label");
    text.m_frame = frame;
    return text;
  }
}

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
      true, 1) == Rect(0, 0, 150, 80));
    REQUIRE(resize(rect, Handle::TOP_LEFT, Point(-10, 10), false, false, true,
      1) == Rect(-10, 10, 110, 40));
    REQUIRE(resize(rect, Handle::RIGHT, Point(120, 500), false, false, true,
      1) == Rect(0, 0, 120, 50));
    REQUIRE(resize(rect, Handle::BOTTOM_RIGHT, Point(200, 60), true, false,
      true, 1) == Rect(0, 0, 200, 100));
    REQUIRE(resize(rect, Handle::RIGHT, Point(200, 0), true, false, true, 1) ==
      Rect(0, -25, 200, 100));
    REQUIRE(resize(rect, Handle::RIGHT, Point(120, 0), false, true, true,
      1) == Rect(-20, 0, 140, 50));
    REQUIRE(resize(rect, Handle::BOTTOM_RIGHT, Point(150, 75), true, true,
      true, 1) == Rect(-50, -25, 200, 100));
    REQUIRE(resize(rect, Handle::LEFT, Point(300, 0), false, false, false,
      1) == Rect(99, 0, 1, 50));
    REQUIRE(resize(Rect(0, 0, 100, 0), Handle::BOTTOM, Point(50, 30), false,
      false, true, 1) == Rect(0, 0, 100, 0));
  }

  TEST_CASE("flip") {
    auto rect = Rect(0, 0, 100, 50);
    REQUIRE(resize(rect, Handle::LEFT, Point(300, 0), false, false, true,
      1) == Rect(300, 0, -200, 50));
    REQUIRE(resize(rect, Handle::RIGHT, Point(-40, 0), false, false, true,
      1) == Rect(0, 0, -40, 50));
    REQUIRE(resize(rect, Handle::RIGHT, Point(0, 0), false, false, true, 1) ==
      Rect(0, 0, 1, 50));
    REQUIRE(resize(rect, Handle::RIGHT, Point(-0.5, 0), false, false, true,
      1) == Rect(0, 0, -1, 50));
    REQUIRE(resize(rect, Handle::BOTTOM_RIGHT, Point(-20, 60), false, false,
      true, 1) == Rect(0, 0, -20, 60));
    REQUIRE(resize(rect, Handle::TOP, Point(0, 80), false, false, true, 1) ==
      Rect(0, 80, 100, -30));
    REQUIRE(resize(rect, Handle::RIGHT, Point(20, 0), false, true, true, 1) ==
      Rect(80, 0, -60, 50));
    REQUIRE(resize(rect, Handle::BOTTOM_RIGHT, Point(-200, 25), true, false,
      true, 1) == Rect(0, 0, -200, 100));
    REQUIRE(resize(rect, Handle::BOTTOM, Point(0, -50), true, false, true,
      1) == Rect(0, 0, 100, -50));
  }

  TEST_CASE("stack_handles") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto row = transaction.append(
      NodeId(), make_header("Row"), make_stack(Orientation::HORIZONTAL));
    auto first = transaction.append(
      row, make_header("First"), make_rectangle_shape(40, 40));
    auto column = transaction.append(row, make_header("Column",
      make_translation(50, 0)), make_stack(Orientation::VERTICAL));
    auto cell = transaction.append(
      column, make_header("Cell"), make_rectangle_shape(40, 20));
    auto group = Group();
    group.m_padding = Padding();
    auto padding = transaction.append(NodeId(), make_header("Padding"), group);
    auto loose = transaction.append(
      NodeId(), make_header("Loose"), make_rectangle_shape(10, 10));
    transaction.commit();
    auto size = Size(100, 100);
    REQUIRE(get_handle_layout(document, std::vector{row}, size, 1) ==
      make_layout({Handle::TOP, Handle::BOTTOM}, {Handle::TOP_LEFT,
        Handle::TOP_RIGHT, Handle::RIGHT, Handle::BOTTOM_RIGHT,
        Handle::BOTTOM_LEFT, Handle::LEFT}));
    REQUIRE(get_handle_layout(document, std::vector{column}, size, 1) ==
      make_layout({Handle::RIGHT, Handle::LEFT}, {Handle::TOP_LEFT,
        Handle::TOP, Handle::TOP_RIGHT, Handle::BOTTOM_RIGHT, Handle::BOTTOM,
        Handle::BOTTOM_LEFT}));
    REQUIRE(get_handle_layout(document, std::vector{first}, size, 1) ==
      make_layout(EVERY, CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{cell}, size, 1) ==
      make_layout(EVERY, CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{first, column}, size,
      1) == make_layout({Handle::TOP, Handle::BOTTOM}, {}));
    REQUIRE(get_handle_layout(document, std::vector{cell, loose}, size, 1) ==
      make_layout({Handle::RIGHT, Handle::LEFT}, {}));
    REQUIRE(get_handle_layout(document, std::vector{loose, first}, size,
      1) == make_layout(EVERY, CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{row, loose}, size, 1) ==
      make_layout(EVERY, CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{padding}, size, 1) ==
      make_layout(EVERY, CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{row}, Size(100, 5), 1) ==
      make_layout({Handle::TOP, Handle::RIGHT, Handle::LEFT}, {}));
  }

  TEST_CASE("text_handles") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto point = transaction.append(
      NodeId(), make_header("Point"), make_text(PositionedFrame()));
    auto automatic = transaction.append(
      NodeId(), make_header("Auto"), make_text(AutoHeightFrame(100)));
    auto area = transaction.append(
      NodeId(), make_header("Area"), make_text(AreaFrame(Size(100, 40))));
    transaction.commit();
    auto size = Size(100, 100);
    auto bottom = make_layout({Handle::BOTTOM}, {Handle::BOTTOM});
    REQUIRE(get_handle_layout(document, std::vector{point}, size, 1) ==
      bottom);
    REQUIRE(get_handle_layout(document, std::vector{point}, Size(4, 4), 1) ==
      bottom);
    REQUIRE(get_handle_layout(document, std::vector{automatic}, size, 1) ==
      make_layout({Handle::TOP, Handle::RIGHT, Handle::BOTTOM, Handle::LEFT},
        CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{area}, size, 1) ==
      make_layout(EVERY, CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{point, automatic}, size,
      1) == make_layout(EVERY, CORNERS));
  }

  TEST_CASE("scroll_group_handles") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto vertical = transaction.append(NodeId(), make_header("Vertical"),
      make_scroll_group(ScrollDirection::VERTICAL));
    auto horizontal = transaction.append(NodeId(), make_header("Horizontal"),
      make_scroll_group(ScrollDirection::HORIZONTAL));
    auto panning = transaction.append(NodeId(), make_header("Panning"),
      make_scroll_group(ScrollDirection::PANNING));
    transaction.commit();
    auto size = Size(100, 100);
    REQUIRE(get_handle_layout(document, std::vector{vertical}, size, 1) ==
      make_layout({Handle::TOP, Handle::BOTTOM}, {}));
    REQUIRE(get_handle_layout(document, std::vector{horizontal}, size, 1) ==
      make_layout({Handle::RIGHT, Handle::LEFT}, {}));
    REQUIRE(get_handle_layout(document, std::vector{panning}, size, 1) ==
      make_layout({Handle::TOP, Handle::RIGHT, Handle::BOTTOM, Handle::LEFT},
        {}));
    REQUIRE(get_handle_layout(document, std::vector{panning}, Size(20, 20),
      1) == make_layout({Handle::RIGHT, Handle::LEFT}, {}));
    REQUIRE(get_handle_layout(document, std::vector{vertical}, Size(100, 9),
      1) == make_layout({Handle::BOTTOM}, {}));
    REQUIRE(get_handle_layout(document, std::vector{vertical}, Size(100, 9),
      0.25) == make_layout({Handle::TOP, Handle::BOTTOM}, {}));
  }

  TEST_CASE("crowded_handles") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto rectangle = transaction.append(
      NodeId(), make_header("Rectangle"), make_rectangle_shape(10, 10));
    auto board = Artboard();
    board.m_frame = Rect(0, 0, 100, 100);
    auto artboard =
      transaction.append(NodeId(), make_header("Artboard"), board);
    transaction.commit();
    auto nodes = std::vector{rectangle};
    REQUIRE(get_handle_layout(document, nodes, Size(10, 5), 1) ==
      make_layout({Handle::TOP, Handle::RIGHT, Handle::LEFT}, {}));
    REQUIRE(get_handle_layout(document, nodes, Size(5, 10), 1) ==
      make_layout({Handle::TOP, Handle::BOTTOM, Handle::LEFT}, {}));
    REQUIRE(get_handle_layout(document, nodes, Size(5, 5), 1) ==
      make_layout({Handle::BOTTOM_RIGHT}, {Handle::BOTTOM_RIGHT}));
    REQUIRE(get_handle_layout(document, nodes, Size(100, 12), 1) ==
      make_layout({Handle::TOP_LEFT, Handle::TOP, Handle::TOP_RIGHT,
        Handle::BOTTOM_RIGHT, Handle::BOTTOM, Handle::BOTTOM_LEFT}, CORNERS));
    REQUIRE(get_handle_layout(document, nodes, Size(12, 100), 1) ==
      make_layout({Handle::TOP_LEFT, Handle::TOP_RIGHT, Handle::RIGHT,
        Handle::BOTTOM_RIGHT, Handle::BOTTOM_LEFT, Handle::LEFT}, CORNERS));
    REQUIRE(get_handle_layout(document, std::vector{artboard},
      Size(100, 100), 1) == make_layout(EVERY, {}));
    REQUIRE(get_handle_layout(document, std::vector{artboard}, Size(5, 5),
      1) == make_layout({Handle::BOTTOM_RIGHT}, {}));
    REQUIRE(get_handle_layout(document, std::vector<NodeId>(),
      Size(100, 100), 1) == HandleLayout());
    REQUIRE(get_locked_handle_layout(Size(100, 100)) ==
      make_layout(EVERY, {}));
    REQUIRE(get_locked_handle_layout(Size(5, 5)) ==
      make_layout({Handle::BOTTOM_RIGHT}, {}));
  }
}

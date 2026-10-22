#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Tools/ArtboardOutlines.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("ArtboardOutlines") {
  TEST_CASE("draw_artboard_outlines") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 100, 50);
    auto board = transaction.append(NodeId(), make_header("A"), artboard);
    artboard.m_frame = Rect(200, 0, 100, 50);
    auto header = make_header("Hidden");
    header.m_is_visible = false;
    transaction.append(NodeId(), header, artboard);
    auto shape = transaction.append(board,
      make_header("Shape", make_translation(10, 10)),
      make_rectangle_shape(20, 20));
    transaction.commit();
    auto overlay = CanvasOverlay();
    auto view = make_translation(10, 20) * make_scale(2, 2);
    draw_artboard_outlines(document, view, {}, overlay);
    REQUIRE(overlay.m_shapes.size() == 4);
    auto& top = overlay.m_shapes[0];
    REQUIRE(top.m_center == Point(110, 19.5));
    REQUIRE(top.m_size == Size(202, 1));
    REQUIRE(!top.m_is_snapped);
    REQUIRE(top.m_fill == ARTBOARD_OUTLINE_COLOR);
    auto& bottom = overlay.m_shapes[1];
    REQUIRE(bottom.m_center == Point(110, 120.5));
    auto& left = overlay.m_shapes[2];
    REQUIRE(left.m_center == Point(9.5, 70));
    REQUIRE(left.m_size == Size(1, 100));
    auto& right = overlay.m_shapes[3];
    REQUIRE(right.m_center == Point(210.5, 70));
    auto focused = CanvasOverlay();
    draw_artboard_outlines(document, view, {shape}, focused);
    REQUIRE(focused.m_shapes.size() == 4);
    REQUIRE(focused.m_shapes[0].m_fill == FOCUSED_ARTBOARD_OUTLINE_COLOR);
  }
}

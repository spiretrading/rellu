#include <optional>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Tools/ArtboardGrids.hpp"

using namespace Rellu;

namespace {
  NodeId add_artboard(Document& document, std::optional<LayoutGrid> grid) {
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 0, 480, 360);
    artboard.m_layout_grid = std::move(grid);
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    auto transaction = Transaction(document);
    auto id = transaction.append(NodeId(), header, artboard);
    transaction.commit();
    return id;
  }
}

TEST_SUITE("ArtboardGrids") {
  TEST_CASE("layout") {
    auto document = Document();
    auto grid = LayoutGrid();
    grid.m_type = GridType::LAYOUT;
    grid.m_column_count = 4;
    grid.m_gutter = 10;
    grid.m_margins.m_left = 37;
    grid.m_margins.m_right = 37;
    grid.m_column_color = Color(0, 1, 1, 0.25);
    add_artboard(document, grid);
    auto overlay = CanvasOverlay();
    draw_artboard_grids(document, Matrix(), Size(1000, 1000), overlay);
    REQUIRE(overlay.m_shapes.empty());
    grid.m_is_visible = true;
    add_artboard(document, grid);
    draw_artboard_grids(document, Matrix(), Size(1000, 1000), overlay);
    REQUIRE(overlay.m_shapes.size() == 4);
    REQUIRE(overlay.m_shapes.front().m_center == Point(184, 180));
    REQUIRE(overlay.m_shapes.front().m_size == Size(94, 360));
    REQUIRE(overlay.m_shapes.front().m_fill == Color(0, 1, 1, 0.25));
  }

  TEST_CASE("square") {
    auto document = Document();
    auto grid = LayoutGrid();
    grid.m_is_visible = true;
    grid.m_square_size = Size(40, 40);
    add_artboard(document, grid);
    auto overlay = CanvasOverlay();
    draw_artboard_grids(document, Matrix(), Size(1000, 1000), overlay);
    REQUIRE(overlay.m_shapes.size() == 11 + 8);
    auto small = CanvasOverlay();
    draw_artboard_grids(document, make_scale(0.05, 0.05), Size(1000, 1000),
      small);
    REQUIRE(small.m_shapes.empty());
  }
}

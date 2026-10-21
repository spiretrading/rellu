#include <algorithm>
#include <cstdlib>
#include <optional>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Tools/ArtboardGrids.hpp"

using namespace Rellu;

namespace {
  Rect get_bounds(const OverlayShape& shape) {
    return Rect(shape.m_center.m_x - shape.m_size.m_width / 2,
      shape.m_center.m_y - shape.m_size.m_height / 2, shape.m_size.m_width,
      shape.m_size.m_height);
  }

  void require_bounds(const OverlayShape& shape, const Rect& bounds) {
    auto actual = get_bounds(shape);
    REQUIRE(actual.m_x == doctest::Approx(bounds.m_x));
    REQUIRE(actual.m_y == doctest::Approx(bounds.m_y));
    REQUIRE(actual.m_width == doctest::Approx(bounds.m_width));
    REQUIRE(actual.m_height == doctest::Approx(bounds.m_height));
  }

  bool is_near(const QColor& color, const QColor& expected) {
    return std::max({std::abs(color.red() - expected.red()),
      std::abs(color.green() - expected.green()),
      std::abs(color.blue() - expected.blue())}) <= 3;
  }

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
    grid.m_column_count = 4;
    grid.m_gutter = 10;
    grid.m_margins.m_left = 37;
    grid.m_margins.m_right = 37;
    grid.m_column_color = Color(0, 1, 1, 0.25);
    grid.m_square_size = Size(40, 40);
    add_artboard(document, grid);
    auto settings = GridSettings();
    auto overlay = CanvasOverlay();
    draw_artboard_grids(
      document, settings, Matrix(), Size(1000, 1000), overlay);
    REQUIRE(overlay.m_shapes.empty());
    settings.m_is_layout_visible = true;
    draw_artboard_grids(
      document, settings, Matrix(), Size(1000, 1000), overlay);
    REQUIRE(overlay.m_shapes.size() == 4 + 4 * 4 + 3 * 2);
    REQUIRE(overlay.m_shapes.front().m_center == Point(184, 180));
    REQUIRE(overlay.m_shapes.front().m_size == Size(94, 360));
    REQUIRE(overlay.m_shapes.front().m_fill == Color(0, 1, 1, 0.25));
    for(auto& shape : overlay.m_shapes) {
      REQUIRE(!shape.m_is_snapped);
      REQUIRE(shape.m_fill == Color(0, 1, 1, 0.25));
      REQUIRE(shape.m_stroke_width == 0);
    }
    auto& shapes = overlay.m_shapes;
    require_bounds(shapes[4], Rect(137 + 5.0 / 9, 5.0 / 9, 4.0 / 9,
      360 - 10.0 / 9));
    require_bounds(shapes[5], Rect(230, 5.0 / 9, 4.0 / 9, 360 - 10.0 / 9));
    require_bounds(shapes[6], Rect(138, 5.0 / 9, 92, 4.0 / 9));
    require_bounds(shapes[7], Rect(138, 359, 92, 4.0 / 9));
    require_bounds(shapes[8], Rect(241 + 5.0 / 9, 5.0 / 9, 4.0 / 9,
      360 - 10.0 / 9));
    require_bounds(shapes[20], Rect(231, 0, 10, 1));
    require_bounds(shapes[21], Rect(231, 359, 10, 1));
    require_bounds(shapes[24], Rect(439, 0, 10, 1));
    require_bounds(shapes[25], Rect(439, 359, 10, 1));
  }

  TEST_CASE("band_pixels") {
    auto document = Document();
    auto grid = LayoutGrid();
    grid.m_column_count = 2;
    grid.m_gutter = 9;
    grid.m_margins.m_left = 10;
    grid.m_margins.m_right = 79;
    grid.m_column_color = Color(0, 1, 1, 64.0 / 255);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 200, 100);
    artboard.m_layout_grid = grid;
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), header, artboard);
    transaction.commit();
    auto settings = GridSettings();
    settings.m_is_layout_visible = true;
    auto overlay = CanvasOverlay();
    draw_artboard_grids(
      document, settings, Matrix(), Size(200, 100), overlay);
    auto renderer = CanvasRenderer();
    renderer.set_background(QColor(255, 255, 255));
    renderer.set_overlay(overlay);
    auto offscreen = OffscreenRenderer(renderer);
    auto image = offscreen.render(QSize(300, 150), 1.5);
    REQUIRE(image.pixelColor(14, 75) == QColor(255, 255, 255));
    REQUIRE(is_near(image.pixelColor(15, 75), QColor(0xB8, 0xFC, 0xFF)));
    REQUIRE(is_near(image.pixelColor(16, 75), QColor(0xA9, 0xFD, 0xFF)));
    REQUIRE(image.pixelColor(17, 75) == QColor(0xBF, 0xFF, 0xFF));
    REQUIRE(image.pixelColor(89, 75) == QColor(0xBF, 0xFF, 0xFF));
    REQUIRE(is_near(image.pixelColor(90, 75), QColor(0x9E, 0xFE, 0xFE)));
    REQUIRE(image.pixelColor(91, 75) == QColor(0xDF, 0xFF, 0xFF));
    REQUIRE(image.pixelColor(92, 75) == QColor(255, 255, 255));
    REQUIRE(is_near(image.pixelColor(50, 0), QColor(0xB8, 0xFC, 0xFF)));
    REQUIRE(is_near(image.pixelColor(50, 1), QColor(0xA9, 0xFD, 0xFF)));
    REQUIRE(image.pixelColor(50, 2) == QColor(0xBF, 0xFF, 0xFF));
    REQUIRE(image.pixelColor(98, 0) == QColor(0xBF, 0xFF, 0xFF));
    REQUIRE(image.pixelColor(98, 1) == QColor(0xDF, 0xFF, 0xFF));
    REQUIRE(image.pixelColor(98, 2) == QColor(255, 255, 255));
    REQUIRE(image.pixelColor(98, 148) == QColor(0xDF, 0xFF, 0xFF));
    REQUIRE(image.pixelColor(98, 149) == QColor(0xBF, 0xFF, 0xFF));
  }

  TEST_CASE("column_edges") {
    auto document = Document();
    auto grid = LayoutGrid();
    grid.m_column_count = 12;
    grid.m_gutter = 16;
    grid.m_margins.m_left = 140;
    grid.m_margins.m_right = 140;
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 1920, 1080);
    artboard.m_layout_grid = grid;
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), header, artboard);
    transaction.commit();
    auto settings = GridSettings();
    settings.m_is_layout_visible = true;
    auto overlay = CanvasOverlay();
    draw_artboard_grids(document, settings, make_scale(1.5, 1.5),
      Size(3000, 2000), overlay);
    REQUIRE(overlay.m_shapes.size() == 12 + 12 * 4 + 11 * 2);
    for(auto i = 0; i != 12; ++i) {
      auto& shape = overlay.m_shapes[i];
      REQUIRE(!shape.m_is_snapped);
      REQUIRE(shape.m_center.m_x - shape.m_size.m_width / 2 ==
        doctest::Approx(210 + 207 * i));
      REQUIRE(shape.m_center.m_x + shape.m_size.m_width / 2 ==
        doctest::Approx(393 + 207 * i));
    }
  }

  TEST_CASE("square") {
    auto document = Document();
    auto grid = LayoutGrid();
    grid.m_type = GridType::LAYOUT;
    grid.m_square_size = Size(40, 40);
    add_artboard(document, grid);
    auto settings = GridSettings();
    settings.m_is_square_visible = true;
    auto overlay = CanvasOverlay();
    draw_artboard_grids(
      document, settings, Matrix(), Size(1000, 1000), overlay);
    REQUIRE(overlay.m_shapes.size() == 11 + 8);
    auto small = CanvasOverlay();
    draw_artboard_grids(document, settings, make_scale(0.05, 0.05),
      Size(1000, 1000), small);
    REQUIRE(small.m_shapes.empty());
  }

  TEST_CASE("initial_grid") {
    auto document = Document();
    add_artboard(document, std::nullopt);
    auto settings = GridSettings();
    settings.m_is_layout_visible = true;
    auto overlay = CanvasOverlay();
    draw_artboard_grids(
      document, settings, Matrix(), Size(1000, 1000), overlay);
    REQUIRE(overlay.m_shapes.size() == 4 + 4 * 4 + 3 * 2);
  }
}

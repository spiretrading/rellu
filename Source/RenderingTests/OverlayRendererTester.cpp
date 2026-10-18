#include <memory>
#include <doctest/doctest.h>
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"

using namespace Rellu;

namespace {
  const auto WHITE = QColor(255, 255, 255);
  const auto BLACK = QColor(0, 0, 0);
  const auto RED = QColor(255, 0, 0);
  const auto BLUE = QColor(0, 0, 255);

  CanvasOverlay make_overlay() {
    auto overlay = CanvasOverlay();
    overlay.m_shapes.push_back(make_overlay_line(
      Point(2, 5), Point(12, 5), 1, make_color(0, 0, 0)));
    overlay.m_shapes.push_back(make_overlay_line(
      Point(5, 10), Point(5, 14), 1, make_color(0, 0, 0)));
    auto box = OverlayShape();
    box.m_center = Point(20, 20);
    box.m_size = Size(10, 10);
    box.m_fill = make_color(255, 0, 0);
    box.m_stroke = make_color(0, 0, 255);
    box.m_stroke_width = 1;
    overlay.m_shapes.push_back(box);
    auto image = std::make_shared<QImage>(2, 2, QImage::Format_RGBA8888);
    image->fill(QColor(0, 255, 0));
    overlay.m_images.push_back(OverlayImage(image, Point(30.4, 2)));
    return overlay;
  }
}

TEST_SUITE("OverlayRenderer") {
  TEST_CASE("render") {
    auto renderer = CanvasRenderer();
    renderer.set_background(WHITE);
    renderer.set_overlay(make_overlay());
    REQUIRE(renderer.get_overlay().m_shapes.size() == 3);
    auto offscreen = OffscreenRenderer(renderer);
    auto image = offscreen.render(QSize(40, 40), 1);
    for(auto x = 2; x != 12; ++x) {
      REQUIRE(image.pixelColor(x, 5) == BLACK);
    }
    REQUIRE(image.pixelColor(1, 5) == WHITE);
    REQUIRE(image.pixelColor(12, 5) == WHITE);
    REQUIRE(image.pixelColor(6, 4) == WHITE);
    REQUIRE(image.pixelColor(6, 6) == WHITE);
    REQUIRE(image.pixelColor(5, 10) == BLACK);
    REQUIRE(image.pixelColor(5, 13) == BLACK);
    REQUIRE(image.pixelColor(4, 12) == WHITE);
    REQUIRE(image.pixelColor(6, 12) == WHITE);
    REQUIRE(image.pixelColor(15, 20) == BLUE);
    REQUIRE(image.pixelColor(16, 20) == RED);
    REQUIRE(image.pixelColor(23, 23) == RED);
    REQUIRE(image.pixelColor(24, 20) == BLUE);
    REQUIRE(image.pixelColor(25, 20) == WHITE);
    REQUIRE(image.pixelColor(30, 2) == QColor(0, 255, 0));
    REQUIRE(image.pixelColor(31, 3) == QColor(0, 255, 0));
    REQUIRE(image.pixelColor(32, 2) == WHITE);
    auto scaled = offscreen.render(QSize(80, 80), 2);
    REQUIRE(scaled.pixelColor(10, 9) == BLACK);
    REQUIRE(scaled.pixelColor(10, 10) == BLACK);
    REQUIRE(scaled.pixelColor(10, 8) == WHITE);
    REQUIRE(scaled.pixelColor(10, 11) == WHITE);
    REQUIRE(scaled.pixelColor(30, 40) == BLUE);
    REQUIRE(scaled.pixelColor(31, 40) == BLUE);
    REQUIRE(scaled.pixelColor(32, 40) == RED);
    REQUIRE(scaled.pixelColor(61, 4) == QColor(0, 255, 0));
    renderer.set_overlay(CanvasOverlay());
    auto cleared = offscreen.render(QSize(40, 40), 1);
    REQUIRE(cleared.pixelColor(5, 5) == WHITE);
    REQUIRE(cleared.pixelColor(20, 20) == WHITE);
  }
}

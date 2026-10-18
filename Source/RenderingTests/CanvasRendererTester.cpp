#include <memory>
#include <utility>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"

using namespace Rellu;

namespace {
  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  auto is_filled(const QImage& image, QColor color) {
    for(auto y = 0; y != image.height(); ++y) {
      for(auto x = 0; x != image.width(); ++x) {
        if(image.pixelColor(x, y) != color) {
          return false;
        }
      }
    }
    return true;
  }
}

TEST_SUITE("CanvasRenderer") {
  TEST_CASE("background") {
    auto renderer = CanvasRenderer();
    auto offscreen = OffscreenRenderer(renderer);
    auto image = offscreen.render(QSize(64, 48), 1);
    REQUIRE(image.size() == QSize(64, 48));
    REQUIRE(image.devicePixelRatio() == 1);
    REQUIRE(is_filled(image, CANVAS_BACKGROUND));
    auto scaled = offscreen.render(QSize(129, 97), 2);
    REQUIRE(scaled.size() == QSize(129, 97));
    REQUIRE(scaled.devicePixelRatio() == 2);
    REQUIRE(is_filled(scaled, CANVAS_BACKGROUND));
    REQUIRE(renderer.get_background() == CANVAS_BACKGROUND);
    renderer.set_background(QColor(0, 0, 0, 0));
    REQUIRE(renderer.get_background() == QColor(0, 0, 0, 0));
    REQUIRE(is_filled(offscreen.render(QSize(8, 8), 1), QColor(0, 0, 0, 0)));
  }

  TEST_CASE("draw_list") {
    auto renderer = CanvasRenderer();
    auto list = std::make_shared<DrawList>();
    list->push_back(FillCommand(
      share(make_rectangle(Rect(2, 3, 5, 4))), Matrix(),
      SolidPaint(make_color(255, 0, 0))));
    renderer.set_draw_list(list);
    renderer.set_view(make_translation(1, 2));
    REQUIRE(renderer.get_draw_list() == list);
    REQUIRE(renderer.get_view() == make_translation(1, 2));
    auto offscreen = OffscreenRenderer(renderer);
    auto red = QColor(255, 0, 0);
    auto image = offscreen.render(QSize(16, 16), 1);
    REQUIRE(image.pixelColor(3, 5) == red);
    REQUIRE(image.pixelColor(7, 8) == red);
    REQUIRE(image.pixelColor(2, 5) == CANVAS_BACKGROUND);
    REQUIRE(image.pixelColor(8, 8) == CANVAS_BACKGROUND);
    REQUIRE(image.pixelColor(7, 9) == CANVAS_BACKGROUND);
    auto scaled = offscreen.render(QSize(32, 32), 2);
    REQUIRE(scaled.pixelColor(6, 10) == red);
    REQUIRE(scaled.pixelColor(15, 17) == red);
    REQUIRE(scaled.pixelColor(5, 10) == CANVAS_BACKGROUND);
    REQUIRE(scaled.pixelColor(16, 17) == CANVAS_BACKGROUND);
    renderer.set_view(Matrix());
    auto moved = offscreen.render(QSize(16, 16), 1);
    REQUIRE(moved.pixelColor(2, 3) == red);
    REQUIRE(moved.pixelColor(7, 7) == CANVAS_BACKGROUND);
    renderer.set_draw_list(nullptr);
    REQUIRE(is_filled(offscreen.render(QSize(16, 16), 1), CANVAS_BACKGROUND));
  }
}

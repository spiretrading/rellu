#include <doctest/doctest.h>
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"

using namespace Rellu;

namespace {
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
  }
}

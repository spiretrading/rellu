#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Tools/CanvasBounds.hpp"

using namespace Rellu;

TEST_SUITE("CanvasBounds") {
  TEST_CASE("outside_regions") {
    REQUIRE(OUTSIDE_CANVAS_COLOR == make_color(160, 160, 160));
    auto size = Size(1980, 1320);
    REQUIRE(get_outside_regions(
      make_translation(990, 660) * make_scale(1, 1), size).empty());
    auto zoom = 0.027;
    auto regions = get_outside_regions(
      make_translation(990, 660) * make_scale(zoom, zoom), size);
    REQUIRE(regions.size() == 2);
    auto inside = 25000 * zoom;
    REQUIRE(regions[0] == Rect(0, 0, 990 - inside, 1320));
    REQUIRE(regions[1] == Rect(990 + inside, 0, 1980 - 990 - inside, 1320));
    regions = get_outside_regions(
      make_translation(25100, 25050) * make_scale(1, 1), size);
    REQUIRE(regions == std::vector{Rect(0, 0, 100, 1320),
      Rect(100, 0, 1880, 50)});
    regions = get_outside_regions(
      make_translation(-30000, 0) * make_scale(1, 1), size);
    REQUIRE(regions == std::vector{Rect(0, 0, 1980, 1320)});
  }
}

#include <cmath>
#include <doctest/doctest.h>
#include "Rellu/Tools/ViewFit.hpp"

using namespace Rellu;

namespace {
  const auto NARROW = Size(2725 / 1.5, 1980 / 1.5);
  const auto WIDE = Size(2970 / 1.5, 1980 / 1.5);

  double to_percent(double zoom) {
    return std::round(zoom * 1000) / 10;
  }

  double to_device(double logical) {
    return std::round(logical * 1.5 * 100) / 100;
  }

  struct Gaps {
    double m_left;
    double m_top;
    double m_right;
    double m_bottom;
  };

  Gaps get_margins(const ViewFit& fit, const Rect& region, Size size) {
    auto left = fit.m_offset.m_x + fit.m_zoom * region.m_x;
    auto top = fit.m_offset.m_y + fit.m_zoom * region.m_y;
    return Gaps(to_device(left), to_device(top),
      to_device(size.m_width - left - fit.m_zoom * region.m_width),
      to_device(size.m_height - top - fit.m_zoom * region.m_height));
  }
}

TEST_SUITE("ViewFit") {
  TEST_CASE("width_limited") {
    auto web = Rect(0, 0, 1920, 1080);
    auto fit = fit_view(web, NARROW);
    REQUIRE(to_percent(fit.m_zoom) == 88.2);
    auto margins = get_margins(fit, web, NARROW);
    REQUIRE(margins.m_left == 93);
    REQUIRE(margins.m_right == 93);
    REQUIRE(std::round(margins.m_top) == 276);
    REQUIRE(margins.m_top == doctest::Approx(margins.m_bottom));
    auto artboards = Rect(0, 0, 3910, 1773);
    fit = fit_view(artboards, NARROW);
    REQUIRE(to_percent(fit.m_zoom) == 43.3);
    margins = get_margins(fit, artboards, NARROW);
    REQUIRE(margins.m_left == 93);
    REQUIRE(std::round(margins.m_top) == 414);
    auto rectangle = Rect(10, 20, 100, 50);
    fit = fit_view(rectangle, WIDE);
    REQUIRE(to_percent(fit.m_zoom) == 1856);
    margins = get_margins(fit, rectangle, WIDE);
    REQUIRE(margins.m_left == 93);
    REQUIRE(margins.m_right == 93);
    REQUIRE(to_percent(fit_view(Rect(0, 0, 1724, 21), WIDE).m_zoom) ==
      107.7);
  }

  TEST_CASE("height_limited") {
    auto line = Rect(5, 5, 4, 21);
    auto fit = fit_view(line, WIDE);
    REQUIRE(to_percent(fit.m_zoom) == 5585.7);
    auto margins = get_margins(fit, line, WIDE);
    REQUIRE(margins.m_top == 110.25);
    REQUIRE(margins.m_bottom == 110.25);
    REQUIRE(margins.m_left == doctest::Approx(margins.m_right));
    auto ipad = Rect(2000, 0, 1024, 1366);
    fit = fit_view(ipad, WIDE);
    REQUIRE(to_percent(fit.m_zoom) == 85.9);
    margins = get_margins(fit, ipad, WIDE);
    REQUIRE(margins.m_top == 110.25);
    REQUIRE(margins.m_left == doctest::Approx(margins.m_right));
  }

  TEST_CASE("marquee") {
    auto zoom = 0.9667;
    auto marquee = Rect(300, 200, 1000 / 1.5 / zoom, 500 / 1.5 / zoom);
    auto fit = fit_view(marquee, WIDE);
    REQUIRE(to_percent(fit.m_zoom) == 269.1);
    auto center = marquee.get_center();
    REQUIRE(fit.m_offset.m_x + fit.m_zoom * center.m_x ==
      doctest::Approx(WIDE.m_width / 2));
    REQUIRE(fit.m_offset.m_y + fit.m_zoom * center.m_y ==
      doctest::Approx(WIDE.m_height / 2));
  }

  TEST_CASE("fitted_region") {
    auto region = get_fitted_region(Point(500, 300), 0.5, WIDE);
    REQUIRE(region.get_center() == Point(500, 300));
    auto fit = fit_view(region, WIDE);
    REQUIRE(fit.m_zoom == doctest::Approx(0.5));
    REQUIRE(fit.m_offset.m_x + 0.5 * 500 == doctest::Approx(WIDE.m_width / 2));
  }

  TEST_CASE("settled") {
    auto area = Size(2560, 1392);
    REQUIRE(!is_settled(true, false, Size(1600, 960), area));
    REQUIRE(!is_settled(true, true, Size(1600, 960), area));
    REQUIRE(!is_settled(true, false, Size(2560, 1392), area));
    REQUIRE(!is_settled(true, true, Size(2560, 960), area));
    REQUIRE(is_settled(true, true, Size(2560, 1392), area));
    REQUIRE(is_settled(true, true, Size(2559, 1391), area));
    REQUIRE(is_settled(true, true, Size(2576, 1408), area));
    REQUIRE(is_settled(false, false, Size(1600, 960), area));
    REQUIRE(is_settled(false, false, Size(800, 600), area));
  }

  TEST_CASE("center_window") {
    REQUIRE(center_window(Size(1600, 960), Rect(0, 0, 2560, 1392)) ==
      Rect(480, 216, 1600, 960));
    REQUIRE(center_window(Size(1600, 960), Rect(-1920, 40, 1920, 1000)) ==
      Rect(-1760, 60, 1600, 960));
    REQUIRE(center_window(Size(1600, 960), Rect(0, 0, 1280, 672)) ==
      Rect(0, 0, 1280, 672));
    REQUIRE(center_window(Size(1600, 960), Rect(0, 0, 1601, 961)) ==
      Rect(0, 0, 1600, 960));
  }

  TEST_CASE("zoom_limits") {
    REQUIRE(fit_view(Rect(0, 0, 0.001, 0.001), Size(1000, 800)).m_zoom ==
      MAXIMUM_ZOOM);
    REQUIRE(fit_view(Rect(0, 0, 1e9, 1e9), Size(1000, 800)).m_zoom ==
      MINIMUM_ZOOM);
  }
}

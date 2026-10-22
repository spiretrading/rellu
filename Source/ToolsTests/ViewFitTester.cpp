#include <cmath>
#include <tuple>
#include <doctest/doctest.h>
#include "Rellu/Tools/ViewFit.hpp"

using namespace Rellu;

namespace {
  const auto NARROW = Size(2719 / 1.5, 1977 / 1.5);
  const auto WIDE = Size(2964 / 1.5, 1977 / 1.5);

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
    REQUIRE(margins.m_left == 90);
    REQUIRE(margins.m_right == 90);
    REQUIRE(std::round(margins.m_top) == 274);
    REQUIRE(margins.m_top == doctest::Approx(margins.m_bottom));
    auto artboards = Rect(0, 0, 3910, 1773);
    fit = fit_view(artboards, NARROW);
    REQUIRE(to_percent(fit.m_zoom) == 43.3);
    margins = get_margins(fit, artboards, NARROW);
    REQUIRE(margins.m_left == 90);
    REQUIRE(std::round(margins.m_top) == 413);
    auto rectangle = Rect(10, 20, 100, 50);
    fit = fit_view(rectangle, WIDE);
    REQUIRE(to_percent(fit.m_zoom) == 1856);
    margins = get_margins(fit, rectangle, WIDE);
    REQUIRE(margins.m_left == 90);
    REQUIRE(margins.m_right == 90);
    REQUIRE(to_percent(fit_view(Rect(0, 0, 1724, 21), WIDE).m_zoom) ==
      107.7);
  }

  TEST_CASE("height_limited") {
    auto line = Rect(5, 5, 4, 21);
    auto fit = fit_view(line, WIDE);
    REQUIRE(to_percent(fit.m_zoom) == 5585.7);
    auto margins = get_margins(fit, line, WIDE);
    REQUIRE(margins.m_top == 108.75);
    REQUIRE(margins.m_bottom == 108.75);
    REQUIRE(margins.m_left == doctest::Approx(margins.m_right));
    auto ipad = Rect(2000, 0, 1024, 1366);
    fit = fit_view(ipad, WIDE);
    REQUIRE(to_percent(fit.m_zoom) == 85.9);
    margins = get_margins(fit, ipad, WIDE);
    REQUIRE(margins.m_top == 108.75);
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

  TEST_CASE("fit_window") {
    auto area = Rect(0, 0, 3840, 2088);
    REQUIRE(fit_window(Rect(770, 334, 2150, 1330), area) ==
      Rect(770, 334, 2150, 1330));
    REQUIRE(fit_window(Rect(1404, 654, 2400, 1440), area) ==
      Rect(1404, 648, 2400, 1440));
    REQUIRE(fit_window(Rect(1500, 700, 2400, 1440), area) ==
      Rect(1440, 648, 2400, 1440));
    REQUIRE(fit_window(Rect(-30, -5, 2400, 1440), area) ==
      Rect(0, 0, 2400, 1440));
    REQUIRE(fit_window(Rect(100, 100, 4000, 2200), area) == area);
    REQUIRE(fit_window(Rect(-3000, 100, 2400, 1440),
      Rect(-2560, 0, 2560, 1400)) == Rect(-2560, 0, 2400, 1400));
  }
  TEST_CASE("zoom_limits") {
    REQUIRE(fit_view(Rect(0, 0, 0.001, 0.001), Size(1000, 800)).m_zoom ==
      MAXIMUM_ZOOM);
    REQUIRE(fit_view(Rect(0, 0, 1e9, 1e9), Size(1000, 800)).m_zoom ==
      MINIMUM_ZOOM);
  }
  TEST_CASE("snap_view_offset") {
    REQUIRE(snap_view_offset(Point(10.5, -3.25), Point(0, 0), 1) ==
      Point(10, -3));
    auto truncated = snap_view_offset(Point(9, 9), Point(-2.0 / 3, 0), 1.5);
    REQUIRE(truncated.m_x == doctest::Approx(26.0 / 3));
    REQUIRE(truncated.m_y == doctest::Approx(26.0 / 3));
    auto snapped = snap_view_offset(Point(10, 10), Point(-2.0 / 3, 0), 1.5);
    REQUIRE(snapped.m_x == doctest::Approx(-2.0 / 3 + 16.0 / 1.5));
    REQUIRE(snapped.m_y == doctest::Approx(10));
    REQUIRE(snap_view_offset(Point(1, 2), Point(0, 0), 0) == Point(1, 2));
  }

  TEST_CASE("xd_view_placement") {
    auto ratio = 1.5;
    auto canvas = Point(687 / ratio, 111 / ratio);
    auto region = Rect(686 / ratio - canvas.m_x, 0, 2719 / ratio,
      1977 / ratio);
    auto fit = fit_view(Rect(0, 0, 1200, 400), region.get_size());
    auto zoom = fit.m_zoom;
    auto offset = fit.m_offset + region.get_top_left();
    auto place = [&] {
      auto snapped =
        snap_view_offset(offset, region.get_top_left(), ratio);
      return Point(std::round((canvas.m_x + snapped.m_x) * ratio),
        std::round((canvas.m_y + snapped.m_y) * ratio));
    };
    REQUIRE(to_percent(zoom) == 141.1);
    REQUIRE(place() == Point(776, 676));
    auto center = region.get_center();
    auto set_zoom = [&] (double percent) {
      auto anchor = Point((center.m_x - offset.m_x) / zoom,
        (center.m_y - offset.m_y) / zoom);
      zoom = percent / 100;
      offset = Point(center.m_x - zoom * anchor.m_x,
        center.m_y - zoom * anchor.m_y);
    };
    set_zoom(125);
    REQUIRE(place() == Point(920, 724));
    set_zoom(150);
    REQUIRE(place() == Point(695, 649));
    set_zoom(175);
    REQUIRE(place().m_y == 574);
    set_zoom(200);
    REQUIRE(place().m_y == 499);
    set_zoom(250);
    REQUIRE(place().m_y == 349);
    set_zoom(300);
    REQUIRE(place().m_y == 199);
    set_zoom(33);
    REQUIRE(place() == Point(1748, 1000));
    set_zoom(66);
    REQUIRE(place() == Point(1451, 901));
  }
  TEST_CASE("xd_tall_fit") {
    auto ratio = 1.5;
    auto canvas = Point(687 / ratio, 111 / ratio);
    auto region = Rect(686 / ratio - canvas.m_x, 0, 2719 / ratio,
      1977 / ratio);
    for(auto [height, zoom, left] : {std::tuple(2000.0, 0.5865, 1869.0),
        std::tuple(1200.0, 0.9775, 1752.0)}) {
      auto fit = fit_view(Rect(0, 0, 400, height), region.get_size());
      REQUIRE(fit.m_zoom == doctest::Approx(zoom));
      auto offset = snap_view_offset(fit.m_offset + region.get_top_left(),
        region.get_top_left(), ratio);
      REQUIRE(std::round((canvas.m_x + offset.m_x) * ratio) == left);
      REQUIRE(std::round((canvas.m_y + offset.m_y) * ratio) == 219);
      auto bottom = (canvas.m_y + offset.m_y + fit.m_zoom * height) * ratio;
      REQUIRE(bottom == doctest::Approx(1978.5));
    }
  }
  TEST_CASE("minimum_client_size") {
    REQUIRE(get_minimum_client_size(1.5, 11) == Size(750, 739));
    REQUIRE(get_minimum_client_size(1, 8) == Size(500, 492));
  }
  TEST_CASE("fit_pane_width") {
    auto to_logical = [] (double device) {
      return device / 1.5;
    };
    auto available = [&] (double client) {
      return to_logical(client - 75 - 435);
    };
    auto to_device = [] (double logical) {
      return std::round(logical * 1.5);
    };
    REQUIRE(to_device(fit_pane_width(to_logical(600), available(1200))) ==
      450);
    auto narrow = fit_pane_width(to_logical(600), available(900));
    REQUIRE(to_device(narrow) == 330);
    REQUIRE(to_device(fit_pane_width(narrow, available(3840))) == 366);
    REQUIRE(to_device(fit_pane_width(to_logical(450), available(3840))) ==
      450);
    REQUIRE(to_device(fit_pane_width(to_logical(611), available(750))) ==
      180);
    REQUIRE(to_device(fit_pane_width(to_logical(4000), available(3840))) ==
      3090);
    REQUIRE(fit_pane_width(500, 30) == 0);
  }
}

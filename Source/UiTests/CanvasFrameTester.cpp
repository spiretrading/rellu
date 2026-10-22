#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Ui/CanvasFrame.hpp"

using namespace Rellu;

TEST_SUITE("CanvasFrame") {
  TEST_CASE("align_to_device_pixels") {
    REQUIRE(align_to_device_pixels(457, 1.5) == 458);
    REQUIRE(align_to_device_pixels(458, 1.5) == 458);
    REQUIRE(align_to_device_pixels(-3, 1.5) == -2);
    REQUIRE(align_to_device_pixels(457, 1.25) == 460);
    REQUIRE(align_to_device_pixels(457, 1.75) == 460);
    REQUIRE(align_to_device_pixels(457, 1) == 457);
    REQUIRE(align_to_device_pixels(457, 2) == 457);
    REQUIRE(align_to_device_pixels(457, std::numbers::sqrt2) == 457);
  }

  TEST_CASE("get_view_region") {
    auto region =
      get_view_region(QRect(457, 74, 1813, 1318), QPoint(458, 74), 1.5);
    REQUIRE(region.m_x * 1.5 == doctest::Approx(-1));
    REQUIRE(region.m_y == 0);
    REQUIRE(region.m_width * 1.5 == doctest::Approx(2719));
    REQUIRE(region.m_height * 1.5 == doctest::Approx(1977));
    REQUIRE(get_view_region(QRect(3, 5, 40, 30), QPoint(3, 5), 1) ==
      Rect(0, 0, 40, 30));
  }

  TEST_CASE("placement") {
    auto window = QWidget();
    auto canvas = new Canvas();
    auto frame = new CanvasFrame(*canvas, &window);
    frame->setGeometry(3, 5, 40, 30);
    window.show();
    REQUIRE(&frame->get_canvas() == canvas);
    REQUIRE(canvas->parentWidget() == frame);
    REQUIRE(canvas->geometry() == QRect(0, 0, 40, 30));
    REQUIRE(canvas->get_view_region() == Rect(0, 0, 40, 30));
    frame->resize(50, 20);
    REQUIRE(canvas->geometry() == QRect(0, 0, 50, 20));
  }
}

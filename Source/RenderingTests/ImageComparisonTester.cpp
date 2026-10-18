#include <doctest/doctest.h>
#include <QImage>
#include "Rellu/Rendering/ImageComparison.hpp"

using namespace Rellu;

namespace {
  auto make_image(QSize size, QColor color) {
    auto image = QImage(size, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
  }
}

TEST_SUITE("ImageComparison") {
  TEST_CASE("identical") {
    auto image = make_image(QSize(10, 8), QColor(10, 20, 30));
    auto comparison = compare_images(image, image);
    REQUIRE(comparison.m_is_match);
    REQUIRE(comparison.m_pixels == 80);
    REQUIRE(comparison.m_mismatches == 0);
    REQUIRE(comparison.m_fraction == 0);
    REQUIRE(comparison.m_maximum_difference == 0);
    REQUIRE(comparison.m_mean_difference == 0);
    REQUIRE(comparison.m_highlight.size() == QSize(10, 8));
  }

  TEST_CASE("within_tolerance") {
    auto expected = make_image(QSize(10, 10), QColor(100, 100, 100));
    auto actual = expected;
    actual.setPixelColor(0, 0, QColor(108, 100, 100));
    actual.setPixelColor(1, 0, QColor(90, 100, 100));
    actual.setPixelColor(2, 0, QColor(100, 104, 100));
    auto comparison = compare_images(actual, expected, ImageTolerance(10, 0));
    REQUIRE(comparison.m_is_match);
    REQUIRE(comparison.m_mismatches == 0);
    REQUIRE(comparison.m_maximum_difference == 10);
    REQUIRE(comparison.m_mean_difference == doctest::Approx(22.0 / 400));
    auto fraction = expected;
    fraction.setPixelColor(5, 5, QColor(0, 0, 0));
    auto fraction_comparison =
      compare_images(fraction, expected, ImageTolerance(0, 0.01));
    REQUIRE(fraction_comparison.m_is_match);
    REQUIRE(fraction_comparison.m_mismatches == 1);
    REQUIRE(fraction_comparison.m_fraction == doctest::Approx(0.01));
  }

  TEST_CASE("beyond_tolerance") {
    auto expected = make_image(QSize(10, 10), QColor(100, 100, 100));
    auto actual = expected;
    actual.setPixelColor(3, 4, QColor(100, 100, 120));
    actual.setPixelColor(7, 2, QColor(100, 100, 120));
    auto comparison =
      compare_images(actual, expected, ImageTolerance(10, 0.01));
    REQUIRE(!comparison.m_is_match);
    REQUIRE(comparison.m_mismatches == 2);
    REQUIRE(comparison.m_fraction == doctest::Approx(0.02));
    REQUIRE(comparison.m_maximum_difference == 20);
    REQUIRE(comparison.m_mean_difference == doctest::Approx(40.0 / 400));
    auto default_comparison = compare_images(actual, expected);
    REQUIRE(!default_comparison.m_is_match);
    REQUIRE(default_comparison.m_mismatches == 2);
  }

  TEST_CASE("size_mismatch") {
    auto expected = make_image(QSize(4, 2), QColor(0, 0, 255));
    auto actual = make_image(QSize(2, 3), QColor(0, 0, 255));
    auto comparison =
      compare_images(actual, expected, ImageTolerance(255, 1));
    REQUIRE(!comparison.m_is_match);
    REQUIRE(comparison.m_pixels == 12);
    REQUIRE(comparison.m_highlight.size() == QSize(4, 3));
    auto strict = compare_images(actual, expected, ImageTolerance(0, 1));
    REQUIRE(strict.m_mismatches == 6);
    REQUIRE(strict.m_maximum_difference == 255);
    REQUIRE(strict.m_mean_difference == doctest::Approx(6.0 * 2 * 255 / 48));
    auto empty = compare_images(QImage(), QImage());
    REQUIRE(empty.m_is_match);
    REQUIRE(empty.m_pixels == 0);
    REQUIRE(empty.m_fraction == 0);
  }

  TEST_CASE("premultiplied_alpha") {
    auto expected = make_image(QSize(2, 2), QColor(255, 0, 0, 0));
    auto actual = make_image(QSize(2, 2), QColor(0, 255, 0, 0));
    REQUIRE(compare_images(actual, expected, ImageTolerance(0, 0)).m_is_match);
    auto translucent = make_image(QSize(2, 2), QColor(255, 255, 255, 2));
    auto clear = make_image(QSize(2, 2), QColor(0, 0, 0, 0));
    auto comparison =
      compare_images(translucent, clear, ImageTolerance(2, 0));
    REQUIRE(comparison.m_is_match);
    REQUIRE(comparison.m_maximum_difference == 2);
  }

  TEST_CASE("highlight") {
    auto expected = make_image(QSize(3, 2), QColor(0, 0, 0));
    expected.setPixelColor(1, 0, QColor(255, 255, 255));
    expected.setPixelColor(2, 0, QColor(0, 0, 0, 0));
    auto actual = expected;
    actual.setPixelColor(0, 1, QColor(200, 0, 0));
    auto comparison = compare_images(actual, expected, ImageTolerance(8, 1));
    auto highlight =
      comparison.m_highlight.convertToFormat(QImage::Format_RGB32);
    REQUIRE(highlight.pixel(0, 1) == qRgb(255, 0, 0));
    REQUIRE(highlight.pixel(0, 0) == qRgb(192, 192, 192));
    REQUIRE(highlight.pixel(1, 0) == qRgb(255, 255, 255));
    REQUIRE(highlight.pixel(2, 0) == qRgb(255, 255, 255));
    REQUIRE(highlight.pixel(1, 1) == qRgb(192, 192, 192));
  }
}

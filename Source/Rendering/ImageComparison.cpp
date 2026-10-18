#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include "Rellu/Rendering/ImageComparison.hpp"

using namespace Rellu;

namespace {
  const auto TRANSPARENT_PIXEL = std::array<uchar, 4>();
  const auto MISMATCH = qRgb(255, 0, 0);

  auto to_premultiplied(const QImage& image) {
    return image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
  }

  const uchar* get_pixel(const QImage& image, int x, int y) {
    if(x >= image.width() || y >= image.height()) {
      return TRANSPARENT_PIXEL.data();
    }
    return image.constScanLine(y) + 4 * x;
  }

  QRgb fade(const uchar* pixel) {
    auto red = pixel[0] + 255 - pixel[3];
    auto green = pixel[1] + 255 - pixel[3];
    auto blue = pixel[2] + 255 - pixel[3];
    auto luma = (299 * red + 587 * green + 114 * blue) / 1000;
    auto gray = 255 - (255 - luma) / 4;
    return qRgb(gray, gray, gray);
  }
}

ImageTolerance::ImageTolerance() noexcept
  : ImageTolerance(16, 0.005) {}

ImageTolerance::ImageTolerance(int difference, double fraction) noexcept
  : m_difference(difference),
    m_fraction(fraction) {}

ImageComparison Rellu::compare_images(const QImage& actual,
    const QImage& expected, const ImageTolerance& tolerance) {
  auto left = to_premultiplied(actual);
  auto right = to_premultiplied(expected);
  auto width = std::max(left.width(), right.width());
  auto height = std::max(left.height(), right.height());
  auto comparison = ImageComparison();
  comparison.m_pixels = width * height;
  comparison.m_mismatches = 0;
  comparison.m_maximum_difference = 0;
  comparison.m_highlight = QImage(width, height, QImage::Format_RGB32);
  auto total = std::int64_t(0);
  for(auto y = 0; y != height; ++y) {
    auto line = reinterpret_cast<QRgb*>(comparison.m_highlight.scanLine(y));
    for(auto x = 0; x != width; ++x) {
      auto actual_pixel = get_pixel(left, x, y);
      auto expected_pixel = get_pixel(right, x, y);
      auto maximum = 0;
      for(auto channel = 0; channel != 4; ++channel) {
        auto difference =
          std::abs(int(actual_pixel[channel]) - int(expected_pixel[channel]));
        total += difference;
        maximum = std::max(maximum, difference);
      }
      comparison.m_maximum_difference =
        std::max(comparison.m_maximum_difference, maximum);
      if(maximum > tolerance.m_difference) {
        ++comparison.m_mismatches;
        line[x] = MISMATCH;
      } else {
        line[x] = fade(expected_pixel);
      }
    }
  }
  if(comparison.m_pixels == 0) {
    comparison.m_fraction = 0;
    comparison.m_mean_difference = 0;
  } else {
    comparison.m_fraction =
      double(comparison.m_mismatches) / comparison.m_pixels;
    comparison.m_mean_difference =
      double(total) / (4 * std::int64_t(comparison.m_pixels));
  }
  comparison.m_is_match = left.size() == right.size() &&
    comparison.m_fraction <= tolerance.m_fraction;
  return comparison;
}

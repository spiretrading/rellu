#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>
#include "BoxShadow.hpp"

using namespace Rellu;

namespace {
  constexpr auto SHADOW_REACH = 2.5;
  constexpr auto LAYER_COUNT = std::size_t(2);

  struct ShadowLayer {
    double m_opacity;
    double m_radius;
    double m_offset;
  };

  constexpr auto TOOL_TIP_SHADOW = std::array<ShadowLayer, LAYER_COUNT>{
    ShadowLayer(0.1375, 2.25, 0.4583), ShadowLayer(0.19, 8.9167, 7.6667)};
  constexpr auto CARD_SHADOW = std::array<ShadowLayer, LAYER_COUNT>{
    ShadowLayer(0.07, 1.5, 0.3333), ShadowLayer(0.07, 3.3333, 0.6667)};

  double get_coverage(double position, double start, double end,
      double radius) {
    auto cumulative = [&] (double value) {
      return 0.5 * (1 + std::erf(value / (radius * std::sqrt(2.0))));
    };
    return cumulative(position - start) - cumulative(position - end);
  }
}

BoxShadow Rellu::make_box_shadow(
    const QRect& box, double ratio, ShadowKind kind) {
  auto& layers = kind == ShadowKind::CARD ? CARD_SHADOW : TOOL_TIP_SHADOW;
  auto reach = std::ceil(layers[1].m_radius * SHADOW_REACH * ratio);
  auto margin_left = static_cast<int>(reach);
  auto margin_top = static_cast<int>(
    std::max(0.0, reach - layers[1].m_offset * ratio));
  auto margin_right = static_cast<int>(reach);
  auto margin_bottom = static_cast<int>(reach + layers[1].m_offset * ratio);
  auto position = QPoint(
    static_cast<int>(std::floor((box.left() - margin_left) / ratio)),
    static_cast<int>(std::floor((box.top() - margin_top) / ratio)));
  auto native = QPoint(static_cast<int>(std::lround(position.x() * ratio)),
    static_cast<int>(std::lround(position.y() * ratio)));
  auto local = box.translated(-native);
  auto image = QImage(local.right() + 1 + margin_right,
    local.bottom() + 1 + margin_bottom, QImage::Format_ARGB32_Premultiplied);
  auto columns = std::vector<std::array<double, LAYER_COUNT>>(
    image.width());
  auto rows = std::vector<std::array<double, LAYER_COUNT>>(image.height());
  for(auto i = std::size_t(0); i != LAYER_COUNT; ++i) {
    auto radius = layers[i].m_radius * ratio;
    auto offset = layers[i].m_offset * ratio;
    for(auto x = 0; x != image.width(); ++x) {
      columns[x][i] =
        get_coverage(x + 0.5, local.left(), local.right() + 1, radius);
    }
    for(auto y = 0; y != image.height(); ++y) {
      rows[y][i] = get_coverage(
        y + 0.5, local.top() + offset, local.bottom() + 1 + offset, radius);
    }
  }
  for(auto y = 0; y != image.height(); ++y) {
    auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for(auto x = 0; x != image.width(); ++x) {
      auto alpha = 0.0;
      for(auto i = std::size_t(0); i != LAYER_COUNT; ++i) {
        alpha += layers[i].m_opacity * columns[x][i] * rows[y][i];
      }
      line[x] = qRgba(0, 0, 0,
        static_cast<int>(std::lround(std::min(alpha, 1.0) * 255)));
    }
  }
  auto size = QSize(static_cast<int>(std::ceil(image.width() / ratio)) + 1,
    static_cast<int>(std::ceil(image.height() / ratio)) + 1);
  return BoxShadow(std::move(image), local, position, size);
}

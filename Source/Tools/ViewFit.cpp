#include <algorithm>
#include <cmath>
#include <limits>
#include "Rellu/Tools/ViewFit.hpp"

using namespace Rellu;

namespace {
  Size get_fitted_size(Size size) {
    return Size(std::max(size.m_width - 2 * FIT_PADDING_X, 1.0),
      std::max(size.m_height - 2 * FIT_PADDING_Y, 1.0));
  }
}

ViewFit Rellu::fit_view(const Rect& region, Size size) {
  auto fit = ViewFit();
  auto fitted = get_fitted_size(size);
  auto zoom = std::numeric_limits<double>::infinity();
  if(region.m_width > 0) {
    zoom = fitted.m_width / region.m_width;
  }
  if(region.m_height > 0) {
    zoom = std::min(zoom, fitted.m_height / region.m_height);
  }
  if(std::isfinite(zoom)) {
    fit.m_zoom = std::clamp(zoom, MINIMUM_ZOOM, MAXIMUM_ZOOM);
  }
  auto center = region.get_center();
  fit.m_offset = Point(size.m_width / 2 - fit.m_zoom * center.m_x,
    size.m_height / 2 - fit.m_zoom * center.m_y);
  return fit;
}

Rect Rellu::get_fitted_region(Point center, double zoom, Size size) {
  auto fitted = get_fitted_size(size);
  auto width = fitted.m_width / zoom;
  auto height = fitted.m_height / zoom;
  return Rect(center.m_x - width / 2, center.m_y - height / 2, width, height);
}

bool Rellu::is_settled(
    bool is_restored_expanded, bool is_expanded, Size size, Size area) {
  return !is_restored_expanded || (is_expanded &&
    size.m_width >= area.m_width - 1 && size.m_height >= area.m_height - 1);
}

Rect Rellu::center_window(Size size, const Rect& area) {
  auto width = std::min(size.m_width, area.m_width);
  auto height = std::min(size.m_height, area.m_height);
  return Rect(area.m_x + std::floor((area.m_width - width) / 2),
    area.m_y + std::floor((area.m_height - height) / 2), width, height);
}

Rect Rellu::fit_window(const Rect& bounds, const Rect& area) {
  auto width = std::min(bounds.m_width, area.m_width);
  auto height = std::min(bounds.m_height, area.m_height);
  return Rect(
    std::clamp(bounds.m_x, area.m_x, area.get_right() - width),
    std::clamp(bounds.m_y, area.m_y, area.get_bottom() - height), width,
    height);
}
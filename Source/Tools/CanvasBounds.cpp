#include <algorithm>
#include "Rellu/Tools/CanvasBounds.hpp"

using namespace Rellu;

std::vector<Rect> Rellu::get_outside_regions(const Matrix& view, Size size) {
  auto regions = std::vector<Rect>();
  auto inside = view.map(CANVAS_BOUNDS);
  auto left = std::clamp(inside.get_left(), 0.0, size.m_width);
  auto right = std::clamp(inside.get_right(), 0.0, size.m_width);
  auto top = std::clamp(inside.get_top(), 0.0, size.m_height);
  auto bottom = std::clamp(inside.get_bottom(), 0.0, size.m_height);
  if(left > 0) {
    regions.push_back(Rect(0, 0, left, size.m_height));
  }
  if(right < size.m_width) {
    regions.push_back(Rect(right, 0, size.m_width - right, size.m_height));
  }
  if(right > left) {
    if(top > 0) {
      regions.push_back(Rect(left, 0, right - left, top));
    }
    if(bottom < size.m_height) {
      regions.push_back(
        Rect(left, bottom, right - left, size.m_height - bottom));
    }
  }
  return regions;
}

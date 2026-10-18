#include <cmath>
#include "Rellu/Rendering/CanvasOverlay.hpp"

using namespace Rellu;

OverlayShape Rellu::make_overlay_line(
    Point start, Point end, double width, Color color) {
  auto shape = OverlayShape();
  shape.m_center = interpolate(start, end, 0.5);
  shape.m_size = Size(distance(start, end), width);
  shape.m_angle = std::atan2(end.m_y - start.m_y, end.m_x - start.m_x);
  shape.m_fill = color;
  return shape;
}

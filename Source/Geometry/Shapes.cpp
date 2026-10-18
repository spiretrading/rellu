#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>
#include "Rellu/Geometry/Shapes.hpp"

using namespace Rellu;

namespace {
  constexpr auto KAPPA = 4 * (std::numbers::sqrt2 - 1) / 3;

  struct Corner {
    Point m_start;
    Point m_first;
    Point m_second;
    Point m_end;
    bool m_is_rounded;
  };

  Corner make_rectangle_corner(
      Point vertex, Point incoming, Point outgoing, double radius) {
    if(radius <= 0) {
      return Corner(vertex, vertex, vertex, vertex, false);
    }
    auto start = vertex - incoming * radius;
    auto end = vertex + outgoing * radius;
    return Corner(start, start + incoming * (KAPPA * radius),
      end - outgoing * (KAPPA * radius), end, true);
  }

  Corner make_corner(Point previous, Point vertex, Point next, double radius) {
    auto before = previous - vertex;
    auto after = next - vertex;
    auto before_length = length(before);
    auto after_length = length(after);
    if(radius <= 0 || before_length == 0 || after_length == 0) {
      return Corner(vertex, vertex, vertex, vertex, false);
    }
    before = before / before_length;
    after = after / after_length;
    auto angle = std::atan2(std::abs(cross(before, after)), dot(before, after));
    auto half_tangent = std::tan(angle / 2);
    if(half_tangent <= 0 || !std::isfinite(radius / half_tangent)) {
      return Corner(vertex, vertex, vertex, vertex, false);
    }
    auto distance = std::min(
      radius / half_tangent, std::min(before_length, after_length) / 2);
    auto effective_radius = distance * half_tangent;
    auto handle =
      4 * std::tan((std::numbers::pi - angle) / 4) * effective_radius / 3;
    auto start = vertex + before * distance;
    auto end = vertex + after * distance;
    return Corner(
      start, start - before * handle, end - after * handle, end, true);
  }

  Path make_path(const std::vector<Corner>& corners) {
    auto path = Path();
    if(corners.empty()) {
      return path;
    }
    auto start = corners.front().m_start;
    auto current = start;
    path.move_to(start);
    for(auto& corner : corners) {
      if(corner.m_start != current && corner.m_start != start) {
        path.line_to(corner.m_start);
      }
      if(corner.m_is_rounded) {
        path.cubic_to(corner.m_first, corner.m_second, corner.m_end);
      }
      current = corner.m_end;
    }
    path.close();
    return path;
  }
}

CornerRadii Rellu::clamp_corner_radii(const CornerRadii& radii, Size size) {
  auto limit = std::max(0.0, std::min(size.m_width, size.m_height) / 2);
  return CornerRadii(std::clamp(radii.m_top_left, 0.0, limit),
    std::clamp(radii.m_top_right, 0.0, limit),
    std::clamp(radii.m_bottom_right, 0.0, limit),
    std::clamp(radii.m_bottom_left, 0.0, limit));
}

Path Rellu::make_rectangle(const Rect& bounds) {
  return make_rectangle(bounds, CornerRadii(0, 0, 0, 0));
}

Path Rellu::make_rectangle(const Rect& bounds, const CornerRadii& radii) {
  auto effective_radii = clamp_corner_radii(radii, bounds.get_size());
  auto right = Point(1, 0);
  auto down = Point(0, 1);
  auto corners = std::vector<Corner>();
  corners.push_back(make_rectangle_corner(
    bounds.get_top_left(), -down, right, effective_radii.m_top_left));
  corners.push_back(make_rectangle_corner(Point(bounds.get_right(),
    bounds.get_top()), right, down, effective_radii.m_top_right));
  corners.push_back(make_rectangle_corner(bounds.get_bottom_right(), down,
    -right, effective_radii.m_bottom_right));
  corners.push_back(make_rectangle_corner(Point(bounds.get_left(),
    bounds.get_bottom()), -right, -down, effective_radii.m_bottom_left));
  return make_path(corners);
}

Path Rellu::make_ellipse(Point center, double x_radius, double y_radius) {
  auto x = Point(x_radius, 0);
  auto y = Point(0, y_radius);
  auto path = Path();
  path.move_to(center + x);
  path.cubic_to(center + x + KAPPA * y, center + KAPPA * x + y, center + y);
  path.cubic_to(center - KAPPA * x + y, center - x + KAPPA * y, center - x);
  path.cubic_to(center - x - KAPPA * y, center - KAPPA * x - y, center - y);
  path.cubic_to(center + KAPPA * x - y, center + x - KAPPA * y, center + x);
  path.close();
  return path;
}

Path Rellu::make_circle(Point center, double radius) {
  return make_ellipse(center, radius, radius);
}

Path Rellu::make_line(Point start, Point end) {
  auto path = Path();
  path.move_to(start);
  path.line_to(end);
  return path;
}

Path Rellu::make_polygon(
    std::span<const Point> vertices, double corner_radius) {
  auto corners = std::vector<Corner>();
  auto count = vertices.size();
  for(auto i = std::size_t(0); i != count; ++i) {
    corners.push_back(make_corner(vertices[(i + count - 1) % count],
      vertices[i], vertices[(i + 1) % count], corner_radius));
  }
  return make_path(corners);
}

Path Rellu::make_polygon(
    int corner_count, const Rect& bounds, double corner_radius) {
  if(corner_count < 3) {
    return Path();
  }
  auto vertices = std::vector<Point>();
  for(auto i = 0; i != corner_count; ++i) {
    auto angle =
      -std::numbers::pi / 2 + 2 * std::numbers::pi * i / corner_count;
    vertices.push_back(Point(std::cos(angle), std::sin(angle)));
  }
  auto left = vertices.front().m_x;
  auto top = vertices.front().m_y;
  auto right = left;
  auto bottom = top;
  for(auto& vertex : vertices) {
    left = std::min(left, vertex.m_x);
    top = std::min(top, vertex.m_y);
    right = std::max(right, vertex.m_x);
    bottom = std::max(bottom, vertex.m_y);
  }
  for(auto& vertex : vertices) {
    vertex = Point(
      bounds.m_x + (vertex.m_x - left) / (right - left) * bounds.m_width,
      bounds.m_y + (vertex.m_y - top) / (bottom - top) * bounds.m_height);
  }
  return make_polygon(vertices, corner_radius);
}

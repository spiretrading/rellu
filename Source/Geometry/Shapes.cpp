#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>
#include "Rellu/Geometry/Shapes.hpp"

using namespace Rellu;

namespace {
  struct Corner {
    Point m_start;
    Point m_first;
    Point m_second;
    Point m_end;
    bool m_is_rounded;
  };

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
  if(!(size.m_width > 0) || !(size.m_height > 0)) {
    return CornerRadii(0, 0, 0, 0);
  }
  auto clamp = [] (double radius) {
    if(!std::isfinite(radius)) {
      return 0.0;
    }
    return std::max(0.0, radius);
  };
  auto top_left = clamp(radii.m_top_left);
  auto top_right = clamp(radii.m_top_right);
  auto bottom_right = clamp(radii.m_bottom_right);
  auto bottom_left = clamp(radii.m_bottom_left);
  auto scale = 1.0;
  auto fit = [&] (double first, double second, double length) {
    if(first + second > length) {
      scale = std::min(scale, length / (first + second));
    }
  };
  fit(top_left, top_right, size.m_width);
  fit(top_right, bottom_right, size.m_height);
  fit(bottom_right, bottom_left, size.m_width);
  fit(bottom_left, top_left, size.m_height);
  return CornerRadii(scale * top_left, scale * top_right,
    scale * bottom_right, scale * bottom_left);
}

Path Rellu::make_rectangle(const Rect& bounds) {
  return make_rectangle(bounds, CornerRadii(0, 0, 0, 0));
}

Path Rellu::make_rectangle(const Rect& bounds, const CornerRadii& radii) {
  auto left = bounds.get_left();
  auto top = bounds.get_top();
  auto right = bounds.get_right();
  auto bottom = bounds.get_bottom();
  auto path = Path();
  auto effective = clamp_corner_radii(radii, bounds.get_size());
  if(effective.m_top_left <= 0 && effective.m_top_right <= 0 &&
      effective.m_bottom_right <= 0 && effective.m_bottom_left <= 0) {
    path.move_to(Point(left, top));
    path.line_to(Point(right, top));
    path.line_to(Point(right, bottom));
    path.line_to(Point(left, bottom));
    path.close();
    return path;
  }
  auto half = bounds.m_width / 2;
  auto is_circle = bounds.m_width == bounds.m_height &&
    effective == CornerRadii(half, half, half, half);
  auto add_side = [&] (Point end) {
    if(!is_circle) {
      path.line_to(end);
    }
  };
  auto add_corner = [&] (Point corner, Point end) {
    path.arc_to(corner, end);
  };
  path.move_to(Point(left, bottom - effective.m_bottom_left));
  add_side(Point(left, top + effective.m_top_left));
  add_corner(Point(left, top), Point(left + effective.m_top_left, top));
  add_side(Point(right - effective.m_top_right, top));
  add_corner(Point(right, top), Point(right, top + effective.m_top_right));
  add_side(Point(right, bottom - effective.m_bottom_right));
  add_corner(
    Point(right, bottom), Point(right - effective.m_bottom_right, bottom));
  add_side(Point(left + effective.m_bottom_left, bottom));
  add_corner(
    Point(left, bottom), Point(left, bottom - effective.m_bottom_left));
  path.close();
  return path;
}

Path Rellu::make_ellipse(Point center, double x_radius, double y_radius) {
  auto x = Point(x_radius, 0);
  auto y = Point(0, y_radius);
  auto path = Path();
  path.move_to(center + x);
  path.arc_to(center + x + y, center + y);
  path.arc_to(center - x + y, center - x);
  path.arc_to(center - x - y, center - y);
  path.arc_to(center + x - y, center + x);
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

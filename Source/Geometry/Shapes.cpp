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

  double get_half_tangent(Point previous, Point vertex, Point next) {
    auto before = previous - vertex;
    auto after = next - vertex;
    auto before_length = length(before);
    auto after_length = length(after);
    if(before_length == 0 || after_length == 0) {
      return 0;
    }
    before = before / before_length;
    after = after / after_length;
    return std::tan(
      std::atan2(std::abs(cross(before, after)), dot(before, after)) / 2);
  }

  Corner make_corner(Point previous, Point vertex, Point next, double radius) {
    auto half_tangent = get_half_tangent(previous, vertex, next);
    if(radius <= 0 || half_tangent <= 0 ||
        !std::isfinite(radius / half_tangent)) {
      return Corner(vertex, vertex, vertex, vertex, false);
    }
    auto before = (previous - vertex) / length(previous - vertex);
    auto after = (next - vertex) / length(next - vertex);
    auto angle = 2 * std::atan(half_tangent);
    auto distance = radius / half_tangent;
    auto handle = 4 * std::tan((std::numbers::pi - angle) / 4) * radius / 3;
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
  auto count = vertices.size();
  auto radius = corner_radius;
  if(radius > 0) {
    auto distances = std::vector<double>(count, 0);
    for(auto i = std::size_t(0); i != count; ++i) {
      auto half_tangent = get_half_tangent(vertices[(i + count - 1) % count],
        vertices[i], vertices[(i + 1) % count]);
      if(half_tangent > 0) {
        distances[i] = 1 / half_tangent;
      }
    }
    for(auto i = std::size_t(0); i != count; ++i) {
      auto next = (i + 1) % count;
      auto needed = radius * (distances[i] + distances[next]);
      auto edge = distance(vertices[i], vertices[next]);
      if(needed > edge) {
        radius *= edge / needed;
      }
    }
  }
  auto corners = std::vector<Corner>();
  for(auto i = std::size_t(0); i != count; ++i) {
    corners.push_back(make_corner(vertices[(i + count - 1) % count],
      vertices[i], vertices[(i + 1) % count], radius));
  }
  return make_path(corners);
}

std::vector<Point> Rellu::make_polygon_vertices(
    int corner_count, const Rect& bounds, double star_ratio) {
  auto corners = std::vector<Point>();
  if(corner_count < 3) {
    return corners;
  }
  auto start = -std::numbers::pi / 2;
  if(corner_count % 2 == 0) {
    start += std::numbers::pi / corner_count;
  }
  for(auto i = 0; i != corner_count; ++i) {
    auto angle = start + 2 * std::numbers::pi * i / corner_count;
    corners.push_back(Point(std::cos(angle), std::sin(angle)));
  }
  auto left = corners.front().m_x;
  auto top = corners.front().m_y;
  auto right = left;
  auto bottom = top;
  for(auto& corner : corners) {
    left = std::min(left, corner.m_x);
    top = std::min(top, corner.m_y);
    right = std::max(right, corner.m_x);
    bottom = std::max(bottom, corner.m_y);
  }
  auto vertices = std::vector<Point>();
  auto is_star = star_ratio < 100;
  vertices.reserve(is_star ? 2 * corners.size() : corners.size());
  for(auto i = std::size_t(0); i != corners.size(); ++i) {
    vertices.push_back(corners[i]);
    if(is_star) {
      auto middle = (corners[i] + corners[(i + 1) % corners.size()]) / 2;
      vertices.push_back(middle * (star_ratio / 100));
    }
  }
  for(auto& vertex : vertices) {
    vertex = Point(
      bounds.m_x + (vertex.m_x - left) / (right - left) * bounds.m_width,
      bounds.m_y + (vertex.m_y - top) / (bottom - top) * bounds.m_height);
  }
  return vertices;
}

Path Rellu::make_polygon(int corner_count, const Rect& bounds,
    double corner_radius, double star_ratio) {
  return make_polygon(
    make_polygon_vertices(corner_count, bounds, star_ratio), corner_radius);
}

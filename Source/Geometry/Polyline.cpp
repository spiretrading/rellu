#include <algorithm>
#include <cmath>
#include <utility>
#include "Rellu/Geometry/Polyline.hpp"

using namespace Rellu;

namespace {
  int get_subdivisions(double deviation, double tolerance) {
    return std::max(1, static_cast<int>(std::ceil(
      std::sqrt(deviation / tolerance))));
  }

  void add_quadratic(std::vector<Point>& points, Point start, Point control,
      Point end, double tolerance) {
    auto count = get_subdivisions(
      length(start - 2 * control + end) / 4, tolerance);
    for(auto i = 1; i < count; ++i) {
      auto t = static_cast<double>(i) / count;
      auto s = 1 - t;
      points.push_back(s * s * start + 2 * s * t * control + t * t * end);
    }
    points.push_back(end);
  }

  void add_cubic(std::vector<Point>& points, Point start, Point first,
      Point second, Point end, double tolerance) {
    auto curvature = std::max(length(start - 2 * first + second),
      length(first - 2 * second + end));
    auto count = get_subdivisions(3 * curvature / 4, tolerance);
    for(auto i = 1; i < count; ++i) {
      auto t = static_cast<double>(i) / count;
      auto s = 1 - t;
      points.push_back(s * s * s * start + 3 * s * s * t * first +
        3 * s * t * t * second + t * t * t * end);
    }
    points.push_back(end);
  }

  void add_polyline(std::vector<Polyline>& polylines, Polyline polyline) {
    if(polyline.m_is_closed && polyline.m_points.size() > 1 &&
        polyline.m_points.back() == polyline.m_points.front()) {
      polyline.m_points.pop_back();
    }
    if(polyline.m_points.size() > 1 || polyline.m_is_closed) {
      polylines.push_back(std::move(polyline));
    }
  }
}

std::vector<Polyline> Rellu::flatten(const Path& path, double tolerance) {
  auto polylines = std::vector<Polyline>();
  auto polyline = Polyline();
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      add_polyline(polylines, std::move(polyline));
      polyline = Polyline();
      polyline.m_points.push_back(points[0]);
    } else if(segment.m_type == SegmentType::LINE) {
      polyline.m_points.push_back(points[0]);
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      add_quadratic(polyline.m_points, polyline.m_points.back(), points[0],
        points[1], tolerance);
    } else if(segment.m_type == SegmentType::CUBIC) {
      add_cubic(polyline.m_points, polyline.m_points.back(), points[0],
        points[1], points[2], tolerance);
    } else {
      polyline.m_is_closed = true;
    }
  }
  add_polyline(polylines, std::move(polyline));
  return polylines;
}

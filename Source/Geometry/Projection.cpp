#include "Rellu/Geometry/Projection.hpp"

using namespace Rellu;

namespace {
  constexpr auto CUBIC_DIVISIONS = 4;

  Point evaluate_cubic(
      Point start, Point first, Point second, Point end, double t) {
    auto s = 1 - t;
    return s * s * s * start + 3 * s * s * t * first +
      3 * s * t * t * second + t * t * t * end;
  }

  Point evaluate_derivative(
      Point start, Point first, Point second, Point end, double t) {
    auto s = 1 - t;
    return 3 * s * s * (first - start) + 6 * s * t * (second - first) +
      3 * t * t * (end - second);
  }
}

Path Rellu::transform(const Path& path, const Projection& projection) {
  auto result = Path(path.get_fill_rule());
  auto start = Point();
  auto current = Point();
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      result.move_to(projection.map(points[0]));
      start = points[0];
      current = start;
    } else if(segment.m_type == SegmentType::LINE) {
      result.line_to(projection.map(points[0]));
      current = points[0];
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      result.quadratic_to(
        projection.map(points[0]), projection.map(points[1]));
      current = points[1];
    } else if(segment.m_type == SegmentType::CUBIC) {
      if(segment.m_is_arc) {
        result.arc_to(projection.map(current + (points[0] - current) /
          ARC_CONTROL), projection.map(points[2]));
      } else {
        auto step = 1.0 / CUBIC_DIVISIONS;
        for(auto i = 0; i != CUBIC_DIVISIONS; ++i) {
          auto t0 = i * step;
          auto t1 = (i + 1) * step;
          auto first = evaluate_cubic(
            current, points[0], points[1], points[2], t0);
          auto last = evaluate_cubic(
            current, points[0], points[1], points[2], t1);
          auto first_control = first + evaluate_derivative(
            current, points[0], points[1], points[2], t0) * (step / 3);
          auto second_control = last - evaluate_derivative(
            current, points[0], points[1], points[2], t1) * (step / 3);
          if(i + 1 == CUBIC_DIVISIONS) {
            last = points[2];
          }
          result.cubic_to(projection.map(first_control),
            projection.map(second_control), projection.map(last));
        }
      }
      current = points[2];
    } else {
      result.close();
      current = start;
    }
  }
  return result;
}

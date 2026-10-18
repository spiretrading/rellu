#include <algorithm>
#include <cmath>
#include <limits>
#include <clipper2/clipper.h>
#include "Precision.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  constexpr auto MAXIMUM_SCALED_COORDINATE = 1e12;

  bool is_representable(const Segment& segment) {
    return std::all_of(segment.m_points.begin(), segment.m_points.end(),
      [] (const auto& point) {
        return std::abs(point.m_x) <= MAXIMUM_COORDINATE &&
          std::abs(point.m_y) <= MAXIMUM_COORDINATE;
      });
  }

  void append(Path& path, const Segment& segment) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      path.move_to(points[0]);
    } else if(segment.m_type == SegmentType::LINE) {
      path.line_to(points[0]);
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      path.quadratic_to(points[0], points[1]);
    } else if(segment.m_type == SegmentType::CUBIC) {
      path.cubic_to(points[0], points[1], points[2]);
    } else {
      path.close();
    }
  }
}

double Details::get_extent(const Path& path) {
  auto extent = 0.0;
  for(auto& segment : path.get_segments()) {
    for(auto& point : segment.m_points) {
      for(auto coordinate : {point.m_x, point.m_y}) {
        if(!std::isfinite(coordinate)) {
          return std::numeric_limits<double>::infinity();
        }
        extent = std::max(extent, std::abs(coordinate));
      }
    }
  }
  return extent;
}

Path Details::drop_unrepresentable(const Path& path) {
  auto result = Path(path.get_fill_rule());
  auto& segments = path.get_segments();
  auto start = segments.begin();
  while(start != segments.end()) {
    auto end = std::find_if(std::next(start), segments.end(),
      [] (const auto& segment) {
        return segment.m_type == SegmentType::MOVE;
      });
    if(std::all_of(start, end, is_representable)) {
      for(auto i = start; i != end; ++i) {
        append(result, *i);
      }
    }
    start = end;
  }
  return result;
}

Precision Details::get_precision(double tolerance, double extent) {
  auto digits = std::clamp(
    2 - static_cast<int>(std::floor(std::log10(tolerance))), 2,
    Clipper2Lib::CLIPPER2_MAX_DEC_PRECISION);
  if(extent * std::pow(10.0, digits) <= MAXIMUM_SCALED_COORDINATE) {
    return Precision(digits, tolerance);
  }
  auto coarse = std::max(-Clipper2Lib::CLIPPER2_MAX_DEC_PRECISION,
    static_cast<int>(
      std::floor(std::log10(MAXIMUM_SCALED_COORDINATE / extent))));
  return Precision(coarse, std::max(tolerance, std::pow(10.0, 2 - coarse)));
}

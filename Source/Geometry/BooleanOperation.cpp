#include <algorithm>
#include <cmath>
#include <clipper2/clipper.h>
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Polyline.hpp"

using namespace Rellu;

namespace {
  int get_precision(double tolerance) {
    return std::clamp(
      2 - static_cast<int>(std::floor(std::log10(tolerance))), 2,
      Clipper2Lib::CLIPPER2_MAX_DEC_PRECISION);
  }

  Clipper2Lib::FillRule to_clipper(FillRule rule) {
    if(rule == FillRule::EVEN_ODD) {
      return Clipper2Lib::FillRule::EvenOdd;
    }
    return Clipper2Lib::FillRule::NonZero;
  }

  Clipper2Lib::ClipType to_clipper(BooleanOperation operation) {
    if(operation == BooleanOperation::ADD) {
      return Clipper2Lib::ClipType::Union;
    } else if(operation == BooleanOperation::SUBTRACT) {
      return Clipper2Lib::ClipType::Difference;
    } else if(operation == BooleanOperation::INTERSECT) {
      return Clipper2Lib::ClipType::Intersection;
    }
    return Clipper2Lib::ClipType::Xor;
  }

  Clipper2Lib::PathsD to_clipper(const Path& path, double tolerance) {
    auto polygons = Clipper2Lib::PathsD();
    for(auto& polyline : flatten(path, tolerance)) {
      auto& polygon = polygons.emplace_back();
      polygon.reserve(polyline.m_points.size());
      for(auto& point : polyline.m_points) {
        polygon.emplace_back(point.m_x, point.m_y);
      }
    }
    return polygons;
  }

  Path to_path(const Clipper2Lib::PathsD& polygons) {
    auto path = Path(FillRule::NON_ZERO);
    for(auto& polygon : polygons) {
      path.move_to(Point(polygon.front().x, polygon.front().y));
      for(auto i = std::size_t(1); i != polygon.size(); ++i) {
        path.line_to(Point(polygon[i].x, polygon[i].y));
      }
      path.close();
    }
    return path;
  }

  Clipper2Lib::PathsD normalize(
      const Path& path, double tolerance, int precision) {
    return Clipper2Lib::Union(to_clipper(path, tolerance),
      to_clipper(path.get_fill_rule()), precision);
  }

  Clipper2Lib::PathsD apply(const Clipper2Lib::PathsD& region,
      const Path& operand, BooleanOperation operation, double tolerance,
      int precision) {
    return Clipper2Lib::BooleanOp(to_clipper(operation),
      to_clipper(operand.get_fill_rule()), region,
      to_clipper(operand, tolerance), precision);
  }
}

Path Rellu::combine(const Path& left, const Path& right,
    BooleanOperation operation, double tolerance) {
  auto precision = get_precision(tolerance);
  return to_path(apply(normalize(left, tolerance, precision), right,
    operation, tolerance, precision));
}

Path Rellu::combine(std::span<const Path> operands,
    BooleanOperation operation, double tolerance) {
  if(operands.empty()) {
    return Path();
  }
  auto precision = get_precision(tolerance);
  auto region = normalize(operands.front(), tolerance, precision);
  for(auto& operand : operands.subspan(1)) {
    region = apply(region, operand, operation, tolerance, precision);
  }
  return to_path(region);
}

#include <algorithm>
#include <vector>
#include <clipper2/clipper.h>
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Precision.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
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

  Clipper2Lib::PathsD normalize(const Path& path, Precision precision) {
    return Clipper2Lib::Union(to_clipper(path, precision.m_tolerance),
      to_clipper(path.get_fill_rule()), precision.m_digits);
  }

  Clipper2Lib::PathsD apply(const Clipper2Lib::PathsD& region,
      const Path& operand, BooleanOperation operation, Precision precision) {
    return Clipper2Lib::BooleanOp(to_clipper(operation),
      to_clipper(operand.get_fill_rule()), region,
      to_clipper(operand, precision.m_tolerance), precision.m_digits);
  }
}

Path Rellu::combine(const Path& left, const Path& right,
    BooleanOperation operation, double tolerance) {
  auto extent = std::max(get_extent(left), get_extent(right));
  if(!(extent <= MAXIMUM_COORDINATE)) {
    return combine(drop_unrepresentable(left), drop_unrepresentable(right),
      operation, tolerance);
  }
  auto precision = get_precision(tolerance, extent);
  return to_path(
    apply(normalize(left, precision), right, operation, precision));
}

Path Rellu::combine(std::span<const Path> operands,
    BooleanOperation operation, double tolerance) {
  if(operands.empty()) {
    return Path();
  }
  auto extent = 0.0;
  for(auto& operand : operands) {
    extent = std::max(extent, get_extent(operand));
  }
  if(!(extent <= MAXIMUM_COORDINATE)) {
    auto representable = std::vector<Path>();
    representable.reserve(operands.size());
    for(auto& operand : operands) {
      representable.push_back(drop_unrepresentable(operand));
    }
    return combine(representable, operation, tolerance);
  }
  auto precision = get_precision(tolerance, extent);
  auto region = normalize(operands.front(), precision);
  for(auto& operand : operands.subspan(1)) {
    region = apply(region, operand, operation, precision);
  }
  return to_path(region);
}

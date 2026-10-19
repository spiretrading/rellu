#include <cmath>
#include <numbers>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Tools/ShapeTool.hpp"

using namespace Rellu;

namespace {
  const char* get_prefix(ShapeKind kind) {
    if(kind == ShapeKind::RECTANGLE) {
      return "Rectangle ";
    } else if(kind == ShapeKind::ELLIPSE) {
      return "Ellipse ";
    } else if(kind == ShapeKind::POLYGON) {
      return "Polygon ";
    }
    return "Line ";
  }

  const char* get_label(ShapeKind kind) {
    if(kind == ShapeKind::RECTANGLE) {
      return "Rectangle";
    } else if(kind == ShapeKind::ELLIPSE) {
      return "Ellipse";
    } else if(kind == ShapeKind::POLYGON) {
      return "Polygon";
    }
    return "Line";
  }

  double get_sign(double value) {
    if(value < 0) {
      return -1;
    }
    return 1;
  }

  Point round_point(Point point) {
    return Point(std::round(point.m_x), std::round(point.m_y));
  }

  Point constrain_angle(Point delta) {
    auto step = std::numbers::pi / 4;
    auto index = std::round(std::atan2(delta.m_y, delta.m_x) / step);
    auto direction = Point(std::cos(index * step), std::sin(index * step));
    auto projection = dot(delta, direction);
    if(static_cast<long long>(std::abs(index)) % 2 == 0) {
      return round_point(direction * projection);
    }
    auto side = std::round(projection / std::numbers::sqrt2);
    return Point(get_sign(direction.m_x) * side,
      get_sign(direction.m_y) * side);
  }
}

ShapeTool::ShapeTool(ShapeKind kind)
  : DrawTool(get_prefix(kind)),
    m_kind(kind) {}

ShapeKind ShapeTool::get_kind() const {
  return m_kind;
}

std::unique_ptr<Command> ShapeTool::make_command(ToolContext& context,
    NodeHeader header, Point start, Point end,
    Qt::KeyboardModifiers modifiers) {
  auto& document = context.get_document();
  auto parent = find_drawing_parent(document, start);
  auto inverse = invert(get_world_transform(document, parent));
  if(!inverse) {
    return nullptr;
  }
  auto shape = Shape();
  auto origin = Point(0, 0);
  if(m_kind == ShapeKind::LINE) {
    auto center = round_point(start);
    auto delta = round_point(end) - center;
    if(modifiers.testFlag(Qt::ShiftModifier)) {
      delta = constrain_angle(delta);
    }
    if(delta == Point(0, 0)) {
      return nullptr;
    }
    auto first = center;
    if(modifiers.testFlag(Qt::AltModifier)) {
      first = center - delta;
    }
    auto last = center + delta;
    origin = Point(std::min(first.m_x, last.m_x),
      std::min(first.m_y, last.m_y));
    shape.m_geometry = LineShape(first - origin, last - origin);
    shape.m_style = make_default_line_style();
  } else {
    auto box = make_drawn_box(start, end, modifiers);
    if(!(box.m_width > 0) || !(box.m_height > 0)) {
      return nullptr;
    }
    origin = box.get_top_left();
    if(m_kind == ShapeKind::RECTANGLE) {
      shape.m_geometry =
        RectangleShape(Rect(0, 0, box.m_width, box.m_height));
    } else if(m_kind == ShapeKind::ELLIPSE) {
      shape.m_geometry = EllipseShape(Point(box.m_width / 2,
        box.m_height / 2), box.m_width / 2, box.m_height / 2);
    } else {
      shape.m_geometry = PolygonShape(
        POLYGON_CORNER_COUNT, Size(box.m_width, box.m_height));
    }
    shape.m_style = make_default_style();
  }
  auto local = inverse->map(origin);
  header.m_transform = make_translation(local.m_x, local.m_y);
  return std::make_unique<InsertNodeCommand>(
    get_label(m_kind), parent, std::move(header), std::move(shape));
}

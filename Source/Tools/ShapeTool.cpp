#include <cmath>
#include <string>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Tools/SelectionBox.hpp"
#include "Rellu/Tools/ShapeTool.hpp"

using namespace Rellu;

namespace {
  std::string get_prefix(ShapeKind kind) {
    auto name = "Line";
    if(kind == ShapeKind::RECTANGLE) {
      name = "Rectangle";
    } else if(kind == ShapeKind::ELLIPSE) {
      name = "Ellipse";
    } else if(kind == ShapeKind::POLYGON) {
      name = "Polygon";
    }
    return std::string(localize("name", name)) + ' ';
  }

  const char* get_label(ShapeKind kind) {
    if(kind == ShapeKind::RECTANGLE) {
      return "Draw Rectangle";
    } else if(kind == ShapeKind::ELLIPSE) {
      return "Draw Ellipse";
    } else if(kind == ShapeKind::POLYGON) {
      return "Draw Polygon";
    }
    return "Draw Line";
  }

  CursorKind get_crosshair(ShapeKind kind) {
    if(kind == ShapeKind::RECTANGLE) {
      return CursorKind::RECTANGLE;
    } else if(kind == ShapeKind::ELLIPSE) {
      return CursorKind::ELLIPSE;
    } else if(kind == ShapeKind::POLYGON) {
      return CursorKind::POLYGON;
    }
    return CursorKind::LINE;
  }

  bool is_half_pixel_aligned(const Style& style) {
    auto& stroke = style.m_stroke;
    return stroke && stroke->m_is_enabled &&
      stroke->m_alignment == StrokeAlignment::CENTER &&
      !(stroke->m_color == Color(0, 0, 0, 0));
  }

  Point snap_to_pixels(Point point, bool is_half_pixel) {
    if(is_half_pixel) {
      return Point(std::floor(point.m_x) + 0.5, std::floor(point.m_y) + 0.5);
    }
    return Point(std::round(point.m_x), std::round(point.m_y));
  }
}

ShapeTool::ShapeTool(ShapeKind kind)
  : DrawTool(get_prefix(kind), get_crosshair(kind)),
    m_kind(kind) {}

ShapeKind ShapeTool::get_kind() const {
  return m_kind;
}

Point ShapeTool::snap_end(
    ToolContext& context, Point end, Qt::KeyboardModifiers modifiers) {
  if(m_kind == ShapeKind::LINE) {
    return end;
  }
  return DrawTool::snap_end(context, end, modifiers);
}

std::unique_ptr<Command> ShapeTool::make_command(ToolContext& context,
    NodeHeader header, Point start, Point end,
    Qt::KeyboardModifiers modifiers) {
  auto& document = context.get_document();
  auto parent = find_parent(context, start);
  auto inverse = invert(get_world_transform(document, parent));
  if(!inverse) {
    return nullptr;
  }
  auto shape = Shape();
  auto origin = Point(0, 0);
  if(m_kind == ShapeKind::LINE) {
    auto is_constrained = modifiers.testFlag(Qt::ShiftModifier);
    auto is_mirrored = modifiers.testFlag(Qt::AltModifier);
    shape.m_style = make_default_line_style();
    auto is_half_pixel = !context.get_snap_service().is_grid_snapped() &&
      is_half_pixel_aligned(shape.m_style);
    auto center = snap_to_pixels(start, is_half_pixel);
    auto delta = end - center;
    if(is_constrained) {
      delta = constrain_drag(delta);
    }
    auto first = center;
    if(is_mirrored) {
      first = center - delta;
    }
    auto ends = context.get_snap_service().snap_line(
      first, center + delta, is_constrained, is_mirrored, modifiers);
    first = ends.m_start;
    if(is_mirrored) {
      first = snap_to_pixels(first, is_half_pixel);
    }
    auto last = snap_to_pixels(ends.m_end, is_half_pixel);
    if(first == last) {
      return nullptr;
    }
    origin = Point(std::min(first.m_x, last.m_x),
      std::min(first.m_y, last.m_y));
    shape.m_geometry = LineShape(first - origin, last - origin);
  } else {
    auto box = Rect(0, 0, 0, 0);
    if(m_kind == ShapeKind::POLYGON) {
      box = make_drawn_box(start, end, modifiers,
        get_equilateral_aspect(POLYGON_CORNER_COUNT));
    } else {
      box = make_drawn_box(start, end, modifiers);
    }
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

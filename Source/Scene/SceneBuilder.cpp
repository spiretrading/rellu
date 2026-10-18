#include <algorithm>
#include <cmath>
#include <variant>
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;

  double get_scale(const Matrix& matrix) {
    auto sum = (matrix.m_a * matrix.m_a + matrix.m_b * matrix.m_b +
      matrix.m_c * matrix.m_c + matrix.m_d * matrix.m_d) / 2;
    auto determinant = matrix.get_determinant();
    return std::sqrt(
      sum + std::sqrt(std::max(0.0, sum * sum - determinant * determinant)));
  }

  std::optional<Color> get_color(const Fill& fill) {
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      return solid->m_color;
    }
    return std::nullopt;
  }

  SolidPaint make_paint(Color color, double opacity) {
    color.m_alpha *= opacity;
    return SolidPaint(color);
  }

  bool is_drawn(const std::optional<Stroke>& stroke) {
    return stroke && stroke->m_is_enabled && stroke->m_width > 0 &&
      stroke->m_color.m_alpha > 0;
  }

  StrokeGeometry to_geometry(const Stroke& stroke) {
    return StrokeGeometry(stroke.m_width, stroke.m_alignment, stroke.m_cap,
      stroke.m_join, stroke.m_miter_limit, stroke.m_dashes, 0);
  }

  bool is_dashed(const std::vector<double>& dashes) {
    auto total = 0.0;
    for(auto dash : dashes) {
      if(!(dash >= 0) || !std::isfinite(dash)) {
        return false;
      }
      total += dash;
    }
    return total > 0;
  }

  bool is_closed(const Path& path) {
    return std::any_of(path.get_segments().begin(), path.get_segments().end(),
      [] (const auto& segment) {
        return segment.m_type == SegmentType::CLOSE;
      });
  }

  Path to_open_path(const Path& path) {
    auto result = Path(path.get_fill_rule());
    auto start = Point(0, 0);
    auto current = Point(0, 0);
    auto is_restarting = false;
    for(auto& segment : path.get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        result.move_to(points[0]);
        start = points[0];
        current = start;
        is_restarting = false;
        continue;
      } else if(segment.m_type == SegmentType::CLOSE) {
        if(current != start) {
          result.line_to(start);
        }
        current = start;
        is_restarting = true;
        continue;
      }
      if(is_restarting) {
        result.move_to(start);
        is_restarting = false;
      }
      if(segment.m_type == SegmentType::LINE) {
        result.line_to(points[0]);
        current = points[0];
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        result.quadratic_to(points[0], points[1]);
        current = points[1];
      } else {
        result.cubic_to(points[0], points[1], points[2]);
        current = points[2];
      }
    }
    return result;
  }

  Path make_outline(
      const Path& path, const StrokeGeometry& stroke, double tolerance) {
    if(!is_dashed(stroke.m_dashes) || !is_closed(path)) {
      return outline_stroke(path, stroke, tolerance);
    }
    auto dashes = to_open_path(path);
    if(stroke.m_alignment == StrokeAlignment::CENTER) {
      return outline_stroke(dashes, stroke, tolerance);
    }
    auto band = stroke;
    band.m_width *= 2;
    band.m_alignment = StrokeAlignment::CENTER;
    auto operation = BooleanOperation::INTERSECT;
    if(stroke.m_alignment == StrokeAlignment::OUTSIDE) {
      operation = BooleanOperation::SUBTRACT;
    }
    return combine(
      outline_stroke(dashes, band, tolerance), path, operation, tolerance);
  }
}

SceneBuilder::SceneBuilder(const Document& document, double tolerance)
  : m_document(&document),
    m_tolerance(tolerance),
    m_generation(0) {}

double SceneBuilder::get_tolerance() const {
  return m_tolerance;
}

void SceneBuilder::set_tolerance(double tolerance) {
  m_tolerance = tolerance;
}

DrawList SceneBuilder::build() {
  ++m_generation;
  auto list = DrawList();
  auto placement = Placement(Matrix(), 1, std::nullopt);
  for(auto& id : m_document->get_canvas()) {
    add_node(id, placement, 0, list);
  }
  std::erase_if(m_outlines, [&] (const auto& entry) {
    return entry.second.m_generation != m_generation;
  });
  return list;
}

DrawList SceneBuilder::build(const NodeId& id) {
  ++m_generation;
  auto list = DrawList();
  if(auto node = m_document->find(id)) {
    if(auto placement = get_placement(node->m_parent, 0)) {
      add_node(id, *placement, 0, list);
    }
  }
  return list;
}

std::optional<SceneBuilder::Entry> SceneBuilder::enter(
    const Node& node, const Placement& parent) {
  auto& nodes = m_document->get_nodes();
  auto source = &node;
  auto transform = node.m_header.m_transform;
  auto children = &node.m_children;
  if(node.get_reference()) {
    auto resolution = resolve_reference(nodes, node.get_id());
    if(!resolution) {
      return std::nullopt;
    }
    source = &nodes.get(resolution->m_source);
    transform = resolution->m_transform;
    children = &nodes.get(resolution->m_child_source).m_children;
  }
  auto& header = source->m_header;
  if(!header.m_is_visible || !(header.m_opacity > 0)) {
    return std::nullopt;
  }
  auto placement = Placement(parent.m_transform * transform,
    parent.m_opacity * header.m_opacity, parent.m_clip);
  if(auto artboard = source->get_artboard()) {
    auto& frame = artboard->m_frame;
    placement.m_transform =
      parent.m_transform * make_translation(frame.m_x, frame.m_y);
    auto clip = parent.m_transform.map(frame);
    if(parent.m_clip) {
      clip = intersect(*parent.m_clip, clip);
    }
    placement.m_clip = clip;
  }
  return Entry(source, children, placement);
}

std::optional<SceneBuilder::Placement> SceneBuilder::get_placement(
    const NodeId& id, int depth) {
  if(id.is_nil()) {
    return Placement(Matrix(), 1, std::nullopt);
  }
  auto node = m_document->find(id);
  if(!node || depth > MAXIMUM_DEPTH) {
    return std::nullopt;
  }
  auto parent = get_placement(node->m_parent, depth + 1);
  if(!parent) {
    return std::nullopt;
  }
  if(auto entry = enter(*node, *parent)) {
    return entry->m_placement;
  }
  return std::nullopt;
}

void SceneBuilder::add_node(const NodeId& id, const Placement& parent,
    int depth, DrawList& list) {
  if(depth > MAXIMUM_DEPTH) {
    return;
  }
  auto node = m_document->find(id);
  if(!node) {
    return;
  }
  auto entry = enter(*node, parent);
  if(!entry) {
    return;
  }
  auto& source = *entry->m_source;
  auto& placement = entry->m_placement;
  if(source.get_artboard()) {
    add_artboard(source, placement, list);
  } else if(source.get_shape()) {
    add_shape(source, placement, list);
    return;
  } else if(source.get_text()) {
    return;
  }
  for(auto& child : *entry->m_children) {
    add_node(child, placement, depth + 1, list);
  }
}

void SceneBuilder::add_artboard(
    const Node& node, const Placement& placement, DrawList& list) {
  auto& artboard = *node.get_artboard();
  if(auto color = get_color(artboard.m_background)) {
    list.push_back(FillCommand(make_rectangle(
      Rect(0, 0, artboard.m_frame.m_width, artboard.m_frame.m_height)),
      placement.m_transform, make_paint(*color, placement.m_opacity),
      placement.m_clip));
  }
}

void SceneBuilder::add_shape(
    const Node& node, const Placement& placement, DrawList& list) {
  auto& shape = *node.get_shape();
  auto& style = shape.m_style;
  auto& outline = get_outline(node, get_scale(placement.m_transform));
  if(!std::holds_alternative<LineShape>(shape.m_geometry)) {
    if(auto color = get_color(style.m_fill)) {
      list.push_back(FillCommand(outline.m_path, placement.m_transform,
        make_paint(*color, placement.m_opacity), placement.m_clip));
    }
  }
  if(is_drawn(style.m_stroke)) {
    list.push_back(FillCommand(outline.m_stroke, placement.m_transform,
      make_paint(style.m_stroke->m_color, placement.m_opacity),
      placement.m_clip));
  }
}

SceneBuilder::Outline& SceneBuilder::get_outline(
    const Node& node, double scale) {
  auto& outline = m_outlines[node.get_id()];
  auto& shape = *node.get_shape();
  if(outline.m_record.get() != &node) {
    outline.m_record = m_document->get_nodes().get_record(node.get_id());
    outline.m_path = to_path(shape.m_geometry);
    outline.m_stroke = Path();
    outline.m_tolerance = 0;
  }
  outline.m_generation = m_generation;
  auto& stroke = shape.m_style.m_stroke;
  if(!is_drawn(stroke)) {
    return outline;
  }
  auto tolerance = m_tolerance / std::max(scale, 1e-9);
  if(!(outline.m_tolerance > 0 && outline.m_tolerance <= tolerance &&
      4 * outline.m_tolerance >= tolerance)) {
    outline.m_stroke =
      make_outline(outline.m_path, to_geometry(*stroke), tolerance);
    outline.m_tolerance = tolerance;
  }
  return outline;
}

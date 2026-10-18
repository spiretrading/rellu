#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Scene/Compositing.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto TOLERANCE_MARGIN = 1e-9;

  double get_scale(const Matrix& matrix) {
    auto sum = (matrix.m_a * matrix.m_a + matrix.m_b * matrix.m_b +
      matrix.m_c * matrix.m_c + matrix.m_d * matrix.m_d) / 2;
    auto determinant = matrix.get_determinant();
    return std::sqrt(
      sum + std::sqrt(std::max(0.0, sum * sum - determinant * determinant)));
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
}

SceneBuilder::SceneBuilder(const Document& document, double tolerance)
  : m_document(&document),
    m_tolerance(tolerance),
    m_generation(0),
    m_paints(document),
    m_dependencies(nullptr) {}

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
  auto is_stale = [&] (const auto& entry) {
    return entry.second.m_generation != m_generation;
  };
  std::erase_if(m_outlines, is_stale);
  std::erase_if(m_backgrounds, is_stale);
  std::erase_if(m_masks, is_stale);
  std::erase_if(m_texts, is_stale);
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

DrawList SceneBuilder::build(
    const NodeId& id, SceneDependencies& dependencies) {
  dependencies.m_keys.clear();
  dependencies.m_minimum_tolerance = 0;
  dependencies.m_maximum_tolerance = std::numeric_limits<double>::infinity();
  m_dependencies = &dependencies;
  m_sources.clear();
  auto list = DrawList();
  try {
    list = build(id);
  } catch(...) {
    m_dependencies = nullptr;
    throw;
  }
  m_dependencies = nullptr;
  auto& keys = dependencies.m_keys;
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return list;
}

void SceneBuilder::discard(const NodeId& id) {
  m_outlines.erase(id);
  m_backgrounds.erase(id);
  m_masks.erase(id);
  m_texts.erase(id);
}

std::optional<SceneBuilder::Entry> SceneBuilder::enter(
    const Node& node, const Placement& parent) {
  auto& nodes = m_document->get_nodes();
  auto source = &node;
  auto transform = node.m_header.m_transform;
  auto children = &node.m_children;
  if(node.get_reference()) {
    add_dependencies(node);
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
  auto placement =
    Placement(parent.m_transform * transform, parent.m_opacity, parent.m_clip);
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
  auto placement = entry->m_placement;
  auto first = list.size();
  auto mask = std::optional<LayerMask>();
  if(auto group = source.get_group(); group && group->m_mask) {
    mask = get_mask(source, placement.m_transform, placement.m_clip);
  }
  if(source.get_artboard()) {
    add_artboard(source, placement, list);
  } else if(source.get_shape()) {
    add_shape(source, placement, list);
  } else if(source.get_text()) {
    add_text(source, placement, list);
  }
  if(!source.get_shape() && !source.get_text()) {
    for(auto& child : *entry->m_children) {
      add_node(child, placement, depth + 1, list);
    }
  }
  add_composite(list, first, source.m_header, std::move(mask),
    entry->m_placement.m_clip);
}

void SceneBuilder::add_dependencies(const Node& reference) {
  if(!m_dependencies) {
    return;
  }
  auto& nodes = m_document->get_nodes();
  auto& keys = m_dependencies->m_keys;
  auto current = &reference;
  for(auto hop = 0; hop != MAXIMUM_DEPTH; ++hop) {
    auto payload = current->get_reference();
    if(!payload) {
      return;
    }
    if(!m_sources.insert(payload->m_source).second) {
      return;
    }
    auto next = nodes.find(payload->m_source);
    if(!next) {
      keys.push_back(payload->m_source);
      return;
    }
    auto root = next;
    for(auto depth = 0; depth != MAXIMUM_DEPTH && !root->m_parent.is_nil();
        ++depth) {
      auto parent = nodes.find(root->m_parent);
      if(!parent) {
        break;
      }
      root = parent;
    }
    keys.push_back(root->get_id());
    current = next;
  }
}

void SceneBuilder::add_tolerance(double minimum, double maximum) {
  if(m_dependencies) {
    m_dependencies->m_minimum_tolerance =
      std::max(m_dependencies->m_minimum_tolerance, minimum);
    m_dependencies->m_maximum_tolerance =
      std::min(m_dependencies->m_maximum_tolerance, maximum);
  }
}

std::optional<Paint> SceneBuilder::convert(
    const Fill& fill, const Path& outline) {
  if(m_dependencies) {
    if(auto image = std::get_if<ImageFill>(&fill)) {
      m_dependencies->m_keys.push_back(image->m_image);
    }
  }
  return m_paints.convert(fill, outline);
}

std::optional<LayerMask> SceneBuilder::get_mask(const Node& node,
    const Matrix& transform, std::optional<Rect>& clip) {
  auto& mask = *node.get_group()->m_mask;
  auto& outline = m_masks[node.get_id()];
  if(outline.m_record.get() != &node || outline.m_transform != transform ||
      outline.m_tolerance != m_tolerance) {
    outline.m_record = m_document->get_nodes().get_record(node.get_id());
    outline.m_transform = transform;
    outline.m_tolerance = m_tolerance;
    outline.m_clip = std::nullopt;
    outline.m_mask = make_mask(mask, transform, m_tolerance, outline.m_clip);
  }
  outline.m_generation = m_generation;
  if(outline.m_mask && mask.m_shapes.size() > 1) {
    add_tolerance(m_tolerance, m_tolerance);
  }
  if(outline.m_clip) {
    if(clip) {
      clip = intersect(*clip, *outline.m_clip);
    } else {
      clip = outline.m_clip;
    }
  }
  return outline.m_mask;
}

void SceneBuilder::add_artboard(
    const Node& node, const Placement& placement, DrawList& list) {
  auto& artboard = *node.get_artboard();
  auto& background = m_backgrounds[node.get_id()];
  if(background.m_record.get() != &node) {
    background.m_record = m_document->get_nodes().get_record(node.get_id());
    background.m_path = std::make_shared<const Path>(make_rectangle(
      Rect(0, 0, artboard.m_frame.m_width, artboard.m_frame.m_height)));
  }
  background.m_generation = m_generation;
  if(auto paint = convert(artboard.m_background, *background.m_path)) {
    list.push_back(FillCommand(background.m_path, placement.m_transform,
      std::move(*paint), placement.m_clip));
  }
}

void SceneBuilder::add_shape(
    const Node& node, const Placement& placement, DrawList& list) {
  auto& shape = *node.get_shape();
  auto& style = shape.m_style;
  auto& outline = get_outline(node, get_scale(placement.m_transform));
  auto fill = std::optional<FillCommand>();
  if(!std::holds_alternative<LineShape>(shape.m_geometry)) {
    if(auto paint = convert(style.m_fill, *outline.m_path)) {
      fill.emplace(outline.m_path, placement.m_transform, std::move(*paint),
        placement.m_clip);
    }
  }
  auto stroke = std::optional<FillCommand>();
  if(is_drawn(style.m_stroke)) {
    stroke.emplace(outline.m_stroke, placement.m_transform,
      make_paint(style.m_stroke->m_color, placement.m_opacity),
      placement.m_clip);
  }
  add_styled(list, std::move(fill), std::move(stroke), style.m_effects);
}

SceneBuilder::Outline& SceneBuilder::get_outline(
    const Node& node, double scale) {
  auto& outline = m_outlines[node.get_id()];
  auto& shape = *node.get_shape();
  if(outline.m_record.get() != &node) {
    outline.m_record = m_document->get_nodes().get_record(node.get_id());
    outline.m_path = std::make_shared<const Path>(to_path(shape.m_geometry));
    outline.m_stroke = nullptr;
    outline.m_tolerance = 0;
  }
  outline.m_generation = m_generation;
  auto& stroke = shape.m_style.m_stroke;
  if(!is_drawn(stroke)) {
    return outline;
  }
  auto divisor = std::max(scale, 1e-9);
  auto tolerance = m_tolerance / divisor;
  if(!(outline.m_tolerance > 0 && outline.m_tolerance <= tolerance &&
      4 * outline.m_tolerance >= tolerance)) {
    outline.m_stroke = std::make_shared<const Path>(
      outline_stroke(*outline.m_path, to_geometry(*stroke), tolerance));
    outline.m_tolerance = tolerance;
  }
  add_tolerance(outline.m_tolerance * divisor * (1 + TOLERANCE_MARGIN),
    4 * outline.m_tolerance * divisor * (1 - TOLERANCE_MARGIN));
  return outline;
}

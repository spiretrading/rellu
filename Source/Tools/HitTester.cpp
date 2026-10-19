#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <variant>
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Tools/HitTester.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto MINIMUM_FLATNESS = 1e-3;
  constexpr auto VIEWPORT_EXTENT = 1e9;

  double get_scale(const Matrix& matrix) {
    auto sum = (matrix.m_a * matrix.m_a + matrix.m_b * matrix.m_b +
      matrix.m_c * matrix.m_c + matrix.m_d * matrix.m_d) / 2;
    auto determinant = matrix.get_determinant();
    return std::sqrt(
      sum + std::sqrt(std::max(0.0, sum * sum - determinant * determinant)));
  }

  Rect make_bounds(Point first, Point second) {
    auto left = std::min(first.m_x, second.m_x);
    auto top = std::min(first.m_y, second.m_y);
    return Rect(left, top, std::max(first.m_x, second.m_x) - left,
      std::max(first.m_y, second.m_y) - top);
  }

  Rect join(const std::optional<Rect>& left, const Rect& right) {
    if(!left) {
      return right;
    }
    auto x = std::min(left->get_left(), right.get_left());
    auto y = std::min(left->get_top(), right.get_top());
    return Rect(x, y, std::max(left->get_right(), right.get_right()) - x,
      std::max(left->get_bottom(), right.get_bottom()) - y);
  }

  bool is_inside(const Rect& rect, Point point) {
    return point.m_x >= rect.get_left() && point.m_x <= rect.get_right() &&
      point.m_y >= rect.get_top() && point.m_y <= rect.get_bottom();
  }

  bool is_open(const ShapeGeometry& geometry) {
    if(std::holds_alternative<LineShape>(geometry)) {
      return true;
    } else if(auto path = std::get_if<PathShape>(&geometry)) {
      auto& segments = path->m_path->get_segments();
      return std::none_of(segments.begin(), segments.end(),
        [] (const auto& segment) {
          return segment.m_type == SegmentType::CLOSE;
        });
    }
    return false;
  }

  double get_stroke_width(const std::optional<Stroke>& stroke) {
    if(!stroke || !stroke->m_is_enabled || !(stroke->m_width > 0)) {
      return 0;
    }
    return stroke->m_width;
  }

  double get_stroke_extent(const std::optional<Stroke>& stroke, bool is_open) {
    auto width = get_stroke_width(stroke);
    if(width == 0) {
      return 0;
    } else if(is_open || stroke->m_alignment == StrokeAlignment::CENTER) {
      return width / 2;
    } else if(stroke->m_alignment == StrokeAlignment::OUTSIDE) {
      return width;
    }
    return 0;
  }

  Rect get_geometry_bounds(const ShapeGeometry& geometry) {
    if(auto rectangle = std::get_if<RectangleShape>(&geometry)) {
      return make_bounds(rectangle->m_bounds.get_top_left(),
        rectangle->m_bounds.get_bottom_right());
    } else if(auto ellipse = std::get_if<EllipseShape>(&geometry)) {
      return make_bounds(ellipse->m_center -
        Point(ellipse->m_x_radius, ellipse->m_y_radius), ellipse->m_center +
        Point(ellipse->m_x_radius, ellipse->m_y_radius));
    } else if(auto line = std::get_if<LineShape>(&geometry)) {
      return make_bounds(line->m_start, line->m_end);
    } else if(auto path = std::get_if<PathShape>(&geometry)) {
      return get_bounds(*path->m_path);
    } else if(auto compound = std::get_if<CompoundShape>(&geometry)) {
      return get_bounds(*compound->m_path);
    }
    return get_bounds(to_path(geometry));
  }

  double get_segment_distance(Point point, Point start, Point end) {
    auto direction = end - start;
    auto length = dot(direction, direction);
    if(length == 0) {
      return distance(point, start);
    }
    auto t = std::clamp(dot(point - start, direction) / length, 0.0, 1.0);
    return distance(point, start + t * direction);
  }

  double get_outline_distance(
      const Path& path, Point point, double flatness) {
    auto result = std::numeric_limits<double>::infinity();
    for(auto& polyline : flatten(path, std::max(flatness, MINIMUM_FLATNESS))) {
      auto& points = polyline.m_points;
      if(points.size() == 1) {
        result = std::min(result, distance(point, points.front()));
        continue;
      }
      for(auto i = std::size_t(1); i < points.size(); ++i) {
        result = std::min(
          result, get_segment_distance(point, points[i - 1], points[i]));
      }
      if(polyline.m_is_closed && points.size() > 2) {
        result = std::min(result,
          get_segment_distance(point, points.back(), points.front()));
      }
    }
    return result;
  }

  bool hit_shape(const Shape& shape, Point point, double tolerance) {
    auto& geometry = shape.m_geometry;
    auto& style = shape.m_style;
    auto generated = std::optional<Path>();
    auto outline = static_cast<const Path*>(nullptr);
    auto rule = FillRule::NON_ZERO;
    if(auto path = std::get_if<PathShape>(&geometry)) {
      outline = &*path->m_path;
      rule = path->m_fill_rule;
    } else if(auto compound = std::get_if<CompoundShape>(&geometry)) {
      outline = &*compound->m_path;
      rule = compound->m_path->get_fill_rule();
    } else {
      generated = to_path(geometry);
      outline = &*generated;
      rule = generated->get_fill_rule();
    }
    auto open = is_open(geometry);
    auto is_filled = (!std::holds_alternative<NoFill>(style.m_fill) ||
      shape.m_video) && !std::holds_alternative<LineShape>(geometry);
    auto is_inside = std::optional<bool>();
    auto test_inside = [&] {
      if(!is_inside) {
        is_inside = contains(*outline, point, rule);
      }
      return *is_inside;
    };
    if(is_filled && test_inside()) {
      return true;
    }
    auto width = get_stroke_width(style.m_stroke);
    auto reach = tolerance;
    if(width > 0) {
      if(open || style.m_stroke->m_alignment == StrokeAlignment::CENTER) {
        reach += width / 2;
      } else if((style.m_stroke->m_alignment == StrokeAlignment::INSIDE) ==
          test_inside()) {
        reach += width;
      }
    }
    return get_outline_distance(*outline, point, tolerance / 4) <= reach;
  }

  bool hit_mask(const Mask& mask, Point point) {
    for(auto& shape : mask.m_shapes) {
      auto inverse = invert(shape.m_transform);
      if(!inverse) {
        continue;
      }
      auto path = to_path(shape.m_geometry);
      if(contains(path, inverse->map(point))) {
        return true;
      }
    }
    return false;
  }

  std::optional<Rect> get_viewport(const Group& group) {
    auto viewport = std::optional<Rect>();
    if(auto& scroll = group.m_scroll_group) {
      viewport.emplace(-VIEWPORT_EXTENT, -VIEWPORT_EXTENT,
        2 * VIEWPORT_EXTENT, 2 * VIEWPORT_EXTENT);
      if(scroll->m_direction != ScrollDirection::VERTICAL) {
        viewport->m_x = scroll->m_offset.m_x;
        viewport->m_width = scroll->m_viewport.m_width;
      }
      if(scroll->m_direction != ScrollDirection::HORIZONTAL) {
        viewport->m_y = scroll->m_offset.m_y;
        viewport->m_height = scroll->m_viewport.m_height;
      }
    }
    if(auto& grid = group.m_repeat_grid) {
      auto size = Rect(0, 0, grid->m_size.m_width, grid->m_size.m_height);
      if(viewport) {
        viewport = intersect(*viewport, size);
      } else {
        viewport = size;
      }
    }
    return viewport;
  }

  std::array<Point, 4> get_corners(const Rect& rect, const Matrix& matrix) {
    return std::array<Point, 4>({matrix.map(rect.get_top_left()),
      matrix.map(Point(rect.get_right(), rect.get_top())),
      matrix.map(rect.get_bottom_right()),
      matrix.map(Point(rect.get_left(), rect.get_bottom()))});
  }

  bool is_separated(
      const std::array<Point, 4>& quad, const Rect& rect, Point axis) {
    auto low = std::numeric_limits<double>::infinity();
    auto high = -low;
    for(auto& corner : quad) {
      auto projection = dot(corner, axis);
      low = std::min(low, projection);
      high = std::max(high, projection);
    }
    auto rect_low = std::numeric_limits<double>::infinity();
    auto rect_high = -rect_low;
    for(auto& corner : {rect.get_top_left(),
        Point(rect.get_right(), rect.get_top()), rect.get_bottom_right(),
        Point(rect.get_left(), rect.get_bottom())}) {
      auto projection = dot(corner, axis);
      rect_low = std::min(rect_low, projection);
      rect_high = std::max(rect_high, projection);
    }
    return high < rect_low || low > rect_high;
  }

  bool intersects(const std::array<Point, 4>& quad, const Rect& rect) {
    if(is_separated(quad, rect, Point(1, 0)) ||
        is_separated(quad, rect, Point(0, 1))) {
      return false;
    }
    for(auto i = std::size_t(0); i != quad.size(); ++i) {
      auto edge = quad[(i + 1) % quad.size()] - quad[i];
      if(edge == Point(0, 0)) {
        continue;
      }
      if(is_separated(quad, rect, Point(-edge.m_y, edge.m_x))) {
        return false;
      }
    }
    return true;
  }
}

HitTester::HitTester(Document& document)
  : HitTester(document, estimate_text_bounds) {}

HitTester::HitTester(Document& document, TextMeasurer measurer)
    : m_document(&document),
      m_measurer(std::move(measurer)),
      m_next_root(0) {
  m_subscription = document.subscribe([this] (const ChangeSet& changes) {
    on_change(changes);
  });
}

Document& HitTester::get_document() const {
  return *m_document;
}

NodeId HitTester::hit(
    Point point, double tolerance, bool is_locked_hit) const {
  return hit_children(m_document->get_canvas(), point, Matrix(), Matrix(),
    tolerance, is_locked_hit, 0);
}

NodeId HitTester::hit_within(const NodeId& id, Point point,
    double tolerance, bool is_locked_hit) const {
  auto node = m_document->find(id);
  if(!node) {
    return NodeId();
  }
  return hit(id, point, get_world_transform(*m_document, node->m_parent),
    tolerance, is_locked_hit, 0);
}

std::vector<NodeId> HitTester::query(
    const NodeId& container, const Rect& region, bool is_enclosed) const {
  auto result = std::vector<NodeId>();
  auto world = Matrix();
  auto children = &m_document->get_canvas();
  if(!container.is_nil()) {
    auto node = m_document->find(container);
    if(!node) {
      return result;
    }
    auto placement = place(*node);
    if(!placement) {
      return result;
    }
    world = get_world_transform(*m_document, container);
    children = placement->m_children;
  }
  for(auto& child : *children) {
    auto node = m_document->find(child);
    if(!node) {
      continue;
    }
    auto& extent = measure(child, 0);
    if(!extent.m_local) {
      continue;
    }
    auto placement = place(*node);
    auto quad = get_corners(*extent.m_local, world * placement->m_transform);
    if(is_enclosed) {
      if(std::all_of(quad.begin(), quad.end(), [&] (const auto& corner) {
          return is_inside(region, corner);
        })) {
        result.push_back(child);
      }
    } else if(intersects(quad, region)) {
      result.push_back(child);
    }
  }
  return result;
}

std::optional<Rect> HitTester::get_extent(const NodeId& id) const {
  return measure(id, 0).m_local;
}

bool HitTester::prepare(std::chrono::nanoseconds budget) {
  constexpr auto CHECK_INTERVAL = 256;
  auto start = std::chrono::steady_clock::now();
  auto& canvas = m_document->get_canvas();
  auto count = 0;
  while(true) {
    if(m_pending.empty()) {
      if(m_next_root >= canvas.size()) {
        return true;
      }
      m_pending.emplace_back(canvas[m_next_root], 0);
      ++m_next_root;
    }
    auto& [id, index] = m_pending.back();
    auto node = m_document->find(id);
    if(node && index < node->m_children.size() &&
        m_pending.size() <= MAXIMUM_DEPTH) {
      auto child = node->m_children[index];
      ++index;
      m_pending.emplace_back(child, 0);
      continue;
    }
    auto depth = static_cast<int>(m_pending.size()) - 1;
    measure(id, depth);
    m_pending.pop_back();
    if(++count == CHECK_INTERVAL) {
      count = 0;
      if(std::chrono::steady_clock::now() - start >= budget) {
        return false;
      }
    }
  }
}

void HitTester::clear() {
  m_extents.clear();
}

std::optional<HitTester::Placement> HitTester::place(const Node& node) const {
  if(node.get_reference()) {
    auto& nodes = m_document->get_nodes();
    auto resolution = resolve_reference(nodes, node.get_id());
    if(!resolution) {
      return std::nullopt;
    }
    return Placement(&nodes.get(resolution->m_source),
      &nodes.get(resolution->m_child_source).m_children,
      resolution->m_transform, resolution->m_child_source == node.get_id());
  } else if(auto artboard = node.get_artboard()) {
    return Placement(&node, &node.m_children,
      make_translation(artboard->m_frame.m_x, artboard->m_frame.m_y), true);
  }
  return Placement(&node, &node.m_children, node.m_header.m_transform, true);
}

const HitTester::Extent& HitTester::measure(
    const NodeId& id, int depth) const {
  if(auto entry = m_extents.find(id); entry != m_extents.end()) {
    return entry->second;
  }
  auto extent = Extent();
  auto node = depth <= MAXIMUM_DEPTH ? m_document->find(id) : nullptr;
  auto placement = std::optional<Placement>();
  if(node) {
    placement = place(*node);
  }
  if(placement && placement->m_source->m_header.m_is_visible) {
    auto& source = *placement->m_source;
    if(auto artboard = source.get_artboard()) {
      extent.m_local =
        Rect(0, 0, artboard->m_frame.m_width, artboard->m_frame.m_height);
    } else if(auto shape = source.get_shape()) {
      extent.m_local = inflate(get_geometry_bounds(shape->m_geometry),
        get_stroke_extent(shape->m_style.m_stroke,
          is_open(shape->m_geometry)));
    } else if(auto text = source.get_text()) {
      extent.m_local = m_measurer(*text);
    } else if(auto group = source.get_group()) {
      if(group->m_mask && !group->m_mask->m_shapes.empty()) {
        for(auto& shape : group->m_mask->m_shapes) {
          extent.m_local = join(extent.m_local,
            get_bounds(to_path(shape.m_geometry), shape.m_transform));
        }
      } else {
        for(auto& child : *placement->m_children) {
          if(auto& parent = measure(child, depth + 1).m_parent) {
            extent.m_local = join(extent.m_local, *parent);
          }
        }
      }
      if(auto viewport = get_viewport(*group); viewport && extent.m_local) {
        if(viewport->intersects(*extent.m_local)) {
          extent.m_local = intersect(*viewport, *extent.m_local);
        } else {
          extent.m_local = std::nullopt;
        }
      }
    }
    if(extent.m_local) {
      extent.m_parent = placement->m_transform.map(*extent.m_local);
    }
  }
  return m_extents.emplace(id, extent).first->second;
}

NodeId HitTester::hit(const NodeId& id, Point point, const Matrix& parent,
    double tolerance, bool is_locked_hit, int depth) const {
  if(depth > MAXIMUM_DEPTH) {
    return NodeId();
  }
  auto node = m_document->find(id);
  if(!node) {
    return NodeId();
  }
  auto placement = place(*node);
  if(!placement ||
      (placement->m_source->m_header.m_is_locked && !is_locked_hit)) {
    return NodeId();
  }
  auto& extent = measure(id, depth);
  if(!extent.m_local) {
    return NodeId();
  }
  auto world = parent * placement->m_transform;
  auto inverse = invert(world);
  if(!inverse) {
    return NodeId();
  }
  auto local = inverse->map(point);
  auto local_tolerance = tolerance * get_scale(*inverse);
  if(!is_inside(inflate(*extent.m_local, local_tolerance), local)) {
    return NodeId();
  }
  auto& source = *placement->m_source;
  if(source.get_artboard()) {
    if(!is_inside(*extent.m_local, local)) {
      return NodeId();
    }
    auto child = hit_children(*placement->m_children, point, world, *inverse,
      tolerance, is_locked_hit, depth + 1);
    if(!child.is_nil()) {
      return child;
    }
    return id;
  } else if(auto shape = source.get_shape()) {
    if(hit_shape(*shape, local, local_tolerance)) {
      return id;
    }
    return NodeId();
  } else if(auto text = source.get_text()) {
    if(is_inside(inflate(m_measurer(*text), local_tolerance), local)) {
      return id;
    }
    return NodeId();
  } else if(auto group = source.get_group()) {
    if(group->m_mask && !group->m_mask->m_shapes.empty() &&
        !hit_mask(*group->m_mask, local)) {
      return NodeId();
    }
    if(auto viewport = get_viewport(*group);
        viewport && !is_inside(*viewport, local)) {
      return NodeId();
    }
    auto child = hit_children(*placement->m_children, point, world, *inverse,
      tolerance, is_locked_hit, depth + 1);
    if(child.is_nil() || placement->m_owns_children) {
      return child;
    }
    return id;
  }
  return NodeId();
}

NodeId HitTester::hit_children(const std::vector<NodeId>& children,
    Point point, const Matrix& world, const Matrix& inverse, double tolerance,
    bool is_locked_hit, int depth) const {
  auto local = inverse.map(point);
  auto local_tolerance = tolerance * get_scale(inverse);
  for(auto i = children.rbegin(); i != children.rend(); ++i) {
    auto& extent = measure(*i, depth);
    if(!extent.m_parent ||
        !is_inside(inflate(*extent.m_parent, local_tolerance), local)) {
      continue;
    }
    auto result = hit(*i, point, world, tolerance, is_locked_hit, depth);
    if(!result.is_nil()) {
      return result;
    }
  }
  return NodeId();
}

void HitTester::on_change(const ChangeSet& changes) {
  if(!changes.m_components.empty()) {
    m_extents.clear();
    return;
  }
  for(auto& id : changes.m_removed) {
    m_extents.erase(id);
  }
  auto visited = std::unordered_set<NodeId>();
  auto is_cleared = false;
  auto invalidate = [&] (const NodeId& id) {
    auto current = id;
    for(auto depth = 0; depth <= MAXIMUM_DEPTH; ++depth) {
      if(!visited.insert(current).second) {
        return;
      }
      m_extents.erase(current);
      auto node = m_document->find(current);
      if(!node) {
        return;
      }
      if(node->m_parent.is_nil()) {
        if(!m_document->is_on_canvas(current)) {
          is_cleared = true;
        }
        return;
      }
      current = node->m_parent;
    }
  };
  for(auto& id : changes.m_inserted) {
    invalidate(id);
  }
  for(auto& id : changes.m_moved) {
    invalidate(id);
  }
  for(auto& change : changes.m_modified) {
    invalidate(change.m_id);
  }
  if(is_cleared) {
    m_extents.clear();
  }
}

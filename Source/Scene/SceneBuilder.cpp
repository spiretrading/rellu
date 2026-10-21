#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include "Rellu/Geometry/BooleanOperation.hpp"
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
  constexpr auto SEGMENT_SIZE = std::size_t(256);
  constexpr auto POPULATION = 2 * SEGMENT_SIZE;
  constexpr auto INSIDE_FILL_INSET = 0.5625;

  SceneDependencies make_dependencies() {
    return SceneDependencies(
      std::vector<NodeId>(), 0, std::numeric_limits<double>::infinity());
  }

  void normalize(SceneDependencies& dependencies) {
    auto& keys = dependencies.m_keys;
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  }

  void merge(SceneDependencies& target, const SceneDependencies& source) {
    target.m_keys.insert(
      target.m_keys.end(), source.m_keys.begin(), source.m_keys.end());
    target.m_minimum_tolerance =
      std::max(target.m_minimum_tolerance, source.m_minimum_tolerance);
    target.m_maximum_tolerance =
      std::min(target.m_maximum_tolerance, source.m_maximum_tolerance);
    if(source.m_is_device_dependent) {
      if(target.m_is_device_dependent &&
          target.m_device_transform != source.m_device_transform) {
        target.m_is_device_mixed = true;
      }
      target.m_is_device_dependent = true;
      target.m_device_transform = source.m_device_transform;
    }
    target.m_is_device_mixed |= source.m_is_device_mixed;
    target.m_is_rasterized |= source.m_is_rasterized;
  }

  bool is_reusable(const SceneDependencies& dependencies, double built,
      double tolerance, const std::optional<Matrix>& device,
      bool is_lenient) {
    if(!is_lenient && dependencies.m_is_device_dependent &&
        (dependencies.m_is_device_mixed ||
          dependencies.m_device_transform != device)) {
      return false;
    }
    return tolerance == built ||
      (tolerance >= dependencies.m_minimum_tolerance &&
        tolerance <= dependencies.m_maximum_tolerance);
  }

  double get_scale(const Matrix& matrix) {
    auto sum = (matrix.m_a * matrix.m_a + matrix.m_b * matrix.m_b +
      matrix.m_c * matrix.m_c + matrix.m_d * matrix.m_d) / 2;
    auto determinant = matrix.get_determinant();
    return std::sqrt(
      sum + std::sqrt(std::max(0.0, sum * sum - determinant * determinant)));
  }

  double get_scale(
      const Matrix& matrix, const std::optional<Projection>& projection) {
    if(!projection) {
      return get_scale(matrix);
    }
    return get_scale(
      linearize(*projection, matrix.map(Point(0, 0))) * matrix);
  }

  Point map_vector(const Matrix& matrix, Point vector) {
    return Point(matrix.m_a * vector.m_x + matrix.m_c * vector.m_y,
      matrix.m_b * vector.m_x + matrix.m_d * vector.m_y);
  }

  std::shared_ptr<const Path> project(const Path& path,
      const Matrix& transform, const Projection& projection) {
    return std::make_shared<const Path>(
      Rellu::transform(path, projection * transform));
  }

  void project(Shadow& shadow, const Matrix& jacobian, double scale) {
    shadow.m_offset = map_vector(jacobian, shadow.m_offset);
    shadow.m_radius *= scale;
  }

  void project(
      DrawList& list, std::size_t first, const Projection& projection) {
    auto bounds = std::optional<Rect>();
    for(auto i = first; i != list.size(); ++i) {
      if(auto fill = std::get_if<FillCommand>(&list[i])) {
        auto region = get_bounds(*fill->m_path, fill->m_transform);
        if(bounds) {
          bounds = unite(*bounds, region);
        } else {
          bounds = region;
        }
      }
    }
    auto center = bounds ? bounds->get_center() : Point(0, 0);
    auto jacobian = linearize(projection, center);
    auto scale = std::sqrt(std::abs(jacobian.get_determinant()));
    for(auto i = first; i != list.size(); ++i) {
      if(auto fill = std::get_if<FillCommand>(&list[i])) {
        auto mapping = projection * fill->m_transform;
        fill->m_path = project(*fill->m_path, fill->m_transform, projection);
        if(auto gradient = std::get_if<GradientPaint>(&fill->m_paint)) {
          gradient->m_projection = mapping;
        } else if(auto image = std::get_if<ImagePaint>(&fill->m_paint)) {
          image->m_projection = mapping;
        }
        fill->m_transform = Matrix();
        fill->m_stroke = std::nullopt;
      } else if(auto layer = std::get_if<BeginLayerCommand>(&list[i])) {
        if(auto& mask = layer->m_mask) {
          mask = LayerMask(
            project(*mask->m_path, mask->m_transform, projection), Matrix());
        }
        for(auto& shadow : layer->m_inner_shadows) {
          project(shadow, jacobian, scale);
        }
        for(auto& shadow : layer->m_drop_shadows) {
          project(shadow, jacobian, scale);
        }
        layer->m_blur *= scale;
      } else if(auto blur = std::get_if<BackdropBlurCommand>(&list[i])) {
        blur->m_path = project(*blur->m_path, blur->m_transform, projection);
        blur->m_transform = Matrix();
        blur->m_radius *= scale;
      }
    }
  }

  SolidPaint make_paint(Color color, double opacity) {
    color.m_alpha *= opacity;
    return SolidPaint(color);
  }

  bool is_drawn(const std::optional<Stroke>& stroke) {
    return stroke && stroke->m_is_enabled && stroke->m_width > 0 &&
      stroke->m_color.m_alpha > 0;
  }

  bool is_opaque(const Fill& fill) {
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      return solid->m_color.m_alpha >= 1;
    } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
      return !gradient->m_stops.empty() && std::all_of(
        gradient->m_stops.begin(), gradient->m_stops.end(),
        [] (const auto& stop) {
          return stop.m_color.m_alpha >= 1;
        });
    }
    return false;
  }

  bool is_closed(const Path& path) {
    auto is_open = false;
    for(auto& segment : path.get_segments()) {
      if(segment.m_type == SegmentType::MOVE) {
        if(is_open) {
          return false;
        }
        is_open = true;
      } else if(segment.m_type == SegmentType::CLOSE) {
        is_open = false;
      }
    }
    return !is_open;
  }

  bool is_open(const Path& path) {
    return std::none_of(path.get_segments().begin(), path.get_segments().end(),
      [] (const auto& segment) {
        return segment.m_type == SegmentType::CLOSE;
      });
  }

  bool is_inward(const std::vector<Effect>& effects) {
    return std::any_of(effects.begin(), effects.end(), [] (const auto& effect) {
      if(auto shadow = std::get_if<InnerShadow>(&effect)) {
        return shadow->m_is_visible;
      } else if(auto blur = std::get_if<Blur>(&effect)) {
        return blur->m_is_visible && blur->m_is_background;
      }
      return false;
    });
  }

  bool is_axis_aligned(const Matrix& matrix) {
    return matrix.m_b == 0 && matrix.m_c == 0;
  }

  void clip_to(std::optional<Rect>& clip, const Rect& rectangle) {
    if(clip) {
      clip = intersect(*clip, rectangle);
    } else {
      clip = rectangle;
    }
  }

  std::optional<Rect> get_viewport(const Group& group) {
    constexpr auto EXTENT = 1e9;
    auto viewport = std::optional<Rect>();
    if(auto& scroll = group.m_scroll_group) {
      viewport.emplace(-EXTENT, -EXTENT, 2 * EXTENT, 2 * EXTENT);
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
      clip_to(viewport,
        Rect(0, 0, grid->m_size.m_width, grid->m_size.m_height));
    }
    return viewport;
  }

  std::optional<Path> make_dash_path(
      const ShapeGeometry& geometry, StrokeAlignment alignment) {
    if(auto ellipse = std::get_if<EllipseShape>(&geometry)) {
      auto x = Point(ellipse->m_x_radius, 0);
      auto y = Point(0, ellipse->m_y_radius);
      auto& center = ellipse->m_center;
      auto path = Path();
      path.move_to(center - y);
      path.arc_to(center + x - y, center + x);
      path.arc_to(center + x + y, center + y);
      path.arc_to(center - x + y, center - x);
      path.arc_to(center - x - y, center - y);
      path.close();
      return path;
    } else if(auto polygon = std::get_if<PolygonShape>(&geometry);
        polygon && alignment != StrokeAlignment::CENTER) {
      auto vertices = polygon->m_vertices;
      if(vertices.empty()) {
        vertices = make_polygon_vertices(polygon->m_corner_count,
          Rect(0, 0, polygon->m_size.m_width, polygon->m_size.m_height),
          polygon->m_star_ratio);
      }
      if(vertices.empty()) {
        return std::nullopt;
      }
      std::rotate(vertices.rbegin(), vertices.rbegin() + 1, vertices.rend());
      return make_polygon(vertices, polygon->m_corner_radius);
    }
    return std::nullopt;
  }

  StrokeGeometry to_geometry(const Stroke& stroke) {
    return StrokeGeometry(stroke.m_width, stroke.m_alignment, stroke.m_cap,
      stroke.m_join, stroke.m_miter_limit, stroke.m_dashes, 0);
  }

  std::optional<Path> offset(const ShapeGeometry& geometry, double distance) {
    if(auto ellipse = std::get_if<EllipseShape>(&geometry)) {
      auto x_radius = ellipse->m_x_radius - distance;
      auto y_radius = ellipse->m_y_radius - distance;
      if(!(x_radius > 0) || !(y_radius > 0)) {
        return std::nullopt;
      }
      return make_ellipse(ellipse->m_center, x_radius, y_radius);
    } else if(auto rectangle = std::get_if<RectangleShape>(&geometry)) {
      auto& bounds = rectangle->m_bounds;
      auto width = bounds.m_width - 2 * distance;
      auto height = bounds.m_height - 2 * distance;
      if(!(width > 0) || !(height > 0)) {
        return std::nullopt;
      }
      auto radii = clamp_corner_radii(rectangle->m_radii, bounds.get_size());
      for(auto radius : {radii.m_top_left, radii.m_top_right,
          radii.m_bottom_right, radii.m_bottom_left}) {
        if(radius > 0 && !(radius > distance)) {
          return std::nullopt;
        }
      }
      auto shrink = [&] (double radius) {
        if(radius > 0) {
          return std::max(0.0, radius - distance);
        }
        return 0.0;
      };
      return make_rectangle(
        Rect(bounds.m_x + distance, bounds.m_y + distance, width, height),
        CornerRadii(shrink(radii.m_top_left), shrink(radii.m_top_right),
          shrink(radii.m_bottom_right), shrink(radii.m_bottom_left)));
    }
    return std::nullopt;
  }

  bool is_rounded(const ShapeGeometry& geometry) {
    auto rectangle = std::get_if<RectangleShape>(&geometry);
    if(!rectangle) {
      return false;
    }
    auto& radii = rectangle->m_radii;
    return radii.m_top_left > 0 || radii.m_top_right > 0 ||
      radii.m_bottom_right > 0 || radii.m_bottom_left > 0;
  }

  std::optional<Path> shrink_path(
      const ShapeGeometry& geometry, double distance) {
    auto rectangle = std::get_if<RectangleShape>(&geometry);
    if(!rectangle) {
      return std::nullopt;
    }
    auto& bounds = rectangle->m_bounds;
    auto width = bounds.m_width - 2 * distance;
    auto height = bounds.m_height - 2 * distance;
    if(!(width > 0) || !(height > 0)) {
      return std::nullopt;
    }
    auto center = bounds.get_center();
    return Rellu::transform(to_path(geometry),
      make_translation(center.m_x, center.m_y) *
      make_scale(width / bounds.m_width, height / bounds.m_height) *
      make_translation(-center.m_x, -center.m_y));
  }

  void append(Path& target, const Path& source) {
    auto start = Point();
    for(auto& segment : source.get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        target.move_to(points[0]);
        start = points[0];
      } else if(segment.m_type == SegmentType::LINE) {
        target.line_to(points[0]);
        start = points[0];
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        target.quadratic_to(points[0], points[1]);
        start = points[1];
      } else if(segment.m_type == SegmentType::CUBIC) {
        if(segment.m_is_arc) {
          target.arc_to(start + (points[0] - start) / ARC_CONTROL, points[2]);
        } else {
          target.cubic_to(points[0], points[1], points[2]);
        }
        start = points[2];
      } else {
        target.close();
        start = points[0];
      }
    }
  }

  std::optional<Path> make_ring(const ShapeGeometry& geometry, double width) {
    auto rectangle = std::get_if<RectangleShape>(&geometry);
    if(!rectangle) {
      return std::nullopt;
    }
    auto& bounds = rectangle->m_bounds;
    auto inner = Rect(bounds.m_x + width, bounds.m_y + width,
      bounds.m_width - 2 * width, bounds.m_height - 2 * width);
    if(!(inner.m_width > 0) || !(inner.m_height > 0)) {
      return std::nullopt;
    }
    auto radii = clamp_corner_radii(rectangle->m_radii, bounds.get_size());
    auto shrink = [&] (double radius) {
      return std::max(0.0, radius - width);
    };
    auto ring = Path(FillRule::EVEN_ODD);
    append(ring, to_path(geometry));
    append(ring, make_rectangle(inner, CornerRadii(shrink(radii.m_top_left),
      shrink(radii.m_top_right), shrink(radii.m_bottom_right),
      shrink(radii.m_bottom_left))));
    return ring;
  }

  bool is_populous(const Document& document, const NodeId& id) {
    auto count = std::size_t(0);
    auto pending = std::vector<const NodeId*>(1, &id);
    while(!pending.empty()) {
      auto node = document.find(*pending.back());
      pending.pop_back();
      if(!node) {
        continue;
      }
      if(++count >= POPULATION) {
        return true;
      }
      for(auto& child : node->m_children) {
        pending.push_back(&child);
      }
    }
    return false;
  }

  bool is_spanning(const auto& segments, std::size_t count) {
    if(segments.empty()) {
      return count == 0;
    }
    return segments.back().m_end == count;
  }

  bool is_blending(const std::vector<std::shared_ptr<const DrawList>>& lists) {
    return std::any_of(lists.begin(), lists.end(), [] (const auto& list) {
      return std::any_of(list->begin(), list->end(), [] (const auto& command) {
        if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
          return layer->m_blend_mode != BlendMode::NORMAL;
        }
        return std::holds_alternative<BackdropBlurCommand>(command);
      });
    });
  }

  bool is_clipping(const Mask& mask) {
    if(mask.m_shapes.size() != 1) {
      return false;
    }
    auto rectangle =
      std::get_if<RectangleShape>(&mask.m_shapes.front().m_geometry);
    if(!rectangle) {
      return false;
    }
    auto& radii = rectangle->m_radii;
    return radii.m_top_left <= 0 && radii.m_top_right <= 0 &&
      radii.m_bottom_right <= 0 && radii.m_bottom_left <= 0;
  }

  Matrix to_single(const Matrix& matrix) {
    auto round = [] (double value) {
      return static_cast<double>(static_cast<float>(value));
    };
    return Matrix(round(matrix.m_a), round(matrix.m_b), round(matrix.m_c),
      round(matrix.m_d), round(matrix.m_tx), round(matrix.m_ty));
  }

  bool translate(double& value, double offset) {
    auto moved = value + offset;
    if(moved - offset != value || moved - value != offset) {
      return false;
    }
    value = moved;
    return true;
  }

  bool translate(Matrix& matrix, Point offset) {
    return translate(matrix.m_tx, offset.m_x) &&
      translate(matrix.m_ty, offset.m_y);
  }

  using ClipMapping =
    std::vector<std::pair<std::optional<Rect>, std::optional<Rect>>>;

  bool map(ClipMapping& mapping, const std::optional<Rect>& from,
      const std::optional<Rect>& to) {
    for(auto& clip : mapping) {
      if(clip.first == from) {
        return clip.second == to;
      }
    }
    mapping.emplace_back(from, to);
    return true;
  }

  bool translate(std::optional<Rect>& clip, const ClipMapping& mapping) {
    for(auto& entry : mapping) {
      if(entry.first == clip) {
        clip = entry.second;
        return true;
      }
    }
    return false;
  }

  void clip_to_mask(const Node& source, auto& placement) {
    auto group = source.get_group();
    if(!group || !group->m_mask || placement.m_projection ||
        !is_clipping(*group->m_mask)) {
      return;
    }
    auto& shape = group->m_mask->m_shapes.front();
    auto matrix = placement.m_transform * shape.m_transform;
    if(is_axis_aligned(matrix)) {
      clip_to(placement.m_clip,
        matrix.map(std::get<RectangleShape>(shape.m_geometry).m_bounds));
    }
  }

  bool is_projected(const Paint& paint) {
    if(auto gradient = std::get_if<GradientPaint>(&paint)) {
      return static_cast<bool>(gradient->m_projection);
    } else if(auto image = std::get_if<ImagePaint>(&paint)) {
      return static_cast<bool>(image->m_projection);
    }
    return false;
  }

  std::shared_ptr<const DrawList> translate(
      const DrawList& list, const ClipMapping& clips, Point offset) {
    auto moved = list;
    for(auto& command : moved) {
      if(auto fill = std::get_if<FillCommand>(&command)) {
        if(is_projected(fill->m_paint) ||
            !translate(fill->m_transform, offset) ||
            !translate(fill->m_clip, clips)) {
          return nullptr;
        }
      } else if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
        if((layer->m_mask &&
            !translate(layer->m_mask->m_transform, offset)) ||
            !translate(layer->m_clip, clips)) {
          return nullptr;
        }
      } else if(auto blur = std::get_if<BackdropBlurCommand>(&command)) {
        if(!translate(blur->m_transform, offset) ||
            !translate(blur->m_clip, clips)) {
          return nullptr;
        }
      }
    }
    return std::make_shared<const DrawList>(std::move(moved));
  }

  bool is_exposed(const Node& source, const auto& placement, bool is_masked,
      const std::vector<std::shared_ptr<const DrawList>>& lists) {
    if(auto group = source.get_group()) {
      if(get_viewport(*group) && (placement.m_projection ||
          !is_axis_aligned(placement.m_transform))) {
        return false;
      }
    }
    auto& header = source.m_header;
    return !is_masked && header.m_blend_mode == BlendMode::NORMAL &&
      std::clamp(header.m_opacity, 0.0, 1.0) >= 1 &&
      !(header.m_is_isolated && is_blending(lists));
  }
}

SceneBuilder::SceneBuilder(const Document& document, double tolerance)
  : m_document(&document),
    m_tolerance(tolerance),
    m_raster_epoch(0),
    m_is_text_deferred(false),
    m_generation(0),
    m_paints(document),
    m_dependencies(nullptr),
    m_is_retaining(false) {}

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
  dependencies.m_is_device_dependent = false;
  dependencies.m_device_transform = std::nullopt;
  dependencies.m_is_rasterized = false;
  dependencies.m_is_device_mixed = false;
  m_dependencies = &dependencies;
  m_roots.clear();
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

std::vector<std::shared_ptr<const DrawList>> SceneBuilder::build_lists(
    const NodeId& id, SceneDependencies& dependencies) {
  ++m_generation;
  dependencies = make_dependencies();
  m_dependencies = &dependencies;
  m_roots.clear();
  m_is_retaining = true;
  auto segment = Segment(0, 1, true, false);
  try {
    if(auto node = m_document->find(id)) {
      if(auto placement = get_placement(node->m_parent, 0)) {
        retain(id, *placement, 0, segment);
      }
    }
  } catch(...) {
    clear_retained();
    m_dependencies = nullptr;
    m_is_retaining = false;
    throw;
  }
  m_dependencies = nullptr;
  m_is_retaining = false;
  merge(dependencies, segment.m_dependencies);
  normalize(dependencies);
  return std::move(segment.m_lists);
}

std::vector<std::shared_ptr<const DrawList>> SceneBuilder::build_lists(
    const NodeId& id, SceneDependencies& dependencies,
    std::chrono::steady_clock::time_point deadline) {
  m_deadline = deadline;
  try {
    auto lists = build_lists(id, dependencies);
    m_deadline = std::nullopt;
    return lists;
  } catch(...) {
    m_deadline = std::nullopt;
    throw;
  }
}

void SceneBuilder::invalidate(const NodeId& id) {
  report(id, false);
}

void SceneBuilder::invalidate_transform(const NodeId& id) {
  report(id, true);
}

void SceneBuilder::report(const NodeId& id, bool is_moved) {
  if(m_subtrees.empty()) {
    return;
  }
  invalidate_dependents(id);
  auto node = m_document->find(id);
  if(!node) {
    return;
  }
  if(auto subtree = m_subtrees.find(id);
      is_moved && subtree != m_subtrees.end()) {
    subtree->second.m_is_moved = true;
  } else {
    forget(id);
  }
  mark_ancestors(*node);
  auto root = node;
  for(auto depth = 0; depth != MAXIMUM_DEPTH && !root->m_parent.is_nil();
      ++depth) {
    auto parent = m_document->find(root->m_parent);
    if(!parent) {
      break;
    }
    root = parent;
  }
  if(root != node) {
    invalidate_dependents(root->get_id());
  }
}

void SceneBuilder::discard(const NodeId& id) {
  m_outlines.erase(id);
  m_backgrounds.erase(id);
  m_masks.erase(id);
  m_texts.erase(id);
  m_large.erase(id);
  forget(id);
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
  auto placement = Placement(parent.m_transform * transform,
    parent.m_opacity, parent.m_clip, parent.m_projection,
    to_single(parent.m_single * transform), parent.m_is_clipped,
    parent.m_is_referenced || static_cast<bool>(node.get_reference()));
  if(auto group = source->get_group();
      group && (group->m_mask || get_viewport(*group))) {
    placement.m_is_clipped = true;
  }
  if(placement.m_is_clipped) {
    placement.m_single = placement.m_transform;
  }
  if(!node.get_reference() && header.m_transform_3d) {
    auto projection = to_projection(*header.m_transform_3d, transform);
    if(parent.m_projection) {
      placement.m_projection =
        *parent.m_projection * parent.m_transform * projection;
    } else {
      placement.m_projection = parent.m_transform * projection;
    }
    placement.m_transform = Matrix();
    placement.m_single = Matrix();
  }
  if(auto artboard = source->get_artboard()) {
    auto& frame = artboard->m_frame;
    auto offset = make_translation(frame.m_x, frame.m_y);
    placement.m_transform = parent.m_transform * offset;
    placement.m_single = to_single(parent.m_single * offset);
    auto clip = parent.m_transform.map(frame);
    if(parent.m_clip) {
      clip = intersect(*parent.m_clip, clip);
    }
    placement.m_clip = clip;
  } else if(auto group = source->get_group()) {
    if(auto viewport = get_viewport(*group); viewport &&
        !placement.m_projection && is_axis_aligned(placement.m_transform)) {
      clip_to(placement.m_clip, placement.m_transform.map(*viewport));
    }
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
  if(auto entry = enter(*node, parent)) {
    add_entry(*node, *entry, depth, list);
  }
}

void SceneBuilder::add_entry(
    const Node& node, const Entry& entry, int depth, DrawList& list) {
  auto& source = *entry.m_source;
  auto placement = entry.m_placement;
  auto first = list.size();
  auto mask = get_group_mask(source, placement);
  if(source.get_artboard()) {
    add_artboard(source, placement, list);
  } else if(source.get_shape()) {
    add_shape(source, placement, list);
  } else if(source.get_text()) {
    add_text(source, placement, list);
  }
  if(placement.m_projection && (source.get_shape() || source.get_text())) {
    project(list, first, *placement.m_projection);
  }
  if(!source.get_shape() && !source.get_text()) {
    for(auto& child : *entry.m_children) {
      add_node(child, placement, depth + 1, list);
    }
    if(m_is_retaining && list.size() - first >= SEGMENT_SIZE) {
      m_large.insert(node.get_id());
    }
  }
  add_composites(source, placement, entry.m_placement.m_clip,
    std::move(mask), first, list);
}

std::optional<LayerMask> SceneBuilder::get_group_mask(
    const Node& source, Placement& placement) {
  auto group = source.get_group();
  if(!group || !group->m_mask) {
    return std::nullopt;
  } else if(placement.m_projection) {
    return get_mask(*group->m_mask, placement);
  }
  return get_mask(source, placement.m_transform, placement.m_clip);
}

void SceneBuilder::add_composites(const Node& source,
    const Placement& placement, const std::optional<Rect>& clip,
    std::optional<LayerMask> mask, std::size_t first, DrawList& list) {
  if(auto group = source.get_group()) {
    if(auto viewport = get_viewport(*group); viewport &&
        placement.m_projection) {
      add_composite(list, first, NodeHeader(), LayerMask(
        project(make_rectangle(*viewport), placement.m_transform,
          *placement.m_projection), Matrix()), placement.m_clip);
    } else if(viewport && !is_axis_aligned(placement.m_transform)) {
      add_composite(list, first, NodeHeader(), LayerMask(
        std::make_shared<const Path>(make_rectangle(*viewport)),
        placement.m_transform), placement.m_clip);
    }
  }
  add_composite(list, first, source.m_header, std::move(mask), clip);
}

void SceneBuilder::retain(const NodeId& id, const Placement& parent,
    int depth, Segment& segment) {
  if(auto subtree = m_subtrees.find(id); subtree != m_subtrees.end()) {
    auto& tree = subtree->second;
    if(tree.m_parent != parent || tree.m_depth != depth ||
        (tree.m_is_moved && !replace(id, tree, parent))) {
      forget(id);
    } else if(!tree.m_is_stale &&
        is_reusable(tree.m_total, tree.m_tolerance, m_tolerance,
          get_text_device(), is_late())) {
      segment.m_lists = tree.m_lists;
      segment.m_size = tree.m_size;
      segment.m_dependencies = tree.m_total;
      segment.m_tolerance = tree.m_tolerance;
      return;
    }
  }
  auto own = make_dependencies();
  auto outer = std::exchange(m_dependencies, &own);
  auto node = static_cast<const Node*>(nullptr);
  if(depth <= MAXIMUM_DEPTH) {
    node = m_document->find(id);
  }
  auto entry = std::optional<Entry>();
  if(node) {
    entry = enter(*node, parent);
  }
  if(!entry || entry->m_source->get_shape() || entry->m_source->get_text()) {
    forget(id);
    auto list = DrawList();
    if(entry) {
      add_entry(*node, *entry, depth, list);
    }
    segment.m_size = list.size();
    segment.m_lists.clear();
    if(!list.empty()) {
      segment.m_lists.push_back(
        std::make_shared<const DrawList>(std::move(list)));
    }
    normalize(own);
    segment.m_dependencies = std::move(own);
    segment.m_tolerance = m_tolerance;
    m_dependencies = outer;
    return;
  }
  auto& source = *entry->m_source;
  auto placement = entry->m_placement;
  auto mask = get_group_mask(source, placement);
  auto& children = *entry->m_children;
  auto is_own = entry->m_children == &node->m_children;
  auto [subtree, is_fresh] = m_subtrees.try_emplace(id);
  auto& tree = subtree->second;
  if(is_fresh || !is_spanning(tree.m_segments, children.size())) {
    tree.m_parent = parent;
    tree.m_depth = depth;
    tree.m_segments.clear();
    add_segments(source, children, is_own, 0, children.size(), SEGMENT_SIZE,
      placement, depth, tree.m_segments);
  } else {
    auto segments = std::vector<Segment>();
    segments.reserve(tree.m_segments.size());
    for(auto& previous : tree.m_segments) {
      if(!previous.m_is_stale && is_reusable(previous.m_dependencies,
          previous.m_tolerance, m_tolerance, get_text_device(), is_late())) {
        segments.push_back(std::move(previous));
      } else if(previous.m_is_retained) {
        auto& refreshed = segments.emplace_back(previous.m_begin,
          previous.m_end, true, false);
        retain(children[previous.m_begin], placement, depth + 1, refreshed);
      } else {
        add_segments(source, children, is_own, previous.m_begin,
          previous.m_end, 2 * SEGMENT_SIZE, placement, depth, segments);
      }
    }
    tree.m_segments = std::move(segments);
  }
  auto lists = std::vector<std::shared_ptr<const DrawList>>();
  auto total = own;
  auto size = std::size_t(0);
  for(auto& part : tree.m_segments) {
    lists.insert(lists.end(), part.m_lists.begin(), part.m_lists.end());
    size += part.m_size;
    merge(total, part.m_dependencies);
  }
  normalize(total);
  if(!is_exposed(source, placement, static_cast<bool>(mask), lists)) {
    auto list = DrawList();
    list.reserve(size + 4);
    for(auto& part : lists) {
      list.insert(list.end(), part->begin(), part->end());
    }
    add_composites(source, placement, entry->m_placement.m_clip,
      std::move(mask), 0, list);
    lists.clear();
    if(!list.empty()) {
      lists.push_back(std::make_shared<const DrawList>(std::move(list)));
    }
  }
  for(auto& key : tree.m_total.m_keys) {
    if(auto dependents = m_dependents.find(key);
        dependents != m_dependents.end()) {
      dependents->second.erase(id);
      if(dependents->second.empty()) {
        m_dependents.erase(dependents);
      }
    }
  }
  for(auto& key : total.m_keys) {
    m_dependents[key].insert(id);
  }
  normalize(own);
  tree.m_dependencies = std::move(own);
  tree.m_total = std::move(total);
  tree.m_lists = std::move(lists);
  tree.m_size = size;
  tree.m_placement = entry->m_placement;
  tree.m_is_stale = false;
  tree.m_is_moved = false;
  tree.m_is_surveyed = false;
  tree.m_clips.clear();
  tree.m_tolerance = m_tolerance;
  segment.m_lists = tree.m_lists;
  segment.m_size = tree.m_size;
  segment.m_dependencies = tree.m_total;
  segment.m_tolerance = m_tolerance;
  m_dependencies = outer;
}

bool SceneBuilder::replace(
    const NodeId& id, Subtree& tree, const Placement& parent) {
  if(tree.m_is_stale || m_device_transform ||
      tree.m_total.m_is_rasterized || !is_reusable(tree.m_total,
        tree.m_tolerance, m_tolerance, get_text_device(), false)) {
    return false;
  }
  auto node = m_document->find(id);
  if(!node) {
    return false;
  }
  auto outer = std::exchange(m_dependencies, nullptr);
  auto entry = enter(*node, parent);
  if(!entry) {
    m_dependencies = outer;
    return false;
  }
  auto& previous = tree.m_placement;
  auto& current = entry->m_placement;
  auto& from = previous.m_transform;
  auto& to = current.m_transform;
  auto offset = Point(to.m_tx - from.m_tx, to.m_ty - from.m_ty);
  auto moved = from;
  if(previous.m_projection || current.m_projection ||
      previous.m_opacity != current.m_opacity || from.m_a != to.m_a ||
      from.m_b != to.m_b || from.m_c != to.m_c || from.m_d != to.m_d ||
      !translate(moved, offset) || moved != to) {
    m_dependencies = outer;
    return false;
  }
  if(!tree.m_is_surveyed) {
    survey(*node, tree);
  }
  auto clips = ClipMapping();
  auto is_mapped = [&] {
    if(!tree.m_is_rigid || !map(clips, parent.m_clip, parent.m_clip) ||
        !map(clips, previous.m_clip, current.m_clip)) {
      return false;
    }
    auto left = previous;
    auto right = current;
    clip_to_mask(*entry->m_source, left);
    clip_to_mask(*entry->m_source, right);
    if(!map(clips, left.m_clip, right.m_clip)) {
      return false;
    }
    for(auto& chain : tree.m_clips) {
      auto before = left;
      auto after = right;
      for(auto& link : chain) {
        auto child = m_document->find(link);
        if(!child) {
          return false;
        }
        auto first = enter(*child, before);
        auto second = enter(*child, after);
        if(!first || !second) {
          if(first || second) {
            return false;
          }
          break;
        }
        before = first->m_placement;
        after = second->m_placement;
        if(!map(clips, before.m_clip, after.m_clip)) {
          return false;
        }
        clip_to_mask(*first->m_source, before);
        clip_to_mask(*second->m_source, after);
        if(!map(clips, before.m_clip, after.m_clip)) {
          return false;
        }
      }
    }
    return true;
  }();
  m_dependencies = outer;
  if(!is_mapped) {
    return false;
  }
  auto translations = std::unordered_map<const DrawList*,
    std::shared_ptr<const DrawList>>();
  auto move = [&] (const std::vector<std::shared_ptr<const DrawList>>& lists) {
    for(auto& list : lists) {
      auto& translation = translations[list.get()];
      if(!translation) {
        translation = translate(*list, clips, offset);
        if(!translation) {
          return false;
        }
      }
    }
    return true;
  };
  if(!move(tree.m_lists) || !std::all_of(tree.m_segments.begin(),
      tree.m_segments.end(), [&] (const auto& segment) {
        return move(segment.m_lists);
      })) {
    return false;
  }
  auto apply = [&] (std::vector<std::shared_ptr<const DrawList>>& lists) {
    for(auto& list : lists) {
      list = translations.at(list.get());
    }
  };
  apply(tree.m_lists);
  for(auto& segment : tree.m_segments) {
    apply(segment.m_lists);
  }
  tree.m_placement = current;
  tree.m_is_moved = false;
  return true;
}

void SceneBuilder::survey(const Node& node, Subtree& tree) const {
  tree.m_is_surveyed = true;
  tree.m_is_rigid = false;
  tree.m_clips.clear();
  auto& nodes = m_document->get_nodes();
  auto path = std::vector<NodeId>();
  auto pending = std::vector<std::pair<const Node*, int>>();
  pending.emplace_back(&node, 0);
  while(!pending.empty()) {
    auto [current, depth] = pending.back();
    pending.pop_back();
    if(depth > MAXIMUM_DEPTH) {
      tree.m_clips.clear();
      return;
    }
    if(depth != 0) {
      path.resize(static_cast<std::size_t>(depth - 1));
      path.push_back(current->get_id());
    }
    auto source = current;
    auto children = &current->m_children;
    if(current->get_reference()) {
      auto resolution = resolve_reference(nodes, current->get_id());
      if(!resolution) {
        continue;
      }
      source = &nodes.get(resolution->m_source);
      children = &nodes.get(resolution->m_child_source).m_children;
    } else if(depth != 0 && current->m_header.m_transform_3d) {
      tree.m_clips.clear();
      return;
    }
    if(source->get_artboard()) {
      tree.m_clips.clear();
      return;
    } else if(source->get_shape() || source->get_text()) {
      continue;
    }
    auto group = source->get_group();
    if(depth != 0 && group && (get_viewport(*group) ||
        (group->m_mask && is_clipping(*group->m_mask)))) {
      tree.m_clips.push_back(path);
    }
    for(auto& child : *children) {
      if(auto descendant = nodes.find(child)) {
        pending.emplace_back(descendant, depth + 1);
      }
    }
  }
  tree.m_is_rigid = true;
}

void SceneBuilder::add_segments(const Node& source,
    const std::vector<NodeId>& children, bool is_own, std::size_t begin,
    std::size_t end, std::size_t limit, const Placement& placement, int depth,
    std::vector<Segment>& segments) {
  auto run = Segment(begin, begin, false, false);
  run.m_dependencies = make_dependencies();
  auto list = DrawList();
  auto outer = std::exchange(m_dependencies, &run.m_dependencies);
  auto flush = [&] (std::size_t position) {
    if(position != run.m_begin || !list.empty()) {
      run.m_end = position;
      run.m_size = list.size();
      if(!list.empty()) {
        run.m_lists.push_back(
          std::make_shared<const DrawList>(std::move(list)));
        list = DrawList();
      }
      normalize(run.m_dependencies);
      run.m_tolerance = m_tolerance;
      segments.push_back(std::move(run));
    }
    run = Segment(position, position, false, false);
    run.m_dependencies = make_dependencies();
    m_dependencies = &run.m_dependencies;
  };
  if(begin == 0 && source.get_artboard()) {
    add_artboard(source, placement, list);
  }
  for(auto i = begin; i != end; ++i) {
    auto& child = children[i];
    if(is_own && (m_subtrees.contains(child) || m_large.contains(child) ||
        is_populous(*m_document, child))) {
      flush(i);
      auto& retained = segments.emplace_back(i, i + 1, true, false);
      retain(child, placement, depth + 1, retained);
      run.m_begin = i + 1;
      run.m_end = i + 1;
    } else {
      add_node(child, placement, depth + 1, list);
      if(list.size() >= limit) {
        flush(i + 1);
      }
    }
  }
  flush(end);
  m_dependencies = outer;
}

void SceneBuilder::forget(const NodeId& id) {
  auto subtree = m_subtrees.find(id);
  if(subtree == m_subtrees.end()) {
    return;
  }
  for(auto& key : subtree->second.m_total.m_keys) {
    if(auto dependents = m_dependents.find(key);
        dependents != m_dependents.end()) {
      dependents->second.erase(id);
      if(dependents->second.empty()) {
        m_dependents.erase(dependents);
      }
    }
  }
  m_subtrees.erase(subtree);
}

void SceneBuilder::mark_ancestors(const Node& node) {
  auto child = &node;
  for(auto depth = 0; depth != MAXIMUM_DEPTH && !child->m_parent.is_nil();
      ++depth) {
    auto parent = m_document->find(child->m_parent);
    if(!parent) {
      return;
    }
    if(auto subtree = m_subtrees.find(parent->get_id());
        subtree != m_subtrees.end()) {
      auto& tree = subtree->second;
      tree.m_is_stale = true;
      auto& siblings = parent->m_children;
      auto index = static_cast<std::size_t>(std::distance(siblings.begin(),
        std::find(siblings.begin(), siblings.end(), child->get_id())));
      auto segment = std::find_if(tree.m_segments.begin(),
        tree.m_segments.end(), [&] (const auto& segment) {
          return segment.m_begin <= index && index < segment.m_end;
        });
      if(segment == tree.m_segments.end()) {
        forget(parent->get_id());
      } else {
        segment->m_is_stale = true;
      }
    }
    child = parent;
  }
}

void SceneBuilder::invalidate_dependents(const NodeId& key) {
  auto dependents = m_dependents.find(key);
  if(dependents == m_dependents.end()) {
    return;
  }
  auto ids = std::vector<NodeId>(
    dependents->second.begin(), dependents->second.end());
  for(auto& id : ids) {
    auto subtree = m_subtrees.find(id);
    if(subtree == m_subtrees.end()) {
      continue;
    }
    auto& tree = subtree->second;
    if(std::binary_search(tree.m_dependencies.m_keys.begin(),
        tree.m_dependencies.m_keys.end(), key)) {
      forget(id);
    } else {
      tree.m_is_stale = true;
      for(auto& segment : tree.m_segments) {
        if(std::binary_search(segment.m_dependencies.m_keys.begin(),
            segment.m_dependencies.m_keys.end(), key)) {
          segment.m_is_stale = true;
        }
      }
    }
    if(auto node = m_document->find(id)) {
      mark_ancestors(*node);
    }
  }
}

bool SceneBuilder::is_late() const {
  return m_deadline && std::chrono::steady_clock::now() >= *m_deadline;
}

const std::optional<Matrix>& SceneBuilder::get_text_device() const {
  if(m_device_transform) {
    return m_device_transform;
  }
  return m_text_transform;
}

void SceneBuilder::clear_retained() {
  m_subtrees.clear();
  m_dependents.clear();
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
    auto next = nodes.find(payload->m_source);
    if(!next) {
      keys.push_back(payload->m_source);
      return;
    }
    auto [root, is_new] = m_roots.try_emplace(payload->m_source);
    if(is_new) {
      auto top = next;
      for(auto depth = 0; depth != MAXIMUM_DEPTH && !top->m_parent.is_nil();
          ++depth) {
        auto parent = nodes.find(top->m_parent);
        if(!parent) {
          break;
        }
        top = parent;
      }
      root->second = top->get_id();
    }
    keys.push_back(root->second);
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

std::optional<LayerMask> SceneBuilder::get_mask(
    const Mask& mask, const Placement& placement) {
  auto clip = std::optional<Rect>();
  auto local = make_mask(mask, placement.m_transform, m_tolerance, clip);
  if(local) {
    return LayerMask(project(*local->m_path, local->m_transform,
      *placement.m_projection), Matrix());
  } else if(clip) {
    return LayerMask(
      project(make_rectangle(*clip), Matrix(), *placement.m_projection),
      Matrix());
  }
  return std::nullopt;
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
  auto poster = shape.m_video ?
    std::optional<Fill>(shape.m_video->m_poster) : std::nullopt;
  auto& paint_fill = poster ? *poster : style.m_fill;
  auto& outline = get_outline(
    node, get_scale(placement.m_transform, placement.m_projection));
  auto& transform =
    m_device_transform ? placement.m_single : placement.m_transform;
  auto fill = std::optional<FillCommand>();
  if(!std::holds_alternative<LineShape>(shape.m_geometry)) {
    if(auto paint = convert(paint_fill, *outline.m_path)) {
      fill.emplace(outline.m_path, transform, std::move(*paint),
        placement.m_clip);
    }
  }
  auto stroke = std::optional<FillCommand>();
  if(is_drawn(style.m_stroke)) {
    stroke.emplace(outline.m_stroke, transform,
      make_paint(style.m_stroke->m_color, placement.m_opacity),
      placement.m_clip);
    if(style.m_stroke->m_dashes.empty() && (outline.m_line ||
        style.m_stroke->m_alignment == StrokeAlignment::CENTER ||
          is_open(*outline.m_path))) {
      auto& line = outline.m_line ? outline.m_line : outline.m_path;
      stroke->m_stroke = StrokeLine(line, style.m_stroke->m_width,
        style.m_stroke->m_cap, style.m_stroke->m_join,
        style.m_stroke->m_miter_limit);
    }
  }
  auto is_inside = style.m_stroke &&
    style.m_stroke->m_alignment == StrokeAlignment::INSIDE;
  if(stroke && outline.m_line) {
    if(fill && style.m_stroke->m_color.m_alpha >= 1 &&
        !is_inward(style.m_effects)) {
      fill->m_path = outline.m_line;
      if(auto inset = is_inside && is_rounded(shape.m_geometry) ?
          shrink_path(shape.m_geometry,
            INSIDE_FILL_INSET * style.m_stroke->m_width) : std::nullopt) {
        fill->m_path = std::make_shared<const Path>(std::move(*inset));
      }
    }
    add_styled(list, std::move(fill), std::move(stroke), style.m_effects);
    return;
  }
  if(fill && stroke && outline.m_layered && is_opaque(paint_fill) &&
      placement.m_opacity >= 1 && !is_inward(style.m_effects) &&
      (!is_inside || style.m_stroke->m_color.m_alpha >= 1)) {
    if(is_inside) {
      stroke->m_path = outline.m_path;
      fill->m_path = outline.m_layered;
    } else {
      stroke->m_path = outline.m_layered;
    }
    add_styled(list, std::move(stroke), std::move(fill), style.m_effects);
    return;
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
    outline.m_line = nullptr;
    outline.m_stroke = nullptr;
    outline.m_layered = nullptr;
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
    outline.m_line = nullptr;
    outline.m_layered = nullptr;
    auto geometry = to_geometry(*stroke);
    if(stroke->m_alignment != StrokeAlignment::CENTER &&
        stroke->m_dashes.empty()) {
      auto distance = stroke->m_width / 2;
      if(stroke->m_alignment == StrokeAlignment::OUTSIDE) {
        distance = -distance;
      }
      if(auto line = offset(shape.m_geometry, distance)) {
        outline.m_line = std::make_shared<const Path>(std::move(*line));
        geometry.m_alignment = StrokeAlignment::CENTER;
      }
    }
    if(outline.m_line) {
      outline.m_stroke = std::make_shared<const Path>(
        outline_stroke(*outline.m_line, geometry, tolerance));
    } else if(auto ring = stroke->m_dashes.empty() &&
        stroke->m_alignment == StrokeAlignment::INSIDE ?
          make_ring(shape.m_geometry, stroke->m_width) : std::nullopt) {
      outline.m_stroke = std::make_shared<const Path>(std::move(*ring));
    } else if(auto dashed = stroke->m_dashes.empty() ? std::nullopt :
        make_dash_path(shape.m_geometry, stroke->m_alignment)) {
      outline.m_stroke = std::make_shared<const Path>(
        outline_stroke(*dashed, geometry, tolerance));
    } else {
      outline.m_stroke = std::make_shared<const Path>(
        outline_stroke(*outline.m_path, geometry, tolerance));
    }
    if(!outline.m_line && stroke->m_alignment != StrokeAlignment::CENTER &&
        stroke->m_dashes.empty() && is_closed(*outline.m_path)) {
      auto operation = BooleanOperation::ADD;
      if(stroke->m_alignment == StrokeAlignment::INSIDE) {
        operation = BooleanOperation::SUBTRACT;
      }
      outline.m_layered = std::make_shared<const Path>(combine(
        *outline.m_path, *outline.m_stroke, operation, tolerance));
    }
    outline.m_tolerance = tolerance;
  }
  add_tolerance(outline.m_tolerance * divisor * (1 + TOLERANCE_MARGIN),
    4 * outline.m_tolerance * divisor * (1 - TOLERANCE_MARGIN));
  return outline;
}

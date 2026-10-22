#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Shapes.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 1e-9;
  constexpr auto FIT_TOLERANCE = 0.001;
  constexpr auto MINIMUM_FONT_SIZE = 1.0;
  constexpr auto MAXIMUM_FONT_SIZE = 1000.0;
  constexpr auto FONT_STEP = 5.0;
  constexpr auto LARGE_NUDGE = 10.0;
  constexpr auto GRID_TOLERANCE = 0.001;
  constexpr auto CANVAS_MARGIN = 10.0;

  std::optional<Point> get_axis_scale(const Matrix& matrix) {
    auto magnitude = std::abs(matrix.m_a) + std::abs(matrix.m_d);
    if(!(std::abs(matrix.m_a) > 0) || !(std::abs(matrix.m_d) > 0) ||
        std::abs(matrix.m_b) > TOLERANCE * magnitude ||
        std::abs(matrix.m_c) > TOLERANCE * magnitude) {
      return std::nullopt;
    }
    return Point(matrix.m_a, matrix.m_d);
  }

  bool is_unit(Point scale) {
    return std::abs(scale.m_x - 1) <= TOLERANCE &&
      std::abs(scale.m_y - 1) <= TOLERANCE;
  }

  bool is_scalable(const Node& node, Point scale) {
    if(is_unit(scale) || node.get_shape()) {
      return true;
    } else if(auto text = node.get_text()) {
      if(std::holds_alternative<AreaFrame>(text->m_frame)) {
        return true;
      } else if(std::holds_alternative<AutoHeightFrame>(text->m_frame)) {
        return std::abs(scale.m_y - 1) <= TOLERANCE;
      }
    } else if(auto group = node.get_group()) {
      return !group->m_repeat_grid && !group->m_scroll_group &&
        !group->m_stack && !group->m_padding && !group->m_component_instance;
    }
    return false;
  }

  Point scale_point(Point point, Point scale) {
    return Point(point.m_x * scale.m_x, point.m_y * scale.m_y);
  }

  ShapeGeometry scale_geometry(const ShapeGeometry& geometry, Point scale) {
    return std::visit([&] (const auto& shape) -> ShapeGeometry {
      using Type = std::decay_t<decltype(shape)>;
      auto scaled = shape;
      if constexpr(std::is_same_v<Type, RectangleShape>) {
        scaled.m_bounds = Rect(shape.m_bounds.m_x * scale.m_x,
          shape.m_bounds.m_y * scale.m_y, shape.m_bounds.m_width * scale.m_x,
          shape.m_bounds.m_height * scale.m_y);
      } else if constexpr(std::is_same_v<Type, EllipseShape>) {
        scaled.m_center = scale_point(shape.m_center, scale);
        scaled.m_x_radius = shape.m_x_radius * scale.m_x;
        scaled.m_y_radius = shape.m_y_radius * scale.m_y;
      } else if constexpr(std::is_same_v<Type, LineShape>) {
        scaled.m_start = scale_point(shape.m_start, scale);
        scaled.m_end = scale_point(shape.m_end, scale);
      } else if constexpr(std::is_same_v<Type, PolygonShape>) {
        scaled.m_size = Size(
          shape.m_size.m_width * scale.m_x, shape.m_size.m_height * scale.m_y);
        for(auto& vertex : scaled.m_vertices) {
          vertex = scale_point(vertex, scale);
        }
      } else {
        scaled.m_path = Shared(
          transform(*shape.m_path, make_scale(scale.m_x, scale.m_y)));
      }
      return scaled;
    }, geometry);
  }

  void reshape(const Document& document, Transaction& transaction,
    const NodeId& id, const Matrix& transform, Matrix local, Matrix composed,
    const TextMeasurer& measurer);

  bool is_point_text(const Node& node) {
    auto text = node.get_text();
    return text && std::holds_alternative<PositionedFrame>(text->m_frame);
  }

  int compare_fit(const Rect& bounds, Size target) {
    auto width = bounds.m_width - target.m_width;
    auto height = bounds.m_height - target.m_height;
    if(width < -FIT_TOLERANCE && height < -FIT_TOLERANCE) {
      return -1;
    } else if(width > FIT_TOLERANCE || height > FIT_TOLERANCE) {
      return 1;
    }
    return 0;
  }

  Text fit_font(const Text& text, Size target, const TextMeasurer& measurer) {
    auto maximum = text.m_runs.empty() ? text.m_base_style.m_font.m_size : 0;
    for(auto& run : text.m_runs) {
      maximum = std::max(maximum, run.m_style.m_font.m_size);
    }
    maximum = std::max(maximum, MINIMUM_FONT_SIZE);
    auto resize = [&] (double size) {
      auto scale = [&] (double value, double minimum) {
        return std::max(std::round(value / maximum * size), minimum);
      };
      auto font = [&] (double value) {
        return std::min(scale(value, MINIMUM_FONT_SIZE), MAXIMUM_FONT_SIZE);
      };
      auto fitted = text;
      fitted.m_base_style.m_font.m_size = font(text.m_base_style.m_font.m_size);
      for(auto& run : fitted.m_runs) {
        run.m_style.m_font.m_size = font(run.m_style.m_font.m_size);
      }
      if(auto& height = fitted.m_paragraph_style.m_line_height) {
        *height = scale(*height, *height == 0 ? 0 : 1);
      }
      auto& spacing = fitted.m_paragraph_style.m_paragraph_spacing;
      spacing = scale(spacing, spacing == 0 ? 0 : 1);
      fitted.m_layout = std::nullopt;
      return fitted;
    };
    auto size = maximum;
    auto fitted = resize(size);
    while(compare_fit(measurer(fitted), target) < 0 &&
        size < MAXIMUM_FONT_SIZE) {
      size += FONT_STEP;
      fitted = resize(size);
    }
    while(compare_fit(measurer(fitted), target) > 0 &&
        size > MINIMUM_FONT_SIZE) {
      size -= 1;
      fitted = resize(size);
    }
    return fitted;
  }

  bool is_mirrorable(const Node& node) {
    if(node.get_shape()) {
      return true;
    } else if(auto group = node.get_group()) {
      return !group->m_repeat_grid && !group->m_scroll_group &&
        !group->m_stack && !group->m_padding && !group->m_component_instance;
    }
    return false;
  }

  Matrix make_reflection(Point center, bool is_horizontal, bool is_vertical) {
    return make_translation(center.m_x, center.m_y) *
      make_scale(is_horizontal ? -1 : 1, is_vertical ? -1 : 1) *
      make_translation(-center.m_x, -center.m_y);
  }

  bool is_rotated_reflection(
      const Node& node, const Matrix& transform, const Matrix& local) {
    constexpr auto REFLECTION_TOLERANCE = 1e-9;
    if(node.get_text() || !is_mirrorable(node) ||
        get_rotation(transform) == 0 || !(local.get_determinant() < 0)) {
      return false;
    }
    return std::abs(local.m_a * local.m_a + local.m_b * local.m_b - 1) <
      REFLECTION_TOLERANCE && std::abs(local.m_c * local.m_c +
        local.m_d * local.m_d - 1) < REFLECTION_TOLERANCE &&
      std::abs(local.m_a * local.m_c + local.m_b * local.m_d) <
        REFLECTION_TOLERANCE;
  }

  CornerRadii mirror_radii(
      CornerRadii radii, bool is_horizontal, bool is_vertical) {
    if(is_horizontal) {
      std::swap(radii.m_top_left, radii.m_top_right);
      std::swap(radii.m_bottom_left, radii.m_bottom_right);
    }
    if(is_vertical) {
      std::swap(radii.m_top_left, radii.m_bottom_left);
      std::swap(radii.m_top_right, radii.m_bottom_right);
    }
    return radii;
  }

  ShapeGeometry mirror_geometry(const ShapeGeometry& geometry,
      const Matrix& reflection, bool is_horizontal, bool is_vertical) {
    return std::visit([&] (const auto& shape) -> ShapeGeometry {
      using Type = std::decay_t<decltype(shape)>;
      auto mirrored = shape;
      if constexpr(std::is_same_v<Type, RectangleShape>) {
        mirrored.m_bounds = reflection.map(shape.m_bounds);
        mirrored.m_radii =
          mirror_radii(shape.m_radii, is_horizontal, is_vertical);
      } else if constexpr(std::is_same_v<Type, EllipseShape>) {
        mirrored.m_center = reflection.map(shape.m_center);
      } else if constexpr(std::is_same_v<Type, LineShape>) {
        mirrored.m_start = reflection.map(shape.m_start);
        mirrored.m_end = reflection.map(shape.m_end);
      } else if constexpr(std::is_same_v<Type, PolygonShape>) {
        if(shape.m_vertices.empty()) {
          if(!is_vertical || shape.m_corner_count % 2 == 0) {
            return mirrored;
          }
          mirrored.m_vertices = make_polygon_vertices(shape.m_corner_count,
            Rect(0, 0, shape.m_size.m_width, shape.m_size.m_height),
            shape.m_star_ratio);
        }
        for(auto& vertex : mirrored.m_vertices) {
          vertex = reflection.map(vertex);
        }
      } else {
        mirrored.m_path = Shared(transform(*shape.m_path, reflection));
      }
      return mirrored;
    }, geometry);
  }

  void mirror_gradient(GradientFill& gradient, const Matrix& reflection) {
    if(!gradient.m_transform.is_identity()) {
      gradient.m_transform = reflection * gradient.m_transform;
    } else if(auto linear = std::get_if<LinearGradient>(&gradient.m_geometry)) {
      linear->m_start = reflection.map(linear->m_start);
      linear->m_end = reflection.map(linear->m_end);
    } else if(auto radial =
        std::get_if<RadialGradient>(&gradient.m_geometry)) {
      radial->m_center = reflection.map(radial->m_center);
      radial->m_focus = reflection.map(radial->m_focus);
    } else if(auto angular =
        std::get_if<AngularGradient>(&gradient.m_geometry)) {
      auto radians = angular->m_rotation * std::numbers::pi / 180;
      auto direction = reflection.map(angular->m_center +
        Point(std::cos(radians), std::sin(radians))) -
          reflection.map(angular->m_center);
      angular->m_center = reflection.map(angular->m_center);
      angular->m_rotation =
        std::atan2(direction.m_y, direction.m_x) * 180 / std::numbers::pi;
      if(reflection.m_a * reflection.m_d - reflection.m_b * reflection.m_c <
          0 && !gradient.m_stops.empty()) {
        auto stops = gradient.m_stops;
        std::reverse(stops.begin(), stops.end());
        for(auto& stop : stops) {
          stop.m_offset = 1 - stop.m_offset;
        }
        stops.front().m_offset = 0;
        stops.back().m_offset = 1;
        gradient.m_stops = std::move(stops);
      }
    }
  }

  Fill mirror_fill(const Fill& fill, const Matrix& reflection,
      bool is_horizontal, bool is_vertical) {
    auto mirrored = fill;
    if(auto image = std::get_if<ImageFill>(&mirrored)) {
      image->m_is_flipped_horizontally ^= is_horizontal;
      image->m_is_flipped_vertically ^= is_vertical;
    } else if(auto gradient = std::get_if<GradientFill>(&mirrored)) {
      if(gradient->m_units == GradientUnits::OBJECT_BOUNDING_BOX) {
        mirror_gradient(*gradient,
          make_reflection(Point(0.5, 0.5), is_horizontal, is_vertical));
      } else {
        mirror_gradient(*gradient, reflection);
      }
    }
    return mirrored;
  }

  void mirror_children(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& children, const Matrix& reflection,
      const TextMeasurer& measurer) {
    for(auto& child : children) {
      auto transform = get_transform(document, child);
      auto composed = reflection * transform;
      if(auto inverse = invert(transform)) {
        reshape(document, transaction, child, transform,
          *inverse * reflection * transform, composed, measurer);
      } else {
        set_transform(document, transaction, child, composed);
      }
    }
  }

  MaskShape mirror_mask_shape(
      const MaskShape& shape, const Matrix& reflection) {
    auto mirrored = shape;
    if(auto inverse = invert(shape.m_transform)) {
      auto local = *inverse * reflection * shape.m_transform;
      if(get_axis_scale(local)) {
        mirrored.m_geometry = mirror_geometry(
          shape.m_geometry, local, local.m_a < 0, local.m_d < 0);
        return mirrored;
      }
    }
    mirrored.m_transform = reflection * shape.m_transform;
    return mirrored;
  }

  void mirror_content(const Document& document, Transaction& transaction,
      const NodeId& id, Point center, bool is_horizontal, bool is_vertical,
      const TextMeasurer& measurer) {
    auto reflection = make_reflection(center, is_horizontal, is_vertical);
    auto& node = document.get(id);
    auto children = node.m_children;
    if(auto shape = node.get_shape()) {
      auto payload = *shape;
      payload.m_geometry = mirror_geometry(
        shape->m_geometry, reflection, is_horizontal, is_vertical);
      payload.m_style.m_fill = mirror_fill(
        shape->m_style.m_fill, reflection, is_horizontal, is_vertical);
      auto is_compound =
        std::holds_alternative<CompoundShape>(payload.m_geometry);
      transaction.replace_payload(id, std::move(payload));
      if(is_compound) {
        mirror_children(
          document, transaction, children, reflection, measurer);
      }
    } else if(auto group = node.get_group()) {
      if(group->m_mask) {
        auto payload = *group;
        for(auto& mask : payload.m_mask->m_shapes) {
          mask = mirror_mask_shape(mask, reflection);
        }
        transaction.replace_payload(id, std::move(payload));
      }
      mirror_children(document, transaction, children, reflection, measurer);
    }
  }

  void scale_children(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& children, Point scale,
      const TextMeasurer& measurer) {
    auto matrix = make_scale(scale.m_x, scale.m_y);
    for(auto& child : children) {
      auto transform = get_transform(document, child);
      auto composed = matrix * transform;
      if(auto inverse = invert(transform)) {
        reshape(document, transaction, child, transform,
          *inverse * matrix * transform, composed, measurer);
      } else {
        set_transform(document, transaction, child, composed);
      }
    }
  }

  void scale_content(const Document& document, Transaction& transaction,
      const NodeId& id, Point scale, const TextMeasurer& measurer) {
    if(is_unit(scale)) {
      return;
    }
    auto& node = document.get(id);
    auto children = node.m_children;
    if(auto shape = node.get_shape()) {
      auto payload = *shape;
      payload.m_geometry = scale_geometry(shape->m_geometry, scale);
      auto is_compound =
        std::holds_alternative<CompoundShape>(payload.m_geometry);
      transaction.replace_payload(id, std::move(payload));
      if(is_compound) {
        scale_children(document, transaction, children, scale, measurer);
      }
    } else if(auto text = node.get_text()) {
      auto payload = *text;
      if(auto area = std::get_if<AreaFrame>(&payload.m_frame)) {
        area->m_size = Size(area->m_size.m_width * scale.m_x,
          area->m_size.m_height * scale.m_y);
      } else if(auto frame = std::get_if<AutoHeightFrame>(&payload.m_frame)) {
        frame->m_width *= scale.m_x;
      }
      if(std::abs(scale.m_x - 1) > TOLERANCE) {
        payload.m_layout = std::nullopt;
      }
      transaction.replace_payload(id, std::move(payload));
    } else if(auto group = node.get_group()) {
      if(group->m_mask) {
        auto payload = *group;
        auto matrix = make_scale(scale.m_x, scale.m_y);
        for(auto& shape : payload.m_mask->m_shapes) {
          shape.m_transform = matrix * shape.m_transform;
        }
        transaction.replace_payload(id, std::move(payload));
      }
      scale_children(document, transaction, children, scale, measurer);
    }
  }

  void fit_point_text(const Document& document, Transaction& transaction,
      const NodeId& id, const Matrix& base, const Matrix& local,
      const TextMeasurer& measurer) {
    auto& text = *document.get(id).get_text();
    auto bounds = measurer(text);
    auto target = local.map(bounds);
    auto payload = text;
    if(std::abs(target.m_width - bounds.m_width) > TOLERANCE ||
        std::abs(target.m_height - bounds.m_height) > TOLERANCE) {
      payload = fit_font(text, target.get_size(), measurer);
    }
    auto fitted = measurer(payload);
    auto transform = base * make_translation(
      target.m_x - fitted.m_x, target.m_y - fitted.m_y);
    if(payload != text) {
      transaction.replace_payload(id, std::move(payload));
    }
    if(transform != get_transform(document, id)) {
      set_transform(document, transaction, id, transform);
    }
  }

  void reshape(const Document& document, Transaction& transaction,
      const NodeId& id, const Matrix& transform, Matrix local,
      Matrix composed, const TextMeasurer& measurer) {
    if(is_rotated_reflection(document.get(id), transform, local)) {
      if(auto bounds = get_local_bounds(
          document, id, BoundsType::GEOMETRY, measurer)) {
        auto center = bounds->get_center();
        mirror_content(
          document, transaction, id, center, false, true, measurer);
        auto updated = composed * make_reflection(center, false, true);
        if(updated != transform) {
          set_transform(document, transaction, id, updated);
        }
        return;
      }
    }
    auto scale = get_axis_scale(local);
    auto base = transform;
    auto is_text_mirrored = false;
    if(scale && (scale->m_x < 0 || scale->m_y < 0)) {
      if(auto bounds = get_local_bounds(
          document, id, BoundsType::GEOMETRY, measurer)) {
        auto& node = document.get(id);
        auto center = bounds->get_center();
        if(node.get_text()) {
          auto reflection =
            make_reflection(center, scale->m_x < 0, scale->m_y < 0);
          is_text_mirrored = (scale->m_x < 0) != (scale->m_y < 0);
          base = transform * reflection;
          local = reflection * local;
          scale = get_axis_scale(local);
        } else {
          if(is_mirrorable(node)) {
            mirror_content(document, transaction, id, center,
              scale->m_x < 0, scale->m_y < 0, measurer);
          }
          auto mapped = local.map(*bounds);
          scale = Point(std::abs(scale->m_x), std::abs(scale->m_y));
          local = Matrix(scale->m_x, 0, 0, scale->m_y,
            mapped.m_x - scale->m_x * bounds->m_x,
            mapped.m_y - scale->m_y * bounds->m_y);
          composed = transform * local;
        }
      }
    }
    if(!scale && local.get_determinant() < 0 &&
        document.get(id).get_text()) {
      is_text_mirrored = true;
    }
    if(scale && scale->m_x > 0 && scale->m_y > 0 &&
        is_scalable(document.get(id), *scale)) {
      auto updated = base * make_translation(local.m_tx, local.m_ty);
      if(updated != transform) {
        set_transform(document, transaction, id, updated);
      }
      scale_content(document, transaction, id, *scale, measurer);
    } else if(scale && scale->m_x > 0 && scale->m_y > 0 &&
        is_point_text(document.get(id))) {
      fit_point_text(document, transaction, id, base, local, measurer);
    } else if(composed != transform) {
      set_transform(document, transaction, id, composed);
    }
    if(is_text_mirrored) {
      auto payload = *document.get(id).get_text();
      auto flip = make_reflection(measurer(payload).get_center(), false, true);
      set_transform(
        document, transaction, id, get_transform(document, id) * flip);
      payload.m_is_flipped_vertically = !payload.m_is_flipped_vertically;
      transaction.replace_payload(id, std::move(payload));
    }
  }

  double round_half_up(double value) {
    return std::floor(value + 0.5);
  }

  struct RotationRecord {
    NodeId m_id;
    std::shared_ptr<const Node> m_node;
    double m_rotation;
    bool m_is_resized;
  };

  void record_rotations(const Document& document, const NodeId& id,
      bool is_resized, std::vector<RotationRecord>& records) {
    auto& node = document.get(id);
    if(!node.get_artboard() && !node.get_reference() &&
        !node.m_header.m_transform_3d) {
      records.push_back(RotationRecord(id,
        document.get_nodes().get_record(id), node.m_header.m_rotation,
        is_resized));
    }
    for(auto& child : node.m_children) {
      record_rotations(document, child, false, records);
    }
  }

  bool overlaps(const Rect& left, const Rect& right) {
    return left.get_left() < right.get_right() &&
      right.get_left() < left.get_right() &&
      left.get_top() < right.get_bottom() &&
      right.get_top() < left.get_bottom();
  }

  void transform_artboard(const Document& document, Transaction& transaction,
      const NodeId& id, const Matrix& mapping) {
    auto& node = document.get(id);
    auto parent = get_parent_transform(document, id);
    auto inverse = invert(parent);
    if(!inverse) {
      return;
    }
    auto local = *inverse * mapping * parent;
    auto scale = get_axis_scale(local);
    if(!scale || !(scale->m_x > 0) || !(scale->m_y > 0)) {
      return;
    }
    auto payload = *node.get_artboard();
    auto frame = payload.m_frame;
    payload.m_frame = local.map(frame);
    if(payload.m_frame == frame) {
      return;
    }
    auto children = node.m_children;
    auto offset = Point(frame.m_x - payload.m_frame.m_x,
      frame.m_y - payload.m_frame.m_y);
    if(payload.m_frame.get_size() != frame.get_size()) {
      if(payload.m_layout_grid) {
        fit_layout_grid(*payload.m_layout_grid, payload.m_frame.get_size());
      } else {
        payload.m_layout_grid = make_initial_grid(
          payload, document.get_resources().m_grid_defaults);
      }
    }
    transaction.replace_payload(id, std::move(payload));
    if(!is_unit(*scale)) {
      for(auto& child : children) {
        translate_node(document, transaction, child, offset);
      }
    }
  }

  NodeId find_top_level(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    while(node) {
      if(node->m_parent.is_nil()) {
        if(document.is_on_canvas(node->get_id())) {
          return node->get_id();
        }
        return NodeId();
      }
      auto& parent = document.get(node->m_parent);
      if(parent.get_artboard()) {
        return node->get_id();
      }
      node = &parent;
    }
    return NodeId();
  }

  NodeId choose_artboard(const Document& document, const NodeId& parent,
      const Rect& bounds, const std::vector<NodeId>& candidates,
      std::optional<Point> pointer) {
    if(candidates.size() == 1) {
      return candidates.front();
    } else if(candidates.empty()) {
      return NodeId();
    } else if(std::find(candidates.begin(), candidates.end(), parent) !=
        candidates.end()) {
      return parent;
    }
    if(pointer) {
      auto& canvas = document.get_canvas();
      for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
        auto artboard = document.get(*i).get_artboard();
        if(artboard && artboard->m_frame.contains(*pointer)) {
          if(overlaps(bounds, artboard->m_frame)) {
            return *i;
          }
          break;
        }
      }
    }
    return candidates.back();
  }
}

void Rellu::set_transform(const Document& document, Transaction& transaction,
    const NodeId& id, const Matrix& transform) {
  auto& node = document.get(id);
  if(auto reference = node.get_reference()) {
    auto payload = *reference;
    payload.m_transform = transform;
    transaction.replace_payload(id, std::move(payload));
  } else if(!node.get_artboard()) {
    auto header = node.m_header;
    header.m_transform = transform;
    transaction.replace_header(std::move(header));
  }
}

void Rellu::translate_node(const Document& document,
    Transaction& transaction, const NodeId& id, Point delta) {
  if(delta == Point(0, 0)) {
    return;
  }
  auto& node = document.get(id);
  if(auto artboard = node.get_artboard()) {
    auto payload = *artboard;
    payload.m_frame.m_x += delta.m_x;
    payload.m_frame.m_y += delta.m_y;
    transaction.replace_payload(id, std::move(payload));
    return;
  }
  auto inverse = invert(get_parent_transform(document, id));
  if(!inverse) {
    return;
  }
  auto transform = get_transform(document, id);
  transform.m_tx += inverse->m_a * delta.m_x + inverse->m_c * delta.m_y;
  transform.m_ty += inverse->m_b * delta.m_x + inverse->m_d * delta.m_y;
  set_transform(document, transaction, id, transform);
}

void Rellu::transform_node(const Document& document,
    Transaction& transaction, const NodeId& id, const Matrix& mapping,
    const TextMeasurer& measurer) {
  if(document.get(id).get_artboard()) {
    transform_artboard(document, transaction, id, mapping);
    return;
  }
  auto parent = get_parent_transform(document, id);
  auto inverse_parent = invert(parent);
  if(!inverse_parent) {
    return;
  }
  auto relative = *inverse_parent * mapping * parent;
  auto transform = get_transform(document, id);
  auto composed = relative * transform;
  if(auto inverse = invert(transform)) {
    reshape(document, transaction, id, transform,
      *inverse * relative * transform, composed, measurer);
  } else {
    set_transform(document, transaction, id, composed);
  }
}

void Rellu::update_artboard_contents(
    const Document& document, Transaction& transaction, const NodeId& id) {
  auto node = document.find(id);
  if(!node || !node->get_artboard()) {
    return;
  }
  auto frame = node->get_artboard()->m_frame;
  auto captured = std::vector<NodeId>();
  for(auto& child : document.get_canvas()) {
    if(!document.get(child).get_artboard()) {
      if(auto bounds = get_world_bounds(document, child);
          bounds && overlaps(*bounds, frame)) {
        captured.push_back(child);
      }
    }
  }
  auto released = std::vector<std::pair<NodeId, Rect>>();
  for(auto& child : node->m_children) {
    if(auto bounds = get_world_bounds(document, child);
        bounds && !overlaps(*bounds, frame)) {
      released.emplace_back(child, *bounds);
    }
  }
  if(!captured.empty()) {
    auto count = document.get(id).m_children.size();
    ReparentCommand(std::move(captured), id, count).apply(
      document, transaction);
  }
  for(auto& [child, bounds] : released) {
    auto parent = NodeId();
    auto& canvas = document.get_canvas();
    for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
      auto artboard = document.get(*i).get_artboard();
      if(*i != id && artboard && overlaps(bounds, artboard->m_frame)) {
        parent = *i;
        break;
      }
    }
    auto count = document.get_children(parent).size();
    ReparentCommand(std::vector{child}, parent, count).apply(
      document, transaction);
  }
}

NodeId Rellu::find_artboard_parent(const Document& document,
    const NodeId& id, std::optional<Point> pointer,
    const TextMeasurer& measurer) {
  auto& node = document.get(id);
  auto outline =
    get_world_bounds(document, id, BoundsType::GEOMETRY, measurer);
  if(!outline || outline->m_width == 0 && outline->m_height == 0) {
    return node.m_parent;
  }
  auto bounds = get_world_bounds(document, id, BoundsType::VISUAL, measurer);
  if(!bounds) {
    return node.m_parent;
  }
  auto candidates = std::vector<NodeId>();
  for(auto& child : document.get_canvas()) {
    auto artboard = document.get(child).get_artboard();
    if(child != id && artboard && overlaps(*bounds, artboard->m_frame)) {
      candidates.push_back(child);
    }
  }
  return choose_artboard(document, node.m_parent, *bounds, candidates,
    pointer);
}

std::vector<NodeId> Rellu::get_settled_nodes(
    const Document& document, const ChangeSet& changes) {
  constexpr auto MASK = ChangeMask::VISIBILITY | ChangeMask::TRANSFORM |
    ChangeMask::GEOMETRY | ChangeMask::TEXT | ChangeMask::CHILDREN |
    ChangeMask::REFERENCE;
  auto nodes = std::vector<NodeId>();
  auto add = [&] (const NodeId& id) {
    auto node = document.find(id);
    if(!node || std::find(nodes.begin(), nodes.end(), id) != nodes.end()) {
      return;
    }
    if(node->m_parent.is_nil() ? document.is_on_canvas(id) :
        static_cast<bool>(document.get(node->m_parent).get_artboard())) {
      nodes.push_back(id);
    }
  };
  for(auto& id : changes.m_inserted) {
    add(id);
  }
  for(auto& id : changes.m_moved) {
    add(id);
  }
  for(auto& change : changes.m_modified) {
    if(is_set(change.m_mask, MASK)) {
      add(change.m_id);
    }
  }
  return nodes;
}

std::vector<NodeId> Rellu::settle_nodes(const Document& document,
    Transaction& transaction, const std::vector<NodeId>& nodes,
    std::optional<Point> pointer, const TextMeasurer& measurer,
    const SettleExemption& exemption) {
  auto targets = std::vector<std::pair<NodeId, NodeId>>();
  auto is_listed = [&] (const NodeId& id) {
    return std::any_of(targets.begin(), targets.end(),
      [&] (const auto& target) { return target.first == id; });
  };
  auto artboards = std::vector<NodeId>();
  auto checked = std::vector<NodeId>();
  for(auto& id : nodes) {
    auto top = find_top_level(document, id);
    if(top.is_nil() ||
        std::find(checked.begin(), checked.end(), top) != checked.end()) {
      continue;
    }
    checked.push_back(top);
    if(document.get(top).get_artboard()) {
      artboards.push_back(top);
      continue;
    }
    auto parent = find_artboard_parent(document, top, pointer, measurer);
    auto& current = document.get(top).m_parent;
    if(parent != current && !(parent.is_nil() && exemption &&
        exemption(document, top))) {
      targets.emplace_back(top, parent);
    }
  }
  if(!artboards.empty()) {
    for(auto& child : document.get_canvas()) {
      if(document.get(child).get_artboard() || is_listed(child)) {
        continue;
      }
      auto bounds =
        get_world_bounds(document, child, BoundsType::VISUAL, measurer);
      if(!bounds) {
        continue;
      }
      auto candidates = std::vector<NodeId>();
      for(auto& artboard : artboards) {
        if(overlaps(*bounds, document.get(artboard).get_artboard()->m_frame)) {
          candidates.push_back(artboard);
        }
      }
      sort_by_z_order(document, candidates);
      auto parent =
        choose_artboard(document, NodeId(), *bounds, candidates, pointer);
      if(!parent.is_nil()) {
        targets.emplace_back(child, parent);
      }
    }
  }
  auto moved = std::vector<NodeId>();
  for(auto& target : targets) {
    moved.push_back(target.first);
  }
  sort_by_z_order(document, moved);
  for(auto& id : moved) {
    auto i = std::find_if(targets.begin(), targets.end(),
      [&] (const auto& target) { return target.first == id; });
    auto count = document.get_children(i->second).size();
    ReparentCommand(std::vector{id}, i->second, count).apply(
      document, transaction);
  }
  return moved;
}

TranslateCommand::TranslateCommand(std::vector<NodeId> nodes, Point delta)
  : TranslateCommand(std::move(nodes), delta, "Move") {}

TranslateCommand::TranslateCommand(
  std::vector<NodeId> nodes, Point delta, std::string label) noexcept
  : m_nodes(std::move(nodes)),
    m_delta(delta),
    m_label(std::move(label)) {}

const std::vector<NodeId>& TranslateCommand::get_nodes() const {
  return m_nodes;
}

Point TranslateCommand::get_delta() const {
  return m_delta;
}

std::string TranslateCommand::get_label() const {
  return m_label;
}

void TranslateCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    translate_node(document, transaction, id, m_delta);
  }
}

bool TranslateCommand::merge(const Command& command) {
  auto translation = dynamic_cast<const TranslateCommand*>(&command);
  if(!translation || translation->m_label != m_label ||
      translation->m_nodes != m_nodes) {
    return false;
  }
  m_delta = m_delta + translation->m_delta;
  return true;
}

TransformCommand::TransformCommand(std::vector<NodeId> nodes,
  const Matrix& mapping, std::string label) noexcept
  : TransformCommand(
      std::move(nodes), mapping, std::move(label), TransformMode::RESIZE) {}

TransformCommand::TransformCommand(std::vector<NodeId> nodes,
  const Matrix& mapping, std::string label, TransformMode mode) noexcept
  : TransformCommand(std::move(nodes), mapping, std::move(label), mode,
      ResizeRule::PROPORTIONAL, estimate_text_bounds) {}

TransformCommand::TransformCommand(std::vector<NodeId> nodes,
  const Matrix& mapping, std::string label, TransformMode mode,
  ResizeRule rule, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_mapping(mapping),
    m_label(std::move(label)),
    m_mode(mode),
    m_rule(rule),
    m_measurer(std::move(measurer)) {}

const std::vector<NodeId>& TransformCommand::get_nodes() const {
  return m_nodes;
}

const Matrix& TransformCommand::get_mapping() const {
  return m_mapping;
}

ResizeRule TransformCommand::get_rule() const {
  return m_rule;
}

const std::vector<double>& TransformCommand::get_rotations() const {
  return m_rotations;
}

void TransformCommand::set_rotations(std::vector<double> rotations) {
  m_rotations = std::move(rotations);
}

const std::vector<Matrix>& TransformCommand::get_adjustments() const {
  return m_adjustments;
}

void TransformCommand::set_adjustments(std::vector<Matrix> adjustments) {
  m_adjustments = std::move(adjustments);
}

std::string TransformCommand::get_label() const {
  return m_label;
}

void TransformCommand::apply(
    const Document& document, Transaction& transaction) {
  auto nodes = get_top_level(document, m_nodes);
  auto is_artboard = [&] (const NodeId& id) {
    return static_cast<bool>(document.get(id).get_artboard());
  };
  if(m_mode == TransformMode::SETTLE) {
    for(auto& id : nodes) {
      if(is_artboard(id)) {
        update_artboard_contents(document, transaction, id);
      }
    }
    return;
  }
  if(m_mode == TransformMode::ROTATE) {
    for(auto& id : nodes) {
      if(is_artboard(id)) {
        continue;
      }
      auto mapping = m_mapping;
      auto index = static_cast<std::size_t>(
        std::find(m_nodes.begin(), m_nodes.end(), id) - m_nodes.begin());
      if(index < m_adjustments.size()) {
        mapping = mapping * m_adjustments[index];
      }
      auto parent = get_parent_transform(document, id);
      if(auto inverse = invert(parent)) {
        set_transform(document, transaction, id,
          *inverse * mapping * parent * get_transform(document, id));
      }
    }
    apply_rotations(document, transaction);
    return;
  }
  auto sizes = std::vector<std::pair<NodeId, Size>>();
  for(auto& id : nodes) {
    if(auto artboard = document.get(id).get_artboard()) {
      sizes.emplace_back(id, artboard->m_frame.get_size());
    }
  }
  auto records = std::vector<RotationRecord>();
  if(m_rotations.empty()) {
    for(auto& id : nodes) {
      record_rotations(document, id, true, records);
    }
  }
  resize_nodes(document, transaction, nodes, m_mapping, m_rule,
    m_mode == TransformMode::DRAG, m_measurer);
  for(auto& record : records) {
    auto node = document.find(record.m_id);
    if(!node || (!record.m_is_resized &&
        document.get_nodes().get_record(record.m_id) == record.m_node)) {
      continue;
    }
    auto header = node->m_header;
    header.m_rotation =
      finalize_rotation(record.m_rotation, header.m_transform);
    if(header != node->m_header) {
      transaction.replace_header(std::move(header));
    }
  }
  apply_rotations(document, transaction);
  if(m_mode == TransformMode::RESIZE) {
    for(auto& [id, size] : sizes) {
      if(document.get(id).get_artboard()->m_frame.get_size() != size) {
        update_artboard_contents(document, transaction, id);
      }
    }
  }
}

void TransformCommand::apply_rotations(
    const Document& document, Transaction& transaction) const {
  for(auto i = std::size_t(0); i < m_rotations.size() && i < m_nodes.size();
      ++i) {
    auto node = document.find(m_nodes[i]);
    if(!node || node->get_artboard() || node->get_reference()) {
      continue;
    }
    auto header = node->m_header;
    header.m_rotation = m_rotations[i];
    if(header != node->m_header) {
      transaction.replace_header(std::move(header));
    }
  }
}

bool TransformCommand::merge(const Command& command) {
  auto transformation = dynamic_cast<const TransformCommand*>(&command);
  if(!transformation || transformation->m_label != m_label ||
      transformation->m_nodes != m_nodes) {
    return false;
  }
  m_mapping = transformation->m_mapping * m_mapping;
  if(!transformation->m_rotations.empty()) {
    m_rotations = transformation->m_rotations;
  }
  if(m_adjustments.empty()) {
    m_adjustments = transformation->m_adjustments;
  } else {
    for(auto i = std::size_t(0); i < m_adjustments.size() &&
        i < transformation->m_adjustments.size(); ++i) {
      m_adjustments[i] = transformation->m_adjustments[i] * m_adjustments[i];
    }
  }
  return true;
}

RotationDrag::RotationDrag(
    std::vector<double> rotations, std::vector<double> angles)
    : m_initial(rotations),
      m_angles(std::move(angles)),
      m_rotations(std::move(rotations)),
      m_unsigned(0),
      m_previous(0),
      m_turn(0),
      m_sign(1) {
  for(auto rotation : m_initial) {
    m_fractions.push_back(rotation - round_half_up(rotation));
  }
  if(m_initial.size() == 1) {
    auto rotation = m_initial.front();
    m_unsigned = std::fmod(rotation + 360, 360);
    if(rotation < 0) {
      m_sign = -1;
    }
  }
}

void RotationDrag::update(double angle, bool is_snapped) {
  auto previous = std::fmod(m_unsigned + m_previous, 360);
  auto current = std::fmod(m_unsigned + angle, 360);
  if(previous >= 270 && previous < 360 && current > 0 && current <= 90) {
    m_sign = 1;
  } else if(previous >= 0 && previous <= 90 && current >= 270 &&
      current < 360) {
    m_sign = -1;
  }
  m_previous = angle;
  auto turn = angle;
  if(is_snapped) {
    turn = 15 * round_half_up((turn + m_unsigned) / 15) - m_unsigned;
  }
  m_turn = std::fmod(round_half_up(turn), 360);
  for(auto i = std::size_t(0); i != m_rotations.size(); ++i) {
    auto rotation = m_rotations[i];
    auto angle_after = m_turn - m_fractions[i];
    if(i < m_angles.size()) {
      angle_after += m_angles[i];
    }
    auto radians = angle_after * std::numbers::pi / 180;
    auto derived = round_half_up(
      fit_rotation(rotation, make_rotation(radians)));
    auto result = std::fmod(derived + 360 * m_sign, 360);
    auto is_turn = std::abs(rotation) == 360;
    auto is_closing = std::abs(rotation) >= 270 && result == 0;
    if(is_turn && is_closing) {
      result = rotation;
    } else if(is_closing) {
      result = 360 * m_sign;
    } else if(is_turn && rotation < 0 && derived <= 90) {
      result = rotation + derived;
    } else if(is_turn && rotation > 0 && derived >= 270) {
      result = derived;
    }
    m_rotations[i] = result == 0 ? 0 : result;
  }
}

double RotationDrag::get_turn() const {
  return m_turn;
}

const std::vector<double>& RotationDrag::get_fractions() const {
  return m_fractions;
}

const std::vector<double>& RotationDrag::get_rotations() const {
  return m_rotations;
}

std::unique_ptr<TranslateCommand> Rellu::make_nudge_command(
    std::vector<NodeId> nodes, Point delta) {
  return std::make_unique<TranslateCommand>(std::move(nodes), delta, "Nudge");
}

Point Rellu::get_large_nudge(const Document& document,
    const NodeId& artboard, Point origin, Point direction) {
  auto distance = LARGE_NUDGE;
  auto node = document.find(artboard);
  auto board = node ? node->get_artboard() : nullptr;
  auto grid = board && board->m_layout_grid ? &*board->m_layout_grid : nullptr;
  if(grid && grid->m_is_visible && grid->m_type == GridType::SQUARE) {
    auto is_x = direction.m_x != 0;
    auto spacing =
      is_x ? grid->m_square_size.m_width : grid->m_square_size.m_height;
    if(spacing > 0) {
      auto local = origin - board->m_frame.get_top_left();
      auto position = is_x ? local.m_x : local.m_y;
      auto remainder =
        std::fmod(std::fmod(position, spacing) + spacing, spacing);
      distance = remainder;
      if(direction.m_x + direction.m_y > 0) {
        distance = spacing - remainder;
      }
      if(distance < GRID_TOLERANCE) {
        distance += spacing;
      }
    }
  }
  return direction * distance;
}

bool Rellu::is_within_canvas(const Rect& bounds) {
  auto& canvas = CANVAS_BOUNDS;
  if(canvas.contains(bounds)) {
    return true;
  } else if(canvas.m_width < bounds.m_width ||
      canvas.m_height < bounds.m_height) {
    auto overlap = intersect(canvas, bounds);
    return overlap.m_width >= CANVAS_MARGIN &&
      overlap.m_height >= CANVAS_MARGIN;
  }
  return false;
}

Point Rellu::keep_on_canvas(const Rect& bounds, Point delta) {
  auto moved = Rect(bounds.m_x + delta.m_x, bounds.m_y + delta.m_y,
    bounds.m_width, bounds.m_height);
  if(is_within_canvas(moved)) {
    return delta;
  }
  auto& canvas = CANVAS_BOUNDS;
  auto left = canvas.get_left();
  auto right = canvas.get_right();
  auto top = canvas.get_top();
  auto bottom = canvas.get_bottom();
  if(canvas.m_width < moved.m_width || canvas.m_height < moved.m_height) {
    left -= moved.m_width - CANVAS_MARGIN;
    right -= CANVAS_MARGIN;
    top -= moved.m_height - CANVAS_MARGIN;
    bottom -= CANVAS_MARGIN;
  } else {
    right -= moved.m_width;
    bottom -= moved.m_height;
  }
  auto x = std::max(std::min(right, moved.m_x), left);
  auto y = std::max(std::min(bottom, moved.m_y), top);
  return Point(x - bounds.m_x, y - bounds.m_y);
}

std::unique_ptr<TransformCommand> Rellu::make_resize_command(
    std::vector<NodeId> nodes, const Rect& from, const Rect& to,
    const TextMeasurer& measurer) {
  return std::make_unique<TransformCommand>(std::move(nodes),
    make_rect_mapping(from, to), "Resize", TransformMode::RESIZE,
    ResizeRule::RESPONSIVE, measurer);
}

std::unique_ptr<TransformCommand> Rellu::make_bounds_command(
    const Document& document, const NodeId& id, const Rect& bounds,
    const TextMeasurer& measurer) {
  auto local = get_local_bounds(document, id, BoundsType::GEOMETRY, measurer);
  if(!local) {
    return nullptr;
  }
  auto world = get_world_transform(document, id);
  auto inverse = invert(world);
  if(!inverse) {
    return nullptr;
  }
  return std::make_unique<TransformCommand>(std::vector{id},
    world * make_rect_mapping(*local, bounds) * *inverse, "Resize",
    TransformMode::RESIZE, ResizeRule::RESPONSIVE, measurer);
}

std::unique_ptr<TransformCommand> Rellu::make_rotate_command(
    std::vector<NodeId> nodes, double angle, Point center,
    std::vector<double> rotations) {
  auto command = std::make_unique<TransformCommand>(std::move(nodes),
    make_rotation(angle, center), "Rotate", TransformMode::ROTATE);
  command->set_rotations(std::move(rotations));
  return command;
}

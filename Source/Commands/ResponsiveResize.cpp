#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/ResponsiveResize.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_SIZE = 1.0000005e-5;
  constexpr auto TOLERANCE = 1e-9;
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto NORMAL_MARGIN = 0.15;
  constexpr auto CENTERED_DEVIATION = 1.0;
  constexpr auto CENTERED_MARGIN = 0.25;
  constexpr auto TEXT_AREA_PROPORTION = 0.66;
  constexpr auto THREE_EDGE_MARGIN = 0.05;
  constexpr auto FOURTH_EDGE_MARGIN = 0.33;
  constexpr auto CONTAINED_MARGIN = 0.2;
  constexpr auto PIXEL_TOLERANCE = 0.01;

  struct Context {
    const Document* m_document;
    Transaction* m_transaction;
    const TextMeasurer* m_measurer;
    ResizeRule m_rule;
    bool m_is_pixel_aligned = false;
    std::unordered_map<NodeId, Constraints> m_constraints =
      std::unordered_map<NodeId, Constraints>();
    std::unordered_set<NodeId> m_materialized = std::unordered_set<NodeId>();
    std::unordered_map<NodeId, std::optional<Rect>> m_placements =
      std::unordered_map<NodeId, std::optional<Rect>>();
  };

  struct Span {
    double m_start;
    double m_length;

    double get_end() const {
      return m_start + m_length;
    }
  };

  struct Pins {
    bool m_start;
    bool m_fixed;
    bool m_end;
  };

  struct Requirements {
    bool m_width;
    bool m_height;
  };

  enum class Fit : unsigned char {
    CONTAIN,
    CONSTRAINED_WIDTH,
    CONSTRAINED_HEIGHT
  };

  bool is_near(double left, double right) {
    return std::abs(left - right) <= TOLERANCE;
  }

  bool is_near(const Rect& left, const Rect& right) {
    return is_near(left.m_x, right.m_x) && is_near(left.m_y, right.m_y) &&
      is_near(left.m_width, right.m_width) &&
      is_near(left.m_height, right.m_height);
  }

  std::optional<Point> get_axis_scale(const Matrix& matrix) {
    auto magnitude = std::abs(matrix.m_a) + std::abs(matrix.m_d);
    if(!(std::abs(matrix.m_a) > 0) || !(std::abs(matrix.m_d) > 0) ||
        std::abs(matrix.m_b) > TOLERANCE * magnitude ||
        std::abs(matrix.m_c) > TOLERANCE * magnitude) {
      return std::nullopt;
    }
    return Point(matrix.m_a, matrix.m_d);
  }

  bool is_translation(const Matrix& matrix) {
    return is_near(matrix.m_a, 1) && is_near(matrix.m_b, 0) &&
      is_near(matrix.m_c, 0) && is_near(matrix.m_d, 1);
  }

  bool is_identity(const Matrix& matrix) {
    return is_translation(matrix) && is_near(matrix.m_tx, 0) &&
      is_near(matrix.m_ty, 0);
  }

  Rect normalize(const Rect& rect) {
    auto x = std::min(rect.m_x, rect.m_x + rect.m_width);
    auto y = std::min(rect.m_y, rect.m_y + rect.m_height);
    return Rect(x, y, std::abs(rect.m_width), std::abs(rect.m_height));
  }

  Matrix make_reflection(Point center, bool is_horizontal, bool is_vertical) {
    return make_translation(center.m_x, center.m_y) *
      make_scale(is_horizontal ? -1 : 1, is_vertical ? -1 : 1) *
      make_translation(-center.m_x, -center.m_y);
  }

  Rect shrink(const Rect& rect, const Margins& margins) {
    auto left = std::max(margins.m_left, 0.0);
    auto top = std::max(margins.m_top, 0.0);
    auto right = std::max(margins.m_right, 0.0);
    auto bottom = std::max(margins.m_bottom, 0.0);
    return Rect(rect.m_x + left, rect.m_y + top,
      rect.m_width - left - right, rect.m_height - top - bottom);
  }

  const Node& resolve(const Document& document, const NodeId& id) {
    auto& node = document.get(id);
    if(node.get_reference()) {
      if(auto resolution = resolve_reference(document.get_nodes(), id)) {
        if(auto source = document.find(resolution->m_source)) {
          return *source;
        }
      }
    }
    return node;
  }

  const Constraints& get_stored(const Document& document, const NodeId& id) {
    auto& node = document.get(id);
    if(node.get_reference() && node.m_header.m_constraints == Constraints()) {
      return resolve(document, id).m_header.m_constraints;
    }
    return node.m_header.m_constraints;
  }

  bool is_instance(const Node& node) {
    auto group = node.get_group();
    return group && group->m_component_instance;
  }

  bool is_compound(const Node& node) {
    auto shape = node.get_shape();
    return shape && std::holds_alternative<CompoundShape>(shape->m_geometry);
  }

  bool is_plain_container(const Node& node) {
    auto group = node.get_group();
    return group && !group->m_component_instance && !group->m_repeat_grid &&
      !group->m_scroll_group;
  }

  bool is_unconstrained(const Node& node) {
    if(auto group = node.get_group()) {
      return group->m_repeat_grid || group->m_scroll_group || group->m_mask;
    }
    return is_compound(node);
  }

  bool has_background(const Group& group) {
    return (group.m_stack && group.m_stack->m_has_background) ||
      (group.m_padding && group.m_padding->m_has_background);
  }

  Margins get_padding(const Group& group) {
    if(group.m_stack && group.m_stack->m_has_background) {
      return group.m_stack->m_padding;
    } else if(group.m_padding && group.m_padding->m_has_background) {
      return group.m_padding->m_padding;
    }
    return Margins();
  }

  bool is_background(const Document& document, const NodeId& id) {
    auto& parent = document.get(id).m_parent;
    if(parent.is_nil()) {
      return false;
    }
    auto group = resolve(document, parent).get_group();
    auto& children = document.get(parent).m_children;
    return group && has_background(*group) && !children.empty() &&
      children.front() == id;
  }

  std::optional<Orientation> get_stack_orientation(
      const Document& document, const NodeId& id) {
    if(id.is_nil()) {
      return std::nullopt;
    }
    auto group = resolve(document, id).get_group();
    if(!group || !group->m_stack) {
      return std::nullopt;
    }
    return group->m_stack->m_orientation;
  }

  bool is_inside_instance(const Document& document, const NodeId& id) {
    auto current = document.get(id).m_parent;
    for(auto depth = 0; !current.is_nil() && depth != MAXIMUM_DEPTH;
        ++depth) {
      if(is_instance(resolve(document, current))) {
        return true;
      }
      current = document.get(current).m_parent;
    }
    return false;
  }

  bool is_writable(const Document& document, const NodeId& id) {
    auto& node = document.get(id);
    if(node.get_reference()) {
      return false;
    }
    auto instance = find_instance(document, node.m_parent);
    if(instance.is_nil()) {
      return true;
    }
    auto role = get_instance_role(document.get(instance));
    return role && role->m_is_main;
  }

  Requirements get_requirements(const Node& node) {
    if(node.m_header.m_transform_3d) {
      return Requirements(true, true);
    }
    if(auto group = node.get_group()) {
      if(group->m_scroll_group) {
        auto direction = group->m_scroll_group->m_direction;
        return Requirements(direction == ScrollDirection::VERTICAL,
          direction == ScrollDirection::HORIZONTAL);
      } else if(group->m_stack) {
        auto orientation = group->m_stack->m_orientation;
        return Requirements(orientation == Orientation::HORIZONTAL,
          orientation == Orientation::VERTICAL);
      }
    } else if(auto text = node.get_text()) {
      return Requirements(false,
        std::holds_alternative<AutoHeightFrame>(text->m_frame));
    }
    return Requirements(false, false);
  }

  void enforce(Constraints& constraints, Requirements requirements) {
    if(requirements.m_width) {
      constraints.m_width = true;
    }
    if(requirements.m_height) {
      constraints.m_height = true;
    }
    if(constraints.m_width && constraints.m_left && constraints.m_right) {
      constraints.m_left = false;
      constraints.m_right = false;
    }
    if(constraints.m_height && constraints.m_top && constraints.m_bottom) {
      constraints.m_top = false;
      constraints.m_bottom = false;
    }
  }

  Constraints infer(const Node& node, Constraints constraints,
      bool is_background, const Rect& bounds, const Rect& frame) {
    auto text = node.get_text();
    auto is_point_text =
      text && std::holds_alternative<PositionedFrame>(text->m_frame);
    auto is_area_text =
      text && std::holds_alternative<AreaFrame>(text->m_frame);
    auto left = bounds.m_x - frame.m_x;
    auto right = frame.get_right() - bounds.get_right();
    auto top = bounds.m_y - frame.m_y;
    auto bottom = frame.get_bottom() - bounds.get_bottom();
    auto width = frame.m_width;
    auto height = frame.m_height;
    constraints.m_left = left < width * NORMAL_MARGIN;
    constraints.m_right = right < width * NORMAL_MARGIN;
    constraints.m_top = top < height * NORMAL_MARGIN;
    constraints.m_bottom = bottom < height * NORMAL_MARGIN;
    constraints.m_width = false;
    constraints.m_height = false;
    if(!is_plain_container(node) && !is_point_text) {
      if(std::abs(left - right) <= CENTERED_DEVIATION &&
          left < width * CENTERED_MARGIN) {
        constraints.m_left = true;
        constraints.m_right = true;
      }
      if(std::abs(top - bottom) <= CENTERED_DEVIATION &&
          top < height * CENTERED_MARGIN) {
        constraints.m_top = true;
        constraints.m_bottom = true;
      }
    }
    if(is_area_text) {
      if(bounds.m_width > width * TEXT_AREA_PROPORTION) {
        constraints.m_left = true;
        constraints.m_right = true;
      }
      if(bounds.m_height > height * TEXT_AREA_PROPORTION) {
        constraints.m_top = true;
        constraints.m_bottom = true;
      }
    }
    auto is_flush = [&] (double first, double second, double third,
        double fourth, double extent) {
      return first <= extent * FOURTH_EDGE_MARGIN &&
        second <= THREE_EDGE_MARGIN && third <= THREE_EDGE_MARGIN &&
        fourth <= THREE_EDGE_MARGIN;
    };
    auto is_filling = is_flush(top, bottom, left, right, height) ||
      is_flush(bottom, top, left, right, height) ||
      is_flush(left, top, bottom, right, width) ||
      is_flush(right, top, bottom, left, width);
    if((!is_point_text && is_filling) || is_background) {
      constraints.m_left = true;
      constraints.m_right = true;
      constraints.m_top = true;
      constraints.m_bottom = true;
    }
    if(!constraints.m_left &&
        left + bounds.m_width <= width * CONTAINED_MARGIN) {
      constraints.m_left = true;
    }
    if(!constraints.m_top &&
        top + bounds.m_height <= height * CONTAINED_MARGIN) {
      constraints.m_top = true;
    }
    if(!constraints.m_right && width * (1 - CONTAINED_MARGIN) <= left) {
      constraints.m_right = true;
    }
    if(!constraints.m_bottom && height * (1 - CONTAINED_MARGIN) <= top) {
      constraints.m_bottom = true;
    }
    constraints.m_width = !(constraints.m_left && constraints.m_right);
    constraints.m_height = !(constraints.m_top && constraints.m_bottom);
    return constraints;
  }

  Constraints compute(const Document& document, const NodeId& id,
      const Rect& bounds, const Rect& frame) {
    auto& node = resolve(document, id);
    auto constraints = get_stored(document, id);
    if(!constraints.m_is_custom && !node.get_artboard()) {
      constraints = infer(
        node, constraints, is_background(document, id), bounds, frame);
    }
    enforce(constraints, get_requirements(node));
    return constraints;
  }

  Constraints get_constraints(const Context& context, const NodeId& id) {
    if(auto entry = context.m_constraints.find(id);
        entry != context.m_constraints.end()) {
      return entry->second;
    }
    return get_stored(*context.m_document, id);
  }

  void store(Context& context, const NodeId& id,
      const Constraints& constraints) {
    context.m_constraints.insert_or_assign(id, constraints);
    auto& document = *context.m_document;
    if(!context.m_transaction || !is_writable(document, id)) {
      return;
    }
    auto& node = document.get(id);
    if(node.m_header.m_constraints == constraints) {
      return;
    }
    auto header = node.m_header;
    header.m_constraints = constraints;
    context.m_transaction->replace_header(std::move(header));
  }

  std::optional<Rect> get_local(const Context& context, const NodeId& id) {
    return get_local_bounds(
      *context.m_document, id, BoundsType::GEOMETRY, *context.m_measurer);
  }

  std::optional<Rect> get_placement(Context& context, const NodeId& id) {
    if(auto entry = context.m_placements.find(id);
        entry != context.m_placements.end()) {
      return entry->second;
    }
    auto& document = *context.m_document;
    auto placement = get_bounds(document, id, get_transform(document, id),
      BoundsType::GEOMETRY, *context.m_measurer);
    context.m_placements.emplace(id, placement);
    return placement;
  }

  std::optional<Rect> get_content_frame(
      const Context& context, const NodeId& id) {
    auto& document = *context.m_document;
    if(auto artboard = document.get(id).get_artboard()) {
      return Rect(0, 0, artboard->m_frame.m_width, artboard->m_frame.m_height);
    }
    auto bounds = get_local(context, id);
    if(!bounds) {
      return std::nullopt;
    }
    if(auto group = resolve(document, id).get_group()) {
      auto frame = shrink(*bounds, get_padding(*group));
      frame.m_width = std::max(frame.m_width, 0.0);
      frame.m_height = std::max(frame.m_height, 0.0);
      return frame;
    }
    return bounds;
  }

  void reset_descendants(Context& context, const NodeId& id, int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto children = context.m_document->get(id).m_children;
    for(auto& child : children) {
      auto constraints = get_constraints(context, child);
      if(!constraints.m_is_custom) {
        constraints.m_left = false;
        constraints.m_right = false;
        constraints.m_top = false;
        constraints.m_bottom = false;
        constraints.m_width = false;
        constraints.m_height = false;
        store(context, child, constraints);
      }
      reset_descendants(context, child, depth + 1);
    }
  }

  void record_node(Context& context, const NodeId& id, const Rect& bounds,
      const Rect& frame, int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto& document = *context.m_document;
    auto constraints = compute(document, id, bounds, frame);
    store(context, id, constraints);
    if(is_unconstrained(resolve(document, id))) {
      reset_descendants(context, id, depth + 1);
      return;
    }
    if(constraints.m_width && constraints.m_height &&
        !is_inside_instance(document, id)) {
      return;
    }
    auto local = get_local(context, id);
    if(!local) {
      return;
    }
    auto children = document.get(id).m_children;
    for(auto& child : children) {
      if(auto placement = get_placement(context, child)) {
        record_node(context, child, *placement, *local, depth + 1);
      }
    }
  }

  void prepare(Context& context, const NodeId& id, int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto& document = *context.m_document;
    auto constraints = get_constraints(context, id);
    constraints.m_is_custom = true;
    auto group = resolve(document, id).get_group();
    auto is_grid = group && group->m_repeat_grid;
    if(is_grid) {
      enforce(constraints, Requirements(true, true));
    }
    store(context, id, constraints);
    if(is_grid) {
      return;
    }
    auto children = document.get(id).m_children;
    for(auto& child : children) {
      prepare(context, child, depth + 1);
    }
  }

  void record_subtree(Context& context, const NodeId& id) {
    auto& document = *context.m_document;
    auto& node = resolve(document, id);
    auto children = document.get(id).m_children;
    if(is_unconstrained(node)) {
      reset_descendants(context, id, 0);
    } else if(auto frame = get_content_frame(context, id)) {
      for(auto& child : children) {
        if(auto placement = get_placement(context, child)) {
          record_node(context, child, *placement, *frame, 1);
        }
      }
    }
    if(is_inside_instance(document, id)) {
      prepare(context, id, 0);
    } else if(!is_unconstrained(node) && is_instance(node)) {
      for(auto& child : children) {
        prepare(context, child, 1);
      }
    }
  }

  double compute_scale(double original, double final) {
    if(original < MINIMUM_SIZE) {
      return 1;
    }
    return std::max(final, MINIMUM_SIZE) / original;
  }

  Rect scale_bounds(
      const Rect& bounds, const Rect& original, const Rect& final) {
    auto x = compute_scale(original.m_width, final.m_width);
    auto y = compute_scale(original.m_height, final.m_height);
    return Rect(final.m_x + (bounds.m_x - original.m_x) * x,
      final.m_y + (bounds.m_y - original.m_y) * y, bounds.m_width * x,
      bounds.m_height * y);
  }

  double clamp_minimum(double value, bool is_clamped, double minimum) {
    if(is_clamped) {
      return std::max(minimum, value);
    }
    return value;
  }

  double clamp_maximum(double value, bool is_clamped, double maximum) {
    if(is_clamped) {
      return std::min(maximum, value);
    }
    return value;
  }

  Span constrain(Span bounds, Span scaled, Span original, Span final,
      Pins pins, bool is_symbol) {
    auto result = scaled;
    if(!pins.m_start && !pins.m_fixed && !pins.m_end) {
      return result;
    }
    auto lower = final.m_start;
    auto upper = final.get_end();
    auto is_extended = false;
    if(bounds.m_start < original.m_start && scaled.m_start < final.m_start) {
      lower = scaled.m_start;
      is_extended = true;
    }
    if(bounds.get_end() > original.get_end() &&
        scaled.get_end() > final.get_end()) {
      upper = scaled.get_end();
      is_extended = true;
    }
    if(is_extended && !is_symbol) {
      auto growth = final.m_length - original.m_length;
      auto before = final.m_start - lower;
      auto total = before + (upper - final.get_end());
      if(growth > 0) {
        if(total > 0) {
          auto share = growth * (before / total);
          lower = std::min(final.m_start, lower + share);
          upper = std::max(final.get_end(), upper - (growth - share));
        }
      } else if(pins.m_fixed && bounds.m_length > upper - lower) {
        auto excess = (bounds.m_length - (upper - lower)) / 2;
        lower -= excess;
        upper += excess;
      }
    }
    if(pins.m_start) {
      result.m_start = clamp_minimum(
        bounds.m_start - (original.m_start - final.m_start), !is_symbol,
        lower);
    }
    if(pins.m_fixed) {
      result.m_length = bounds.m_length;
      if(!pins.m_start && !pins.m_end) {
        auto slack = scaled.m_length - result.m_length;
        auto room = original.m_length - bounds.m_length;
        if(room == 0 || room != room) {
          room = MINIMUM_SIZE;
        }
        auto offset = bounds.m_start - original.m_start;
        if(offset < 0 && is_symbol) {
          result.m_start =
            bounds.m_start - (original.m_start - final.m_start);
        } else if(offset + bounds.m_length >= original.m_length &&
            is_symbol) {
          result.m_start = final.get_end() -
            (original.get_end() - bounds.get_end()) - result.m_length;
        } else {
          auto ratio = 0.5;
          if(is_symbol || room > MINIMUM_SIZE || offset > MINIMUM_SIZE) {
            ratio = offset / room;
            if(ratio != ratio) {
              ratio = 0;
            }
          }
          auto position = clamp_maximum(result.m_start + slack * ratio,
            !is_symbol, upper - result.m_length);
          result.m_start = clamp_minimum(position, !is_symbol, lower);
        }
      }
    }
    if(pins.m_end) {
      auto margin = original.get_end() - bounds.get_end();
      if(pins.m_start) {
        result.m_length = clamp_minimum(final.m_length - margin -
          (result.m_start - final.m_start), !is_symbol, MINIMUM_SIZE);
      } else {
        result.m_start = clamp_minimum(
          final.get_end() - margin - result.m_length, !is_symbol, lower);
      }
    }
    if(!is_symbol) {
      auto overflow = result.get_end() - upper;
      if(overflow > 0) {
        if(!pins.m_fixed) {
          result.m_length = std::max(MINIMUM_SIZE, result.m_length - overflow);
          overflow = result.get_end() - upper;
        }
        if(overflow > 0) {
          auto widest = std::max(final.m_length, scaled.m_length);
          if(result.m_length > widest) {
            result.m_start = std::min(
              lower, final.m_start - 0.5 * (result.m_length - widest));
          } else {
            result.m_start = upper - result.m_length;
          }
        }
      }
    }
    return result;
  }

  Rect constrain(const Rect& bounds, const Rect& scaled, const Rect& original,
      const Rect& final, const Constraints& constraints, bool is_symbol) {
    auto horizontal = constrain(Span(bounds.m_x, bounds.m_width),
      Span(scaled.m_x, scaled.m_width), Span(original.m_x, original.m_width),
      Span(final.m_x, final.m_width), Pins(constraints.m_left,
        constraints.m_width, constraints.m_right), is_symbol);
    auto vertical = constrain(Span(bounds.m_y, bounds.m_height),
      Span(scaled.m_y, scaled.m_height),
      Span(original.m_y, original.m_height),
      Span(final.m_y, final.m_height), Pins(constraints.m_top,
        constraints.m_height, constraints.m_bottom), is_symbol);
    return Rect(horizontal.m_start, vertical.m_start, horizontal.m_length,
      vertical.m_length);
  }

  Size fit_aspect(Size size, Size ratio, Fit fit) {
    if(ratio.m_width == ratio.m_height) {
      auto side = 0.0;
      if(fit == Fit::CONSTRAINED_WIDTH) {
        side = size.m_width;
      } else if(fit == Fit::CONSTRAINED_HEIGHT) {
        side = size.m_height;
      } else {
        side = std::min(size.m_width, size.m_height);
      }
      return Size(side, side);
    } else if(ratio.m_width == 0) {
      return Size(0, size.m_height);
    } else if(ratio.m_height == 0) {
      return Size(size.m_width, 0);
    }
    auto width = ratio.m_width / ratio.m_height * size.m_height;
    auto height = ratio.m_height / ratio.m_width * size.m_width;
    if(fit == Fit::CONSTRAINED_WIDTH) {
      return Size(size.m_width, height);
    } else if(fit == Fit::CONSTRAINED_HEIGHT) {
      return Size(width, size.m_height);
    } else if(width < size.m_width) {
      return Size(width, size.m_height);
    }
    return Size(size.m_width, height);
  }

  Rect arrange(const Context& context, const NodeId& id, const Rect& bounds,
      const Rect& original, const Rect& final, bool is_symbol,
      std::optional<Orientation> stack) {
    auto& node = resolve(*context.m_document, id);
    auto scaled = scale_bounds(bounds, original, final);
    auto constraints = get_constraints(context, id);
    auto group = node.get_group();
    auto has_constraint = (constraints.m_is_enabled && (constraints.m_left ||
      constraints.m_right || constraints.m_top || constraints.m_bottom ||
      constraints.m_width || constraints.m_height)) ||
      (group && group->m_stack);
    if(stack == Orientation::HORIZONTAL) {
      constraints.m_width = true;
      constraints.m_left = false;
      constraints.m_right = false;
    } else if(stack == Orientation::VERTICAL) {
      constraints.m_height = true;
      constraints.m_top = false;
      constraints.m_bottom = false;
    }
    auto is_applied =
      has_constraint && context.m_rule == ResizeRule::RESPONSIVE;
    auto target = scaled;
    if(is_applied) {
      target = constrain(
        bounds, scaled, original, final, constraints, is_symbol);
    }
    if(auto& lock = node.m_header.m_aspect_lock) {
      auto ratio = is_applied ? bounds.get_size() : *lock;
      auto fit = Fit::CONTAIN;
      if(constraints.m_width) {
        fit = Fit::CONSTRAINED_WIDTH;
      } else if(constraints.m_height) {
        fit = Fit::CONSTRAINED_HEIGHT;
      }
      auto size = fit_aspect(target.get_size(), ratio, fit);
      target.m_width = size.m_width;
      target.m_height = size.m_height;
    }
    target.m_width = std::max(target.m_width, MINIMUM_SIZE);
    target.m_height = std::max(target.m_height, MINIMUM_SIZE);
    return target;
  }

  bool is_snappable(const Node& node) {
    if(auto shape = node.get_shape()) {
      return !std::holds_alternative<PathShape>(shape->m_geometry) &&
        !std::holds_alternative<CompoundShape>(shape->m_geometry);
    } else if(node.get_text()) {
      return true;
    } else if(auto group = node.get_group()) {
      return group->m_repeat_grid || group->m_scroll_group ||
        group->m_component_instance;
    }
    return false;
  }

  bool is_integral(double value) {
    return std::abs(value - std::round(value)) < PIXEL_TOLERANCE;
  }

  bool is_integral(const Rect& rect) {
    return is_integral(rect.m_x) && is_integral(rect.m_y) &&
      is_integral(rect.m_width) && is_integral(rect.m_height);
  }

  Rect snap(const Context& context, const NodeId& id, const Rect& bounds,
      const Rect& target, const Matrix& origin, const Matrix& world) {
    auto& node = resolve(*context.m_document, id);
    if(!context.m_is_pixel_aligned || !is_snappable(node) ||
        !is_integral(origin.map(bounds))) {
      return target;
    }
    auto local = get_local(context, id);
    auto inverse = invert(world);
    if(!local || !is_integral(local->m_width) ||
        !is_integral(local->m_height) || !inverse) {
      return target;
    }
    auto position = world.map(target.get_top_left());
    auto start = inverse->map(
      Point(std::round(position.m_x), std::round(position.m_y)));
    auto size = Size(std::round(target.m_width), std::round(target.m_height));
    if(!node.get_text()) {
      auto corner = world.map(target.get_bottom_right());
      auto end = inverse->map(
        Point(std::round(corner.m_x), std::round(corner.m_y)));
      size = Size(std::round(end.m_x - start.m_x),
        std::round(end.m_y - start.m_y));
    }
    return Rect(start.m_x, start.m_y, std::max(size.m_width, MINIMUM_SIZE),
      std::max(size.m_height, MINIMUM_SIZE));
  }

  Matrix to_world(const Document& document, const NodeId& id,
      const Matrix& relative) {
    auto parent = get_parent_transform(document, id);
    if(auto inverse = invert(parent)) {
      return parent * relative * *inverse;
    }
    return Matrix();
  }

  void place(Context& context, const NodeId& id, const Matrix& relative,
    const Matrix& origin, int depth);

  void layout_children(Context& context, const NodeId& id,
      const Rect& original, const Rect& final, const Matrix& origin,
      int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto& document = *context.m_document;
    auto group = resolve(document, id).get_group();
    auto is_symbol = group && group->m_component_instance;
    auto stack = get_stack_orientation(document, id);
    auto world = get_world_transform(document, id);
    auto children = document.get(id).m_children;
    if(group && has_background(*group) && !children.empty()) {
      children.erase(children.begin());
    }
    if(context.m_materialized.contains(id) &&
        context.m_rule != ResizeRule::PROPORTIONAL) {
      if(auto local = get_local(context, id)) {
        for(auto& child : children) {
          if(!context.m_constraints.contains(child)) {
            if(auto placement = get_placement(context, child)) {
              record_node(context, child, *placement, *local, depth);
            }
          }
        }
      }
    }
    for(auto& child : children) {
      auto bounds = get_placement(context, child);
      if(!bounds) {
        continue;
      }
      auto target = snap(context, child, *bounds, arrange(context, child,
        *bounds, original, final, is_symbol, stack), origin, world);
      if(is_near(target, *bounds)) {
        continue;
      }
      place(context, child, make_rect_mapping(*bounds, target), origin,
        depth + 1);
    }
  }

  void resize_artboard(
      Context& context, const NodeId& id, const Matrix& relative) {
    auto& document = *context.m_document;
    auto& node = document.get(id);
    auto scale = get_axis_scale(relative);
    if(!scale || !(scale->m_x > 0) || !(scale->m_y > 0)) {
      return;
    }
    auto payload = *node.get_artboard();
    auto frame = payload.m_frame;
    payload.m_frame = relative.map(frame);
    if(payload.m_frame == frame) {
      return;
    }
    if(payload.m_viewport_height &&
        *payload.m_viewport_height > payload.m_frame.m_height) {
      payload.m_viewport_height = payload.m_frame.m_height;
    }
    if(payload.m_layout_grid) {
      fit_layout_grid(*payload.m_layout_grid, payload.m_frame.get_size());
    }
    std::erase_if(payload.m_guides, [&] (const auto& guide) {
      auto extent = guide.m_orientation == Orientation::HORIZONTAL ?
        payload.m_frame.m_height : payload.m_frame.m_width;
      return guide.m_position < 1 || guide.m_position >= extent;
    });
    auto is_responsive = context.m_rule == ResizeRule::RESPONSIVE &&
      node.m_header.m_constraints.m_is_enabled;
    auto size = payload.m_frame.get_size();
    context.m_transaction->replace_payload(id, std::move(payload));
    if(is_responsive) {
      layout_children(context, id,
        Rect(0, 0, frame.m_width, frame.m_height),
        Rect(0, 0, size.m_width, size.m_height),
        make_translation(frame.m_x, frame.m_y), 0);
    }
  }

  void resize_group(Context& context, const NodeId& id, const Matrix& local,
      const Matrix& origin, int depth) {
    auto& document = *context.m_document;
    auto bounds = get_local(context, id);
    if(!bounds) {
      return;
    }
    auto target = normalize(local.map(*bounds));
    auto transform = get_transform(document, id);
    auto group_origin = origin * transform;
    auto shifted = transform * make_translation(
      target.m_x - bounds->m_x, target.m_y - bounds->m_y);
    if(shifted != transform) {
      set_transform(document, *context.m_transaction, id, shifted);
    }
    auto final = Rect(bounds->m_x, bounds->m_y, target.m_width,
      target.m_height);
    auto group = *document.get(id).get_group();
    if(group.m_mask) {
      auto matrix = make_rect_mapping(*bounds, final);
      for(auto& shape : group.m_mask->m_shapes) {
        shape.m_transform = matrix * shape.m_transform;
      }
      context.m_transaction->replace_payload(id, group);
    }
    auto padding = get_padding(group);
    layout_children(context, id, shrink(*bounds, padding),
      shrink(final, padding), group_origin, depth);
  }

  void resize_grid(Context& context, const NodeId& id, const Matrix& local) {
    auto& document = *context.m_document;
    auto bounds = get_local(context, id);
    if(!bounds || document.get(id).m_children.empty()) {
      return;
    }
    auto target = normalize(local.map(*bounds));
    auto transform = get_transform(document, id);
    auto shifted = transform * make_translation(
      target.m_x - bounds->m_x, target.m_y - bounds->m_y);
    if(shifted != transform) {
      set_transform(document, *context.m_transaction, id, shifted);
    }
    auto& grid = *document.get(id).get_group()->m_repeat_grid;
    auto size = Size(grid.m_size.m_width + target.m_width - bounds->m_width,
      grid.m_size.m_height + target.m_height - bounds->m_height);
    if(!is_near(size.m_width, grid.m_size.m_width) ||
        !is_near(size.m_height, grid.m_size.m_height)) {
      ResizeRepeatGridCommand(id, size).apply(
        document, *context.m_transaction);
    }
  }

  void resize_scroll(Context& context, const NodeId& id, const Matrix& local) {
    auto& document = *context.m_document;
    auto bounds = get_local(context, id);
    if(!bounds) {
      return;
    }
    auto target = normalize(local.map(*bounds));
    auto transform = get_transform(document, id);
    auto shifted = transform * make_translation(
      target.m_x - bounds->m_x, target.m_y - bounds->m_y);
    if(shifted != transform) {
      set_transform(document, *context.m_transaction, id, shifted);
    }
    auto group = *document.get(id).get_group();
    auto& scroll = *group.m_scroll_group;
    auto viewport = scroll.m_viewport;
    if(scroll.m_direction != ScrollDirection::VERTICAL) {
      viewport.m_width = std::max(
        0.0, viewport.m_width + target.m_width - bounds->m_width);
    }
    if(scroll.m_direction != ScrollDirection::HORIZONTAL) {
      viewport.m_height = std::max(
        0.0, viewport.m_height + target.m_height - bounds->m_height);
    }
    if(viewport != scroll.m_viewport) {
      scroll.m_viewport = viewport;
      context.m_transaction->replace_payload(id, std::move(group));
    }
  }

  void place(Context& context, const NodeId& id, const Matrix& relative,
      const Matrix& origin, int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto& document = *context.m_document;
    auto& transaction = *context.m_transaction;
    if(document.get(id).get_artboard()) {
      resize_artboard(context, id, relative);
      return;
    }
    if(is_identity(relative)) {
      return;
    }
    if(document.get(id).get_reference()) {
      if(is_translation(relative)) {
        set_transform(
          document, transaction, id, relative * get_transform(document, id));
        return;
      }
      materialize_reference(document, transaction, id);
      context.m_materialized.insert(id);
      if(document.get(id).get_reference()) {
        transform_node(document, transaction, id,
          to_world(document, id, relative), *context.m_measurer);
        return;
      }
    }
    auto& node = document.get(id);
    auto transform = get_transform(document, id);
    auto inverse = invert(transform);
    auto group = node.get_group();
    if(group && inverse && !node.m_header.m_transform_3d) {
      auto local = *inverse * relative * transform;
      if(auto scale = get_axis_scale(local);
          scale && scale->m_x > 0 && scale->m_y > 0) {
        if(group->m_repeat_grid) {
          resize_grid(context, id, local);
        } else if(group->m_scroll_group) {
          resize_scroll(context, id, local);
        } else {
          resize_group(context, id, local, origin, depth);
        }
        return;
      }
    }
    transform_node(document, transaction, id,
      to_world(document, id, relative), *context.m_measurer);
  }

  void flip_pins(Context& context, const NodeId& id, bool is_horizontal,
      bool is_vertical, bool is_eligible, int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto& document = *context.m_document;
    auto constraints = get_constraints(context, id);
    if(is_eligible && constraints.m_is_custom) {
      auto flipped = constraints;
      if(is_horizontal && flipped.m_left != flipped.m_right) {
        std::swap(flipped.m_left, flipped.m_right);
      }
      if(is_vertical && flipped.m_top != flipped.m_bottom) {
        std::swap(flipped.m_top, flipped.m_bottom);
      }
      if(flipped != constraints) {
        store(context, id, flipped);
      }
    }
    auto& node = resolve(document, id);
    auto children = document.get(id).m_children;
    auto is_container = node.get_artboard() || is_compound(node) ||
      is_plain_container(node);
    auto is_next = constraints.m_is_enabled && !children.empty() &&
      is_container;
    for(auto& child : children) {
      flip_pins(
        context, child, is_horizontal, is_vertical, is_next, depth + 1);
    }
  }

  void resize_single(Context& context, const NodeId& id,
      const Matrix& mapping) {
    auto& document = *context.m_document;
    auto& transaction = *context.m_transaction;
    if(document.get(id).get_artboard()) {
      if(context.m_rule != ResizeRule::PROPORTIONAL) {
        record_subtree(context, id);
      }
      resize_artboard(context, id, mapping);
      return;
    }
    auto parent = get_parent_transform(document, id);
    auto inverse_parent = invert(parent);
    if(!inverse_parent) {
      return;
    }
    auto relative = *inverse_parent * mapping * parent;
    if(document.get(id).get_reference() && !is_translation(relative)) {
      materialize_reference(document, transaction, id);
    }
    auto transform = get_transform(document, id);
    auto inverse = invert(transform);
    auto local = inverse ? *inverse * relative * transform : Matrix();
    auto scale = inverse ? get_axis_scale(local) : std::nullopt;
    auto& node = document.get(id);
    auto group = node.get_group();
    if(!scale || !group || node.m_header.m_transform_3d) {
      if(context.m_rule != ResizeRule::PROPORTIONAL && is_compound(node)) {
        reset_descendants(context, id, 0);
      }
      transform_node(
        document, transaction, id, mapping, *context.m_measurer);
      return;
    }
    if(context.m_rule != ResizeRule::PROPORTIONAL) {
      record_subtree(context, id);
    }
    auto bounds = get_local(context, id);
    if(!bounds) {
      transform_node(
        document, transaction, id, mapping, *context.m_measurer);
      return;
    }
    auto target = normalize(local.map(*bounds));
    auto is_horizontal = scale->m_x < 0;
    auto is_vertical = scale->m_y < 0;
    if(auto& stack = document.get(id).get_group()->m_stack) {
      if(stack->m_orientation == Orientation::HORIZONTAL) {
        target.m_x = bounds->m_x;
        target.m_width = bounds->m_width;
        is_horizontal = false;
      } else {
        target.m_y = bounds->m_y;
        target.m_height = bounds->m_height;
        is_vertical = false;
      }
    }
    auto is_mirrored = (is_horizontal || is_vertical) &&
      is_plain_container(document.get(id));
    place(context, id,
      transform * make_rect_mapping(*bounds, target) * *inverse, parent, 0);
    if(is_mirrored) {
      auto world = parent * transform;
      if(auto inverse_world = invert(world)) {
        transform_node(document, transaction, id, world * make_reflection(
          target.get_center(), is_horizontal, is_vertical) * *inverse_world,
          *context.m_measurer);
      }
    }
  }

  void resize_multiple(Context& context, const std::vector<NodeId>& nodes,
      const Matrix& mapping) {
    auto& document = *context.m_document;
    auto& transaction = *context.m_transaction;
    auto scale = get_axis_scale(mapping);
    auto bounds = get_world_bounds(
      document, nodes, BoundsType::GEOMETRY, *context.m_measurer);
    if(!scale || !bounds) {
      for(auto& id : nodes) {
        transform_node(
          document, transaction, id, mapping, *context.m_measurer);
      }
      return;
    }
    auto target = normalize(mapping.map(*bounds));
    auto placements = std::vector<std::optional<Rect>>();
    for(auto& id : nodes) {
      placements.push_back(get_world_bounds(
        document, id, BoundsType::GEOMETRY, *context.m_measurer));
    }
    if(context.m_rule != ResizeRule::PROPORTIONAL) {
      for(auto i = std::size_t(0); i != nodes.size(); ++i) {
        if(placements[i]) {
          record_node(context, nodes[i], *placements[i], *bounds, 0);
        }
      }
    }
    for(auto i = std::size_t(0); i != nodes.size(); ++i) {
      auto& id = nodes[i];
      if(!placements[i]) {
        continue;
      }
      auto& parent = document.get(id).m_parent;
      auto is_symbol =
        !parent.is_nil() && is_instance(resolve(document, parent));
      auto placed = snap(context, id, *placements[i], arrange(context, id,
        *placements[i], *bounds, target, is_symbol,
        get_stack_orientation(document, parent)), Matrix(), Matrix());
      auto world = make_rect_mapping(*placements[i], placed);
      if(document.get(id).get_artboard()) {
        resize_artboard(context, id, world);
        continue;
      }
      auto parent_transform = get_parent_transform(document, id);
      if(auto inverse = invert(parent_transform)) {
        place(context, id, *inverse * world * parent_transform,
          parent_transform, 0);
      }
    }
    auto is_horizontal = scale->m_x < 0;
    auto is_vertical = scale->m_y < 0;
    if(is_horizontal || is_vertical) {
      auto reflection =
        make_reflection(target.get_center(), is_horizontal, is_vertical);
      for(auto& id : nodes) {
        if(!document.get(id).get_artboard()) {
          transform_node(
            document, transaction, id, reflection, *context.m_measurer);
        }
      }
    }
  }

  std::optional<std::pair<bool, bool>> get_mirroring(
      const Document& document, const NodeId& id, const Matrix& mapping) {
    if(!(mapping.get_determinant() < 0)) {
      return std::nullopt;
    }
    auto world = get_world_transform(document, id);
    auto scale = std::optional<Point>();
    if(auto inverse = invert(world)) {
      scale = get_axis_scale(*inverse * mapping * world);
    }
    if(!scale) {
      scale = get_axis_scale(mapping);
    }
    if(!scale) {
      return std::nullopt;
    }
    return std::pair(scale->m_x < 0, scale->m_y < 0);
  }
}

Constraints Rellu::infer_constraints(const Document& document,
    const NodeId& id, const TextMeasurer& measurer) {
  auto& node = document.get(id);
  auto& stored = get_stored(document, id);
  if(node.m_parent.is_nil()) {
    return stored;
  }
  auto context =
    Context(&document, nullptr, &measurer, ResizeRule::RESPONSIVE);
  auto frame = get_content_frame(context, node.m_parent);
  auto bounds = get_placement(context, id);
  if(!frame || !bounds) {
    return stored;
  }
  return compute(document, id, *bounds, *frame);
}

void Rellu::record_constraints(const Document& document,
    Transaction& transaction, const NodeId& id, const TextMeasurer& measurer) {
  auto context =
    Context(&document, &transaction, &measurer, ResizeRule::RESPONSIVE);
  record_subtree(context, id);
}

void Rellu::resize_nodes(const Document& document, Transaction& transaction,
    const std::vector<NodeId>& nodes, const Matrix& mapping, ResizeRule rule,
    bool is_pixel_aligned, const TextMeasurer& measurer) {
  auto context =
    Context(&document, &transaction, &measurer, rule, is_pixel_aligned);
  auto mirrored = std::vector<std::pair<NodeId, std::pair<bool, bool>>>();
  for(auto& id : nodes) {
    if(auto mirroring = get_mirroring(document, id, mapping)) {
      mirrored.emplace_back(id, *mirroring);
    }
  }
  if(rule == ResizeRule::PROPORTIONAL) {
    for(auto& id : nodes) {
      transform_node(
        document, transaction, id, mapping, *context.m_measurer);
    }
  } else if(nodes.size() == 1) {
    resize_single(context, nodes.front(), mapping);
  } else if(!nodes.empty()) {
    resize_multiple(context, nodes, mapping);
  }
  for(auto& [id, mirroring] : mirrored) {
    if(document.find(id)) {
      flip_pins(context, id, mirroring.first, mirroring.second, false, 0);
    }
  }
}

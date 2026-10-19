#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StackCommands.hpp"
#include "Rellu/Tools/LayoutHandles.hpp"

using namespace Rellu;

namespace {
  constexpr auto GRID_COLOR = make_color(56, 181, 74);
  constexpr auto GAP_FILL = make_color(255, 46, 186, 0.3);
  constexpr auto GAP_STROKE = make_color(255, 46, 186);
  constexpr auto LINE_WIDTH = 1.0;
  constexpr auto DASH = 4.0;
  constexpr auto MINIMUM_SIZE = 1.0;

  using Slot = std::pair<NodeId, Rect>;

  const Group* find_group(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node) {
      return nullptr;
    }
    return node->get_group();
  }

  const RepeatGrid* find_grid(const Document& document, const NodeId& id) {
    auto group = find_group(document, id);
    if(!group || !group->m_repeat_grid) {
      return nullptr;
    }
    return &*group->m_repeat_grid;
  }

  const Stack* find_stack(const Document& document, const NodeId& id) {
    auto group = find_group(document, id);
    if(!group || !group->m_stack) {
      return nullptr;
    }
    return &*group->m_stack;
  }

  bool is_locked(const Document& document, const NodeId& id) {
    for(auto node = document.find(id); node;
        node = document.find(node->m_parent)) {
      if(node->m_header.m_is_locked) {
        return true;
      }
      if(node->m_parent.is_nil()) {
        return false;
      }
    }
    return false;
  }

  NodeId get_selected(const ToolContext& context) {
    auto& nodes = context.get_editor().get_selection().get_nodes();
    if(nodes.size() != 1 || is_locked(context.get_document(), nodes.front())) {
      return NodeId();
    }
    return nodes.front();
  }

  Matrix get_view_transform(const ToolContext& context, const NodeId& id) {
    return context.get_view() *
      get_world_transform(context.get_document(), id);
  }

  double get_scale(const Matrix& matrix) {
    return std::sqrt(std::abs(matrix.get_determinant()));
  }

  std::vector<Slot> get_slots(const ToolContext& context, const NodeId& id) {
    auto& document = context.get_document();
    auto slots = std::vector<Slot>();
    for(auto& item : get_stack_items(document, id)) {
      if(!document.get(item).m_header.m_is_visible) {
        continue;
      }
      if(auto bounds = get_bounds(document, item,
          get_transform(document, item), BoundsType::GEOMETRY,
          context.get_text_measurer())) {
        slots.emplace_back(item, *bounds);
      }
    }
    return slots;
  }

  OverlayShape make_box(const Matrix& matrix, const Rect& rect, Color fill,
      Color stroke, double radius) {
    auto origin = matrix.map(rect.get_top_left());
    auto right = matrix.map(Point(rect.get_right(), rect.get_top()));
    auto bottom = matrix.map(Point(rect.get_left(), rect.get_bottom()));
    auto shape = OverlayShape();
    shape.m_center = matrix.map(rect.get_center());
    shape.m_size = Size(distance(origin, right), distance(origin, bottom));
    shape.m_angle =
      std::atan2(right.m_y - origin.m_y, right.m_x - origin.m_x);
    shape.m_radius = radius;
    shape.m_fill = fill;
    shape.m_stroke = stroke;
    shape.m_stroke_width = stroke.m_alpha > 0 ? LINE_WIDTH : 0;
    return shape;
  }

  void add_dashes(CanvasOverlay& overlay, Point start, Point end) {
    auto length = distance(start, end);
    if(length == 0) {
      return;
    }
    auto direction = (end - start) * (1 / length);
    for(auto offset = 0.0; offset < length; offset += 2 * DASH) {
      auto stop = std::min(offset + DASH, length);
      overlay.m_shapes.push_back(make_overlay_line(start + direction * offset,
        start + direction * stop, LINE_WIDTH, GRID_COLOR));
    }
  }

  std::pair<double, double> widen(double start, double end, double reach) {
    if(end - start >= reach) {
      return std::pair(start, end);
    }
    auto center = (start + end) / 2;
    return std::pair(center - reach / 2, center + reach / 2);
  }
}

LayoutHandles::LayoutHandles()
  : m_target(Part::NONE, 0),
    m_hover(Part::NONE, 0),
    m_press(0, 0),
    m_initial(0),
    m_is_applied(false) {}

NodeId LayoutHandles::get_grid(const ToolContext& context) const {
  auto id = get_selected(context);
  if(id.is_nil() || !find_grid(context.get_document(), id)) {
    return NodeId();
  }
  return id;
}

NodeId LayoutHandles::get_stack(const ToolContext& context) const {
  auto id = get_selected(context);
  if(id.is_nil() || !find_stack(context.get_document(), id)) {
    return NodeId();
  }
  return id;
}

std::optional<ToolCursor> LayoutHandles::hover(
    const ToolContext& context, Point position) {
  auto target = find(context, position);
  m_hover = target.value_or(Target(Part::NONE, 0));
  if(!target) {
    return std::nullopt;
  }
  auto id = get_subject(context);
  auto matrix = get_view_transform(context, id);
  auto angle = std::atan2(matrix.m_b, matrix.m_a);
  auto stack = find_stack(context.get_document(), id);
  if(target->m_part == Part::BOTTOM || target->m_part == Part::ROW_GAP ||
      (target->m_part == Part::STACK_GAP && stack &&
        stack->m_orientation == Orientation::VERTICAL)) {
    angle += std::numbers::pi / 2;
  }
  return ToolCursor(CursorKind::RESIZE, angle);
}

bool LayoutHandles::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return false;
  }
  auto target = find(context, event.m_position);
  if(!target) {
    return false;
  }
  m_node = get_subject(context);
  auto inverse = invert(get_view_transform(context, m_node));
  if(!inverse) {
    return false;
  }
  auto& document = context.get_document();
  m_target = *target;
  m_press = inverse->map(event.m_position);
  m_item = NodeId();
  if(m_target.m_part == Part::STACK_GAP) {
    auto slots = get_slots(context, m_node);
    m_item = slots[static_cast<std::size_t>(m_target.m_index)].first;
    m_initial = get_stack_margin(
      document, *find_stack(document, m_node), m_item);
  } else {
    auto& grid = *find_grid(document, m_node);
    if(m_target.m_part == Part::RIGHT) {
      m_initial = grid.m_size.m_width;
    } else if(m_target.m_part == Part::BOTTOM) {
      m_initial = grid.m_size.m_height;
    } else if(m_target.m_part == Part::COLUMN_GAP) {
      m_initial = grid.m_column_spacing;
    } else {
      m_initial = grid.m_row_spacing;
    }
  }
  m_is_applied = false;
  context.get_editor().get_history().seal();
  return true;
}

void LayoutHandles::move(ToolContext& context, const PointerEvent& event) {
  auto& document = context.get_document();
  auto inverse = invert(get_view_transform(context, m_node));
  if(!inverse || m_target.m_part == Part::NONE) {
    return;
  }
  auto delta = inverse->map(event.m_position) - m_press;
  auto& editor = context.get_editor();
  if(m_target.m_part == Part::STACK_GAP) {
    auto stack = find_stack(document, m_node);
    if(!stack) {
      return;
    }
    auto offset = stack->m_orientation == Orientation::VERTICAL ?
      delta.m_y : delta.m_x;
    auto margin = std::round(m_initial + offset);
    if(margin == get_stack_margin(document, *stack, m_item)) {
      return;
    }
    editor.merge(std::make_unique<SetStackSpacingCommand>(
      m_node, m_item, margin, context.get_text_measurer()));
    m_is_applied = true;
    return;
  }
  auto grid = find_grid(document, m_node);
  if(!grid) {
    return;
  }
  if(m_target.m_part == Part::RIGHT || m_target.m_part == Part::BOTTOM) {
    auto size = grid->m_size;
    if(m_target.m_part == Part::RIGHT) {
      size.m_width =
        std::max(std::round(m_initial + delta.m_x), MINIMUM_SIZE);
    } else {
      size.m_height =
        std::max(std::round(m_initial + delta.m_y), MINIMUM_SIZE);
    }
    if(size == grid->m_size) {
      return;
    }
    editor.merge(std::make_unique<ResizeRepeatGridCommand>(m_node, size));
  } else {
    auto is_column = m_target.m_part == Part::COLUMN_GAP;
    auto offset = is_column ? delta.m_x : delta.m_y;
    auto spacing =
      std::round(m_initial + offset / std::max(m_target.m_index, 1));
    if(spacing ==
        (is_column ? grid->m_column_spacing : grid->m_row_spacing)) {
      return;
    }
    editor.merge(std::make_unique<SetRepeatGridSpacingCommand>(
      std::vector{m_node}, is_column ? Orientation::HORIZONTAL :
        Orientation::VERTICAL, spacing));
  }
  m_is_applied = true;
}

void LayoutHandles::release(ToolContext& context) {
  if(m_target.m_part != Part::NONE) {
    context.get_editor().get_history().seal();
  }
  m_target = Target(Part::NONE, 0);
  m_node = NodeId();
  m_item = NodeId();
  m_is_applied = false;
}

void LayoutHandles::cancel(ToolContext& context) {
  if(m_is_applied) {
    context.get_editor().undo();
  }
  release(context);
}

bool LayoutHandles::is_active() const {
  return m_target.m_part != Part::NONE;
}

void LayoutHandles::draw(
    const ToolContext& context, CanvasOverlay& overlay) const {
  auto id = is_active() ? m_node : get_subject(context);
  if(id.is_nil()) {
    return;
  }
  auto matrix = get_view_transform(context, id);
  auto shown = is_active() ? m_target : m_hover;
  if(auto gap = get_gap(context, id, shown)) {
    overlay.m_shapes.push_back(
      make_box(matrix, *gap, GAP_FILL, GAP_STROKE, 0));
  }
  auto grid = find_grid(context.get_document(), id);
  if(!grid) {
    return;
  }
  auto& size = grid->m_size;
  auto corners = std::array<Point, 4>({matrix.map(Point(0, 0)),
    matrix.map(Point(size.m_width, 0)),
    matrix.map(Point(size.m_width, size.m_height)),
    matrix.map(Point(0, size.m_height))});
  for(auto i = std::size_t(0); i != corners.size(); ++i) {
    add_dashes(overlay, corners[i], corners[(i + 1) % corners.size()]);
  }
  auto scale = get_scale(matrix);
  if(scale <= 0) {
    return;
  }
  auto length = HANDLE_LENGTH / scale;
  auto thickness = HANDLE_THICKNESS / scale;
  overlay.m_shapes.push_back(make_box(matrix,
    Rect(size.m_width - thickness / 2, size.m_height / 2 - length / 2,
      thickness, length), GRID_COLOR, Color(0, 0, 0, 0),
    HANDLE_THICKNESS / 2));
  overlay.m_shapes.push_back(make_box(matrix,
    Rect(size.m_width / 2 - length / 2, size.m_height - thickness / 2,
      length, thickness), GRID_COLOR, Color(0, 0, 0, 0),
    HANDLE_THICKNESS / 2));
}

NodeId LayoutHandles::get_subject(const ToolContext& context) const {
  auto grid = get_grid(context);
  if(!grid.is_nil()) {
    return grid;
  }
  return get_stack(context);
}

std::optional<LayoutHandles::Target> LayoutHandles::find(
    const ToolContext& context, Point position) const {
  auto id = get_subject(context);
  if(id.is_nil()) {
    return std::nullopt;
  }
  auto matrix = get_view_transform(context, id);
  auto inverse = invert(matrix);
  auto scale = get_scale(matrix);
  if(!inverse || scale <= 0) {
    return std::nullopt;
  }
  auto point = inverse->map(position);
  auto is_within = [&] (const std::optional<Rect>& gap) {
    return gap && point.m_x >= gap->get_left() &&
      point.m_x <= gap->get_right() && point.m_y >= gap->get_top() &&
      point.m_y <= gap->get_bottom();
  };
  auto grid = find_grid(context.get_document(), id);
  if(!grid) {
    auto count = static_cast<int>(get_slots(context, id).size());
    for(auto i = 0; i + 1 < count; ++i) {
      if(is_within(get_gap(context, id, Target(Part::STACK_GAP, i)))) {
        return Target(Part::STACK_GAP, i);
      }
    }
    return std::nullopt;
  }
  auto& size = grid->m_size;
  auto reach = HANDLE_REACH / scale;
  auto length = (HANDLE_LENGTH / 2 + HANDLE_REACH) / scale;
  if(std::abs(point.m_x - size.m_width) <= reach &&
      std::abs(point.m_y - size.m_height / 2) <= length) {
    return Target(Part::RIGHT, 0);
  }
  if(std::abs(point.m_y - size.m_height) <= reach &&
      std::abs(point.m_x - size.m_width / 2) <= length) {
    return Target(Part::BOTTOM, 0);
  }
  for(auto i = 1; i < grid->m_column_count; ++i) {
    if(is_within(get_gap(context, id, Target(Part::COLUMN_GAP, i)))) {
      return Target(Part::COLUMN_GAP, i);
    }
  }
  for(auto i = 1; i < grid->m_row_count; ++i) {
    if(is_within(get_gap(context, id, Target(Part::ROW_GAP, i)))) {
      return Target(Part::ROW_GAP, i);
    }
  }
  return std::nullopt;
}

std::optional<Rect> LayoutHandles::get_gap(const ToolContext& context,
    const NodeId& id, const Target& target) const {
  auto& document = context.get_document();
  auto scale = get_scale(get_view_transform(context, id));
  if(scale <= 0) {
    return std::nullopt;
  }
  auto reach = GAP_REACH / scale;
  if(target.m_part == Part::STACK_GAP) {
    auto stack = find_stack(document, id);
    auto slots = get_slots(context, id);
    auto index = static_cast<std::size_t>(target.m_index);
    if(!stack || index + 1 >= slots.size()) {
      return std::nullopt;
    }
    auto& before = slots[index].second;
    auto& after = slots[index + 1].second;
    if(stack->m_orientation == Orientation::VERTICAL) {
      auto span = widen(before.get_bottom(), after.get_top(), reach);
      auto left = std::min(before.get_left(), after.get_left());
      auto right = std::max(before.get_right(), after.get_right());
      return Rect(left, span.first, right - left, span.second - span.first);
    }
    auto span = widen(before.get_right(), after.get_left(), reach);
    auto top = std::min(before.get_top(), after.get_top());
    auto bottom = std::max(before.get_bottom(), after.get_bottom());
    return Rect(span.first, top, span.second - span.first, bottom - top);
  }
  auto grid = find_grid(document, id);
  if(!grid) {
    return std::nullopt;
  }
  auto& size = grid->m_size;
  if(target.m_part == Part::COLUMN_GAP) {
    auto end = target.m_index *
      (grid->m_cell_size.m_width + grid->m_column_spacing);
    auto span = widen(end - grid->m_column_spacing, end, reach);
    if(span.first >= size.m_width) {
      return std::nullopt;
    }
    return Rect(span.first, 0, span.second - span.first, size.m_height);
  } else if(target.m_part == Part::ROW_GAP) {
    auto end =
      target.m_index * (grid->m_cell_size.m_height + grid->m_row_spacing);
    auto span = widen(end - grid->m_row_spacing, end, reach);
    if(span.first >= size.m_height) {
      return std::nullopt;
    }
    return Rect(0, span.first, size.m_width, span.second - span.first);
  }
  return std::nullopt;
}

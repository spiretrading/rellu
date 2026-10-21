#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StackCommands.hpp"
#include "Rellu/Tools/LayoutHandles.hpp"
#include "Rellu/Tools/SelectionBox.hpp"
#include "Rellu/Tools/SmartGuides.hpp"

using namespace Rellu;

namespace {
  constexpr auto GRID_COLOR = make_color(30, 203, 64);
  constexpr auto HANDLE_FILL = make_color(255, 255, 255);
  constexpr auto GAP_FILL = make_color(255, 74, 225, 0.4);
  constexpr auto PALE_GAP_FILL = make_color(255, 74, 225, 0.2);
  constexpr auto GAP_STROKE = make_color(255, 74, 225);
  constexpr auto LINE_WIDTH = 1.0;
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

  struct Frame {
    Point m_scale;
    Size m_size;
  };

  Frame get_frame(const Matrix& matrix, Size size) {
    auto origin = matrix.map(Point(0, 0));
    auto scale = Point(distance(origin, matrix.map(Point(1, 0))),
      distance(origin, matrix.map(Point(0, 1))));
    return Frame(scale,
      Size(size.m_width * scale.m_x, size.m_height * scale.m_y));
  }

  Rect to_local(const Frame& frame, const Rect& rect) {
    if(!(frame.m_scale.m_x > 0) || !(frame.m_scale.m_y > 0)) {
      return Rect(0, 0, 0, 0);
    }
    return Rect(rect.m_x / frame.m_scale.m_x, rect.m_y / frame.m_scale.m_y,
      rect.m_width / frame.m_scale.m_x, rect.m_height / frame.m_scale.m_y);
  }

  void add_gap_sides(CanvasOverlay& overlay, const Matrix& matrix,
      const Rect& gap, bool is_column) {
    auto frame = get_frame(matrix, gap.get_size());
    if(!(frame.m_scale.m_x > 0) || !(frame.m_scale.m_y > 0)) {
      return;
    }
    auto offset = Point(LINE_WIDTH / 2 / frame.m_scale.m_x,
      LINE_WIDTH / 2 / frame.m_scale.m_y);
    auto add = [&] (Point start, Point end) {
      overlay.m_shapes.push_back(make_overlay_line(
        matrix.map(start), matrix.map(end), LINE_WIDTH, GAP_STROKE));
    };
    if(is_column) {
      auto left = gap.get_left() - offset.m_x;
      auto right = gap.get_right() + offset.m_x;
      add(Point(left, gap.get_top()), Point(left, gap.get_bottom()));
      add(Point(right, gap.get_top()), Point(right, gap.get_bottom()));
    } else {
      auto top = gap.get_top() - offset.m_y;
      auto bottom = gap.get_bottom() + offset.m_y;
      add(Point(gap.get_left(), top), Point(gap.get_right(), top));
      add(Point(gap.get_left(), bottom), Point(gap.get_right(), bottom));
    }
  }

  std::string format_number(double value) {
    if(value == std::floor(value)) {
      return std::format("{}", static_cast<long long>(value));
    }
    return std::format("{}", value);
  }

  std::string format_spacing(double spacing) {
    if(spacing != std::floor(spacing)) {
      return std::format("{:.2f}", spacing);
    }
    return std::format("{}", static_cast<long long>(spacing));
  }

  int get_gap_count(const RepeatGrid& grid, bool is_column, double scale) {
    auto count =
      std::max((is_column ? grid.m_column_count : grid.m_row_count) - 1, 0);
    auto spacing = is_column ? grid.m_column_spacing : grid.m_row_spacing;
    auto pitch = (is_column ? grid.m_cell_size.m_width :
      grid.m_cell_size.m_height) + spacing;
    auto room = SmartGuides::FONT_SIZE;
    if(is_column) {
      room *= static_cast<double>(format_number(spacing).size()) / 2;
    }
    if(pitch * scale < room) {
      return std::min(count, 1);
    }
    return count;
  }

  bool is_activation_key(int key) {
    return key == Qt::Key_S || key == Qt::Key_QuoteLeft ||
      key == Qt::Key_AsciiTilde;
  }

  std::shared_ptr<const QImage> make_label(
      const ToolContext& context, const std::string& text) {
    auto scale = context.get_scale();
    if(!(scale > 0)) {
      return nullptr;
    }
    return make_distance_label(text, scale);
  }

  void add_label(const ToolContext& context, CanvasOverlay& overlay,
      const std::shared_ptr<const QImage>& label, const Matrix& matrix,
      const Rect& gap, bool is_above) {
    if(!label) {
      return;
    }
    auto size = Point(label->width(), label->height()) / context.get_scale();
    auto center = Point(0, 0);
    if(is_above) {
      center = matrix.map(Point(gap.get_center().m_x, gap.get_top())) -
        Point(0, SmartGuides::LABEL_OFFSET + SmartGuides::FONT_ASCENT / 2);
    } else {
      center = matrix.map(Point(gap.get_left(), gap.get_center().m_y)) -
        Point(SmartGuides::LABEL_OFFSET + size.m_x / 2 -
          SmartGuides::LABEL_PADDING, 0);
    }
    overlay.m_images.push_back(OverlayImage(label, center - size / 2));
  }

  std::pair<double, double> widen(double start, double end, double reach) {
    if(start > end) {
      std::swap(start, end);
    }
    if(end - start >= reach) {
      return std::pair(start, end);
    }
    auto center = (start + end) / 2;
    return std::pair(center - reach / 2, center + reach / 2);
  }
}

LayoutHandles::LayoutHandles()
  : m_target(Part::NONE, 0, NodeId()),
    m_hover(Part::NONE, 0, NodeId()),
    m_press(0, 0),
    m_initial(0),
    m_spacing(0),
    m_is_applied(false),
    m_is_all(false),
    m_is_activated(false),
    m_is_shift(false) {}

NodeId LayoutHandles::get_grid(const ToolContext& context) const {
  auto id = get_selected(context);
  if(id.is_nil() || !find_grid(context.get_document(), id)) {
    return NodeId();
  }
  return id;
}

std::vector<NodeId> LayoutHandles::get_stacks(
    const ToolContext& context) const {
  auto& document = context.get_document();
  auto stacks = std::vector<NodeId>();
  auto selected = get_selected(context);
  if(m_is_activated && !selected.is_nil() && find_stack(document, selected)) {
    stacks.push_back(selected);
  }
  auto& container = context.get_editor().get_context().get_container();
  if(!container.is_nil() && container != selected &&
      find_stack(document, container) && !is_locked(document, container)) {
    stacks.push_back(container);
  }
  return stacks;
}

bool LayoutHandles::key_press(const KeyEvent& event) {
  if(!is_activation_key(event.m_key) || m_is_activated) {
    return false;
  }
  m_is_activated = true;
  return true;
}

bool LayoutHandles::key_release(const KeyEvent& event) {
  if(!is_activation_key(event.m_key) || event.m_is_repeat ||
      !m_is_activated) {
    return false;
  }
  m_is_activated = false;
  return true;
}

std::optional<ToolCursor> LayoutHandles::hover(const ToolContext& context,
    Point position, Qt::KeyboardModifiers modifiers, bool is_handle_hit) {
  m_is_shift = modifiers.testFlag(Qt::ShiftModifier);
  auto target = find(context, position, is_handle_hit);
  m_hover = target.value_or(Target(Part::NONE, 0, NodeId()));
  if(!target) {
    return std::nullopt;
  }
  auto matrix = get_view_transform(context, target->m_node);
  auto angle = std::atan2(matrix.m_b, matrix.m_a);
  auto stack = find_stack(context.get_document(), target->m_node);
  if(target->m_part == Part::BOTTOM || target->m_part == Part::ROW_GAP ||
      (target->m_part == Part::STACK_GAP && stack &&
        stack->m_orientation == Orientation::VERTICAL)) {
    angle += std::numbers::pi / 2;
  }
  return ToolCursor(CursorKind::RESIZE, angle);
}

bool LayoutHandles::press(
    ToolContext& context, const PointerEvent& event, bool is_handle_hit) {
  if(event.m_button != Qt::LeftButton) {
    return false;
  }
  auto target = find(context, event.m_position, is_handle_hit);
  if(!target) {
    return false;
  }
  auto inverse = invert(get_view_transform(context, target->m_node));
  if(!inverse) {
    return false;
  }
  auto& document = context.get_document();
  m_node = target->m_node;
  m_target = *target;
  m_press = inverse->map(event.m_position);
  m_item = NodeId();
  m_is_all = false;
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
  m_spacing = m_initial;
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
  m_is_shift = event.m_modifiers.testFlag(Qt::ShiftModifier);
  auto delta = inverse->map(event.m_position) - m_press;
  auto& editor = context.get_editor();
  if(m_target.m_part == Part::STACK_GAP) {
    auto stack = find_stack(document, m_node);
    if(!stack) {
      return;
    }
    auto offset = stack->m_orientation == Orientation::VERTICAL ?
      delta.m_y : delta.m_x;
    auto spacing =
      std::clamp(std::round(m_initial + offset), 0.0, MAXIMUM_SPACING);
    if(spacing == m_spacing && m_is_shift == m_is_all) {
      return;
    }
    if(m_is_applied) {
      editor.undo();
      m_is_applied = false;
    }
    m_spacing = spacing;
    m_is_all = m_is_shift;
    auto commands = std::vector<std::unique_ptr<Command>>();
    if(m_is_all) {
      commands.push_back(std::make_unique<SetStackSpacingCommand>(
        std::vector{m_node}, spacing, context.get_text_measurer()));
    } else {
      commands.push_back(std::make_unique<SetStackSpacingCommand>(
        m_node, m_item, spacing, context.get_text_measurer()));
    }
    if(editor.execute(std::make_unique<CompositeCommand>(
        "Stack Spacing", std::move(commands)))) {
      m_is_applied = true;
    }
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
  m_target = Target(Part::NONE, 0, NodeId());
  m_node = NodeId();
  m_item = NodeId();
  m_is_applied = false;
  m_is_all = false;
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
  auto shown = is_active() ? m_target : m_hover;
  if(shown.m_part == Part::STACK_GAP) {
    auto stacks = get_stacks(context);
    if(is_active() || std::find(stacks.begin(), stacks.end(),
        shown.m_node) != stacks.end()) {
      draw_stack(context, overlay, shown);
    }
  }
  auto id = get_grid(context);
  if(is_active()) {
    id = NodeId();
    if(m_target.m_part != Part::STACK_GAP) {
      id = m_node;
    }
  }
  auto grid = find_grid(context.get_document(), id);
  if(!grid) {
    return;
  }
  if(shown.m_node != id) {
    shown = Target(Part::NONE, 0, NodeId());
  }
  auto matrix = get_view_transform(context, id);
  auto is_gap = [] (Part part) {
    return part == Part::COLUMN_GAP || part == Part::ROW_GAP;
  };
  if(is_active() && is_gap(m_target.m_part)) {
    auto is_column = m_target.m_part == Part::COLUMN_GAP;
    auto spacing = is_column ? grid->m_column_spacing : grid->m_row_spacing;
    auto label = make_label(context, format_spacing(spacing));
    for(auto i = 1; i <= get_gap_count(*grid, is_column, get_scale(matrix));
        ++i) {
      auto gap = get_gap(context, Target(m_target.m_part, i, id), false);
      if(!gap) {
        continue;
      }
      overlay.m_shapes.push_back(
        make_box(matrix, *gap, GAP_FILL, Color(0, 0, 0, 0), 0));
      add_label(context, overlay, label, matrix, *gap, is_column);
    }
  } else if(is_gap(shown.m_part)) {
    if(auto gap = get_gap(context, shown, false)) {
      overlay.m_shapes.push_back(
        make_box(matrix, *gap, GAP_FILL, Color(0, 0, 0, 0), 0));
    }
  }
  if(is_gap(shown.m_part)) {
    if(auto gap = get_gap(context, shown, false)) {
      add_gap_sides(
        overlay, matrix, *gap, shown.m_part == Part::COLUMN_GAP);
    }
  }
  auto frame = get_frame(matrix, grid->m_size);
  for(auto part : {Part::RIGHT, Part::BOTTOM}) {
    auto handle = part == Part::RIGHT ? Handle::RIGHT : Handle::BOTTOM;
    auto rect = to_local(frame, get_grid_handle_rect(handle, frame.m_size));
    auto fill = HANDLE_FILL;
    if(shown.m_part == part) {
      fill = GRID_COLOR;
    }
    overlay.m_shapes.push_back(
      make_box(matrix, rect, fill, GRID_COLOR, HANDLE_RADIUS));
  }
}

std::optional<LayoutHandles::Target> LayoutHandles::find(
    const ToolContext& context, Point position, bool is_handle_hit) const {
  auto& document = context.get_document();
  auto find_in = [&] (const NodeId& id) -> std::optional<Target> {
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
    auto grid = find_grid(document, id);
    if(!grid) {
      auto count = static_cast<int>(get_slots(context, id).size());
      for(auto i = 0; i + 1 < count; ++i) {
        if(is_within(get_gap(context, Target(Part::STACK_GAP, i, id)))) {
          return Target(Part::STACK_GAP, i, id);
        }
      }
      return std::nullopt;
    }
    auto frame = get_frame(matrix, grid->m_size);
    for(auto part : {Part::BOTTOM, Part::RIGHT}) {
      auto handle = part == Part::RIGHT ? Handle::RIGHT : Handle::BOTTOM;
      if(is_within(
          to_local(frame, get_grid_handle_rect(handle, frame.m_size)))) {
        return Target(part, 0, id);
      }
    }
    for(auto i = 1; i <= get_gap_count(*grid, false, scale); ++i) {
      if(is_within(get_gap(context, Target(Part::ROW_GAP, i, id)))) {
        return Target(Part::ROW_GAP, i, id);
      }
    }
    for(auto i = 1; i <= get_gap_count(*grid, true, scale); ++i) {
      if(is_within(get_gap(context, Target(Part::COLUMN_GAP, i, id)))) {
        return Target(Part::COLUMN_GAP, i, id);
      }
    }
    return std::nullopt;
  };
  if(auto grid = get_grid(context); !grid.is_nil()) {
    if(auto target = find_in(grid)) {
      return target;
    }
  }
  for(auto& stack : get_stacks(context)) {
    if(is_handle_hit && is_entered(context, stack)) {
      continue;
    }
    if(auto target = find_in(stack)) {
      return target;
    }
  }
  return std::nullopt;
}

bool LayoutHandles::is_entered(
    const ToolContext& context, const NodeId& id) const {
  return context.get_editor().get_context().get_container() == id;
}

std::optional<Rect> LayoutHandles::get_gap(const ToolContext& context,
    const Target& target, bool is_widened) const {
  auto& document = context.get_document();
  auto& id = target.m_node;
  auto scale = get_scale(get_view_transform(context, id));
  if(scale <= 0) {
    return std::nullopt;
  }
  auto reach = 0.0;
  if(is_widened) {
    reach = GAP_REACH / scale;
  }
  if(target.m_part == Part::STACK_GAP) {
    auto stack = find_stack(document, id);
    auto slots = get_slots(context, id);
    auto index = static_cast<std::size_t>(target.m_index);
    auto bounds = get_local_bounds(
      document, id, BoundsType::GEOMETRY, context.get_text_measurer());
    if(!stack || !bounds || index + 1 >= slots.size()) {
      return std::nullopt;
    }
    auto padding = Margins();
    if(is_entered(context, id)) {
      padding = stack->m_padding;
    }
    auto& before = slots[index].second;
    auto& after = slots[index + 1].second;
    if(stack->m_orientation == Orientation::VERTICAL) {
      auto span = widen(before.get_bottom(), after.get_top(), reach);
      auto left = bounds->get_left() + std::max(padding.m_left, 0.0);
      auto right = bounds->get_right() - std::max(padding.m_right, 0.0);
      return Rect(left, span.first, right - left, span.second - span.first);
    }
    auto span = widen(before.get_right(), after.get_left(), reach);
    auto top = bounds->get_top() + std::max(padding.m_top, 0.0);
    auto bottom = bounds->get_bottom() - std::max(padding.m_bottom, 0.0);
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

void LayoutHandles::draw_stack(const ToolContext& context,
    CanvasOverlay& overlay, const Target& shown) const {
  auto stack = find_stack(context.get_document(), shown.m_node);
  if(!stack) {
    return;
  }
  auto matrix = get_view_transform(context, shown.m_node);
  auto is_selection = !is_entered(context, shown.m_node);
  if(m_is_shift) {
    auto count = static_cast<int>(get_slots(context, shown.m_node).size());
    for(auto i = 0; i + 1 < count; ++i) {
      if(i == shown.m_index) {
        continue;
      }
      if(auto gap = get_gap(
          context, Target(Part::STACK_GAP, i, shown.m_node), false)) {
        overlay.m_shapes.push_back(
          make_box(matrix, *gap, PALE_GAP_FILL, Color(0, 0, 0, 0), 0));
      }
    }
  }
  auto gap = get_gap(context, shown, false);
  if(!gap) {
    return;
  }
  auto fill = PALE_GAP_FILL;
  if(is_selection) {
    fill = GAP_FILL;
  }
  overlay.m_shapes.push_back(
    make_box(matrix, *gap, fill, Color(0, 0, 0, 0), 0));
  auto is_horizontal = stack->m_orientation == Orientation::HORIZONTAL;
  if(is_selection) {
    add_gap_sides(overlay, matrix, *gap, is_horizontal);
  }
  auto size = is_horizontal ? gap->m_width : gap->m_height;
  add_label(context, overlay,
    make_label(context, std::format("{}", std::lround(size))), matrix, *gap,
    is_horizontal);
}

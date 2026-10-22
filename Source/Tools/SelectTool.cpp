#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <unordered_set>
#include <variant>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Tools/PathPoints.hpp"
#include "Rellu/Tools/SelectTool.hpp"
#include "Rellu/Tools/TextTool.hpp"

using namespace Rellu;

namespace {
  constexpr auto SELECTION_COLOR = make_color(38, 128, 235);
  constexpr auto CONTEXT_COLOR = make_color(38, 128, 235, 0.2);
  constexpr auto GRID_CONTEXT_COLOR = make_color(30, 203, 64, 0.2);
  constexpr auto LOCKED_CONTEXT_COLOR = make_color(162, 162, 162, 0.3);
  constexpr auto CONTEXT_WIDTH = 4.0;
  constexpr auto MINIMUM_NUDGED_SIZE = 0.5;
  constexpr auto MARQUEE_FILL = make_color(38, 128, 235, 0.08);
  constexpr auto LINE_WIDTH = 1.0;
  constexpr auto OUTLINE_FLATNESS = 0.2;
  constexpr auto MAXIMUM_OUTLINE_SEGMENTS = std::size_t(4096);

  class DragCopyCommand : public Command {
    public:
      explicit DragCopyCommand(std::vector<NodeId> nodes)
        : m_duplicate(std::move(nodes)) {}

      std::string get_label() const override {
        return m_duplicate.get_label();
      }

      void apply(const Document& document, Transaction& transaction) override {
        m_duplicate.apply(document, transaction);
      }

      bool merge(const Command& command) override {
        auto translation = dynamic_cast<const TranslateCommand*>(&command);
        return translation &&
          translation->get_nodes() == m_duplicate.get_copies();
      }

      std::optional<std::vector<NodeId>> get_selection() const override {
        return m_duplicate.get_selection();
      }

    private:
      DuplicateCommand m_duplicate;
  };

  bool is_shift(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ShiftModifier);
  }

  bool is_alt(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::AltModifier);
  }

  bool is_control(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ControlModifier);
  }

  bool is_enterable(const Node& node) {
    if(node.get_group()) {
      return true;
    } else if(auto shape = node.get_shape()) {
      return std::holds_alternative<CompoundShape>(shape->m_geometry);
    }
    return false;
  }

  bool is_artboard(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->get_artboard();
  }

  bool is_scroll_group(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    auto group = node ? node->get_group() : nullptr;
    return group && group->m_scroll_group;
  }

  bool has_visible_leaf(const Document& document, const NodeId& id) {
    for(auto& child : document.get(id).m_children) {
      auto& node = document.get(child);
      if(!node.m_header.m_is_visible) {
        continue;
      } else if(!node.get_group() || has_visible_leaf(document, child)) {
        return true;
      }
    }
    return false;
  }

  void add_context_outline(CanvasOverlay& overlay,
      const std::array<Point, 4>& corners, Color color) {
    auto orientation = 1.0;
    if(cross(corners[1] - corners[0], corners[3] - corners[0]) < 0) {
      orientation = -1;
    }
    for(auto i = std::size_t(0); i != corners.size(); ++i) {
      auto start = corners[i];
      auto end = corners[(i + 1) % corners.size()];
      auto length = distance(start, end);
      if(length == 0) {
        continue;
      }
      auto along = (end - start) / length;
      if(i % 2 == 0) {
        start = start - along * CONTEXT_WIDTH;
        end = end + along * CONTEXT_WIDTH;
      }
      auto offset =
        Point(along.m_y, -along.m_x) * (orientation * CONTEXT_WIDTH / 2);
      overlay.m_shapes.push_back(make_overlay_line(
        start + offset, end + offset, CONTEXT_WIDTH, color));
    }
  }

  std::string get_resize_nudge_label(Point direction) {
    if(direction.m_x < 0) {
      return "Resize Nudge Left";
    } else if(direction.m_x > 0) {
      return "Resize Nudge Right";
    } else if(direction.m_y < 0) {
      return "Resize Nudge Up";
    }
    return "Resize Nudge Down";
  }

  double get_sign(double value) {
    if(value < 0) {
      return -1;
    } else if(value > 0) {
      return 1;
    }
    return 0;
  }

  std::unique_ptr<Command> make_nudge_resize(const Document& document,
      const NodeId& id, Point step, const std::string& label,
      const TextMeasurer& measurer) {
    auto node = document.find(id);
    auto local =
      get_local_bounds(document, id, BoundsType::GEOMETRY, measurer);
    auto world = get_world_transform(document, id);
    auto inverse = invert(world);
    if(!node || !local || !inverse) {
      return nullptr;
    }
    auto sign = get_sign(step.m_x + step.m_y);
    auto magnitude = std::abs(step.m_x + step.m_y);
    auto& lock = node->m_header.m_aspect_lock;
    auto is_locked = lock && lock->m_width > 0 && lock->m_height > 0;
    auto angle = std::atan2(world.m_b, world.m_a);
    auto diff = Point(0, 0);
    if(is_locked) {
      auto ratio = lock->m_width / lock->m_height;
      auto turned = step;
      if(std::abs(std::cos(angle)) <= std::abs(std::sin(angle))) {
        std::swap(turned.m_x, turned.m_y);
      }
      auto width = local->m_width + turned.m_x;
      auto height = width / ratio;
      if(turned.m_x == 0) {
        height = local->m_height + turned.m_y;
        width = height * ratio;
      }
      diff = Point(std::round(width - local->m_width),
        std::round(height - local->m_height));
    } else {
      auto moved = inverse->map(step) - inverse->map(Point(0, 0));
      if(std::abs(moved.m_x) > std::abs(moved.m_y)) {
        diff = Point(get_sign(moved.m_x) * magnitude, 0);
      } else {
        diff = Point(0, get_sign(moved.m_y) * magnitude);
      }
    }
    auto minimum = MINIMUM_NUDGED_SIZE;
    if(auto shape = node->get_shape();
        shape && std::holds_alternative<LineShape>(shape->m_geometry)) {
      minimum = 0;
    }
    auto resized = Rect(local->m_x, local->m_y,
      std::max(local->m_width + sign * std::abs(diff.m_x), minimum),
      std::max(local->m_height + sign * std::abs(diff.m_y), minimum));
    if(is_locked) {
      auto degrees = angle * 180 / std::numbers::pi;
      if(degrees < 0) {
        degrees += 360;
      }
      if(degrees >= 45 && degrees < 135) {
        resized.m_y -= diff.m_y;
      } else if(degrees >= 135 && degrees < 225) {
        resized.m_x -= diff.m_x;
        resized.m_y -= diff.m_y;
      } else if(degrees >= 225 && degrees < 315) {
        resized.m_x -= diff.m_x;
      }
    } else if(get_sign(diff.m_x + diff.m_y) != sign) {
      resized.m_x += diff.m_x;
      resized.m_y += diff.m_y;
    }
    return std::make_unique<TransformCommand>(std::vector{id},
      world * make_rect_mapping(*local, resized) * *inverse, label,
      TransformMode::RESIZE, ResizeRule::RESPONSIVE, measurer);
  }

  std::string get_nudge_label(Point direction) {
    if(direction.m_x < 0) {
      return "Nudge Left";
    } else if(direction.m_x > 0) {
      return "Nudge Right";
    } else if(direction.m_y < 0) {
      return "Nudge Up";
    }
    return "Nudge Down";
  }

  NodeId find_selected_ancestor(
      const Document& document, const Selection& selection, const NodeId& id) {
    auto node = document.find(id);
    while(node && !node->m_parent.is_nil()) {
      if(selection.contains(node->m_parent)) {
        return node->m_parent;
      }
      node = document.find(node->m_parent);
    }
    return NodeId();
  }

  bool is_kept(const Document& document, const Selection& selection,
      const NodeId& hit, const NodeId& target) {
    auto current = hit;
    while(auto node = document.find(current)) {
      if(selection.contains(current)) {
        return true;
      } else if(current == target) {
        return false;
      }
      current = node->m_parent;
    }
    return false;
  }

  bool is_stacked(const Document& document, const NodeId& id, Point axis) {
    auto node = document.find(id);
    auto parent = node ? document.find(node->m_parent) : nullptr;
    auto group = parent ? parent->get_group() : nullptr;
    if(!group || !group->m_stack) {
      return false;
    }
    auto is_horizontal =
      group->m_stack->m_orientation == Orientation::HORIZONTAL;
    if(is_horizontal != (axis.m_x != 0)) {
      return false;
    }
    auto items = get_stack_items(document, parent->get_id());
    return std::find(items.begin(), items.end(), id) != items.end();
  }

  NodeId find_clipped_hit(
      const ToolContext& context, Point point, double tolerance) {
    auto& document = context.get_document();
    auto& editor = context.get_editor();
    auto& hit_tester = context.get_hit_tester();
    auto test = [&] (const NodeId& id) {
      auto artboard = find_artboard(document, id);
      if(artboard.is_nil() || artboard == id ||
          document.get(artboard).get_artboard()->m_frame.contains(point)) {
        return NodeId();
      }
      return hit_tester.hit_within(id, point, tolerance, true);
    };
    auto test_children = [&] (const NodeId& id) {
      auto& children = document.get(id).m_children;
      for(auto i = children.rbegin(); i != children.rend(); ++i) {
        if(auto hit = test(*i); !hit.is_nil()) {
          return hit;
        }
      }
      return NodeId();
    };
    auto& container = editor.get_context().get_container();
    if(document.find(container)) {
      if(auto hit = test_children(container); !hit.is_nil()) {
        return hit;
      }
    }
    auto& nodes = editor.get_selection().get_nodes();
    for(auto& id : nodes) {
      if(auto hit = test(id); !hit.is_nil()) {
        return hit;
      }
    }
    for(auto& id : nodes) {
      auto artboard = find_artboard(document, id);
      if(!artboard.is_nil() && artboard != id) {
        if(auto hit = test_children(artboard); !hit.is_nil()) {
          return hit;
        }
      }
    }
    return NodeId();
  }

  NodeId find_hit(const ToolContext& context, Point point) {
    auto tolerance = SelectTool::HIT_TOLERANCE / context.get_zoom();
    auto& document = context.get_document();
    auto hit = context.get_hit_tester().hit(point, tolerance, true);
    auto clipped = find_clipped_hit(context, point, tolerance);
    if(clipped.is_nil() || (!hit.is_nil() &&
        find_artboard(document, hit) == find_artboard(document, clipped))) {
      return hit;
    }
    return clipped;
  }

  Rect make_rect(Point first, Point second) {
    auto left = std::min(first.m_x, second.m_x);
    auto top = std::min(first.m_y, second.m_y);
    return Rect(left, top, std::max(first.m_x, second.m_x) - left,
      std::max(first.m_y, second.m_y) - top);
  }

  bool is_inside(const std::array<Point, 4>& quad, Point point) {
    auto sign = 0.0;
    for(auto i = std::size_t(0); i != quad.size(); ++i) {
      auto edge = quad[(i + 1) % quad.size()] - quad[i];
      auto side = cross(edge, point - quad[i]);
      if(side == 0) {
        continue;
      }
      if(sign == 0) {
        sign = side;
      } else if((side > 0) != (sign > 0)) {
        return false;
      }
    }
    return true;
  }

  std::array<Point, 4> to_view(
      const Matrix& view, const std::array<Point, 4>& corners) {
    return std::array<Point, 4>({view.map(corners[0]), view.map(corners[1]),
      view.map(corners[2]), view.map(corners[3])});
  }

  NodeId get_container(
      const Document& document, const std::vector<NodeId>& nodes) {
    if(nodes.empty()) {
      return NodeId();
    }
    if(auto node = document.find(nodes.front())) {
      return node->m_parent;
    }
    return NodeId();
  }

  void measure(ToolContext& context, const std::optional<SelectionBox>& box,
      NodeId target, Qt::KeyboardModifiers modifiers) {
    auto& service = context.get_snap_service();
    auto& document = context.get_document();
    auto& selection = context.get_editor().get_selection();
    if(!box || !is_alt(modifiers) || selection.is_empty()) {
      service.measure(std::nullopt, Rect());
      return;
    }
    if(target.is_nil() || selection.contains(target)) {
      target = find_artboard(document, selection.get_nodes().front());
    }
    if(target.is_nil() || selection.contains(target)) {
      service.measure(std::nullopt, Rect());
      return;
    }
    auto bounds = get_world_bounds(document, target, BoundsType::GEOMETRY,
      context.get_text_measurer());
    if(!bounds) {
      service.measure(std::nullopt, Rect());
      return;
    }
    service.measure(box->m_transform.map(box->m_rect), *bounds);
  }

  std::optional<Path> get_outline(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    auto shape = node ? node->get_shape() : nullptr;
    if(!shape) {
      return std::nullopt;
    }
    auto world = get_world_transform(document, id);
    if(auto compound = std::get_if<CompoundShape>(&shape->m_geometry)) {
      return transform(*compound->m_path, world);
    }
    return transform(to_path(shape->m_geometry), world);
  }

  bool add_path_outline(CanvasOverlay& overlay, const Path& path,
      const Matrix& view, Color color) {
    auto polylines = flatten(transform(path, view), OUTLINE_FLATNESS);
    auto count = std::size_t(0);
    for(auto& polyline : polylines) {
      count += polyline.m_points.size();
    }
    if(count > MAXIMUM_OUTLINE_SEGMENTS) {
      return false;
    }
    auto add = [&] (Point start, Point end) {
      auto length = distance(start, end);
      if(length == 0) {
        return;
      }
      auto extension = (end - start) * (LINE_WIDTH / 2 / length);
      overlay.m_shapes.push_back(make_overlay_line(
        start - extension, end + extension, LINE_WIDTH, color));
    };
    for(auto& polyline : polylines) {
      auto& points = polyline.m_points;
      for(auto i = std::size_t(1); i < points.size(); ++i) {
        add(points[i - 1], points[i]);
      }
      if(polyline.m_is_closed && points.size() > 2) {
        add(points.back(), points.front());
      }
    }
    return true;
  }
}

SelectTool::SelectTool()
  : m_gesture(Gesture::NONE),
    m_press(0, 0),
    m_press_document(0, 0),
    m_pointer(0, 0),
    m_is_marquee_pending(false),
    m_delta(0, 0),
    m_is_applied(false),
    m_is_copied(false),
    m_marquee(0, 0, 0, 0) {}

void SelectTool::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  if(m_gesture != Gesture::NONE) {
    finish(context);
  }
  auto& editor = context.get_editor();
  auto& selection = editor.get_selection();
  auto& edit = editor.get_context();
  auto& document = context.get_document();
  auto modifiers = event.m_modifiers;
  m_press = event.m_position;
  m_press_document = context.to_document(m_press);
  m_clicked = NodeId();
  m_removed = NodeId();
  m_is_marquee_pending = false;
  m_is_applied = false;
  context.get_snap_service().measure(std::nullopt, Rect());
  m_badge = m_handles.find_lock_badge(context, m_press);
  if(!m_badge.is_nil()) {
    m_gesture = Gesture::BADGE;
    return;
  }
  if(!is_control(modifiers)) {
    auto is_grid = !m_layout_handles.get_grid(context).is_nil();
    if(is_grid && m_handles.press(context, event)) {
      m_gesture = Gesture::HANDLE;
      return;
    } else if(m_layout_handles.press(context, event,
        static_cast<bool>(m_handles.hover(context, event.m_position)))) {
      m_gesture = Gesture::LAYOUT;
      return;
    } else if(!is_grid && m_handles.press(context, event)) {
      m_gesture = Gesture::HANDLE;
      return;
    }
  }
  if(m_guides.press(context, event)) {
    m_gesture = Gesture::GUIDE;
    if(auto cursor = m_guides.hover(context, event.m_position)) {
      m_cursor = *cursor;
    }
    return;
  }
  auto select = [&] (const NodeId& target, const NodeId& hit) {
    auto& nodes = selection.get_nodes();
    auto artboard = find_artboard(document, hit);
    if(is_shift(modifiers)) {
      if(selection.contains(target)) {
        m_removed = target;
      } else {
        m_removed = find_selected_ancestor(document, selection, target);
        if(m_removed.is_nil()) {
          selection.add(target);
        }
      }
    } else if(!is_kept(document, selection, hit, target)) {
      if(nodes.size() > 1 && artboard != hit &&
          selection.contains(artboard)) {
        if(std::all_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
            return is_artboard(document, id);
          })) {
          m_clicked = target;
        }
      } else {
        selection.select(target);
      }
    }
    m_gesture = Gesture::PENDING;
  };
  auto title = context.get_titles().find(m_press);
  if(!title.is_nil()) {
    while(!edit.get_container().is_nil()) {
      edit.exit();
    }
    select(title, title);
    return;
  }
  auto hit = find_hit(context, m_press_document);
  auto background = NodeId();
  if(!hit.is_nil() && is_artboard(document, hit)) {
    background = hit;
    hit = NodeId();
  }
  if(!hit.is_nil()) {
    auto target = resolve_target(context, hit, modifiers, true);
    if(!target.is_nil()) {
      select(target, hit);
      return;
    }
  }
  m_gesture = Gesture::PENDING;
  m_is_marquee_pending = true;
  if(is_shift(modifiers)) {
    m_base = selection.get_nodes();
  } else {
    m_base.clear();
  }
  if(!edit.get_container().is_nil()) {
    auto box = get_selection_box(document,
      std::vector{edit.get_container()}, context.get_text_measurer());
    if(!box || !is_inside(box->get_corners(), m_press_document)) {
      while(!edit.get_container().is_nil()) {
        edit.exit();
      }
    }
  }
  if(!edit.get_container().is_nil()) {
    m_scope = edit.get_container();
  } else {
    m_scope = background;
  }
  if(!background.is_nil()) {
    edit.set_artboard(background);
  }
  if(!is_shift(modifiers)) {
    selection.clear();
  }
}

void SelectTool::move(ToolContext& context, const PointerEvent& event) {
  if(m_gesture == Gesture::NONE) {
    update_hover(context, event);
    return;
  }
  if(!event.m_buttons.testFlag(Qt::LeftButton)) {
    finish(context);
    update_hover(context, event);
    return;
  }
  if(m_gesture == Gesture::LAYOUT) {
    m_layout_handles.move(context, event);
    return;
  } else if(m_gesture == Gesture::HANDLE) {
    m_handles.move(context, event);
    return;
  } else if(m_gesture == Gesture::GUIDE) {
    m_guides.move(context, event);
    if(auto cursor = m_guides.hover(context, event.m_position)) {
      m_cursor = *cursor;
    }
    return;
  }
  if(m_gesture == Gesture::PENDING) {
    if(distance(event.m_position, m_press) < DRAG_THRESHOLD) {
      return;
    }
    begin_drag(context, event);
  }
  if(m_gesture == Gesture::MOVE) {
    update_move(context, event);
  } else if(m_gesture == Gesture::MARQUEE) {
    update_marquee(context, event);
  }
}

void SelectTool::release(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  auto& selection = context.get_editor().get_selection();
  if(m_gesture == Gesture::PENDING && !m_removed.is_nil()) {
    selection.remove(m_removed);
  } else if(m_gesture == Gesture::PENDING && !m_clicked.is_nil()) {
    selection.select(m_clicked);
  } else if(m_gesture == Gesture::GUIDE) {
    m_guides.release(context, event);
  } else if(m_gesture == Gesture::MOVE && m_is_applied) {
    drop(context, event);
  } else if(m_gesture == Gesture::BADGE &&
      m_handles.find_lock_badge(context, event.m_position) == m_badge) {
    auto& editor = context.get_editor();
    editor.get_history().seal();
    editor.execute(
      std::make_unique<SetLockCommand>(std::vector{m_badge}, false));
    editor.get_history().seal();
  }
  finish(context);
  update_hover(context, event);
}

void SelectTool::double_click(
    ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  if(m_handles.fit_text(context, event.m_position)) {
    return;
  }
  auto& editor = context.get_editor();
  auto& document = context.get_document();
  auto hit = find_hit(context, context.to_document(event.m_position));
  if(hit.is_nil() || is_artboard(document, hit)) {
    return;
  }
  auto& edit = editor.get_context();
  auto target = edit.resolve(hit);
  if(is_locked_node(document, target)) {
    return;
  }
  if(is_text_reference(document, target)) {
    editor.execute(std::make_unique<MakeEditableCommand>(
      std::vector{target}, "Edit Text"));
  }
  auto node = document.find(target);
  if(node && node->get_text()) {
    editor.get_selection().select(target);
    editor.get_text_selection().set(
      target, 0, node->get_text()->m_content->size());
    context.request_tool("text");
    return;
  }
  if(target == hit && is_point_editable(document, target)) {
    editor.get_selection().select(target);
    context.request_tool("path_edit");
    return;
  }
  if(!node || !is_enterable(*node) || target == hit) {
    return;
  }
  if(!edit.enter(target)) {
    return;
  }
  auto inner = edit.resolve(hit);
  if(is_repeat_grid(document, target) && inner != hit &&
      document.find(inner) && is_enterable(document.get(inner)) &&
      edit.enter(inner)) {
    inner = edit.resolve(hit);
  }
  if(!inner.is_nil()) {
    editor.get_selection().select(inner);
  }
}

void SelectTool::leave(ToolContext& context) {
  if(m_gesture == Gesture::NONE) {
    context.get_snap_service().measure(std::nullopt, Rect());
    context.get_editor().get_context().set_hover(NodeId());
    m_guides.cancel(context);
    m_handles.leave();
    m_cursor = ToolCursor();
  }
}

bool SelectTool::key_press(ToolContext& context, const KeyEvent& event) {
  auto& editor = context.get_editor();
  if(m_layout_handles.key_press(event)) {
    if(m_gesture == Gesture::NONE) {
      update_hover(context, PointerEvent(
        m_pointer, Qt::NoButton, Qt::NoButton, event.m_modifiers));
    }
    return true;
  }
  if(event.m_key == Qt::Key_Escape) {
    if(m_gesture != Gesture::NONE) {
      cancel(context);
      return true;
    }
    auto& edit = editor.get_context();
    if(!edit.get_container().is_nil()) {
      auto container = edit.get_container();
      edit.exit();
      editor.get_selection().select(container);
      return true;
    }
    if(!editor.get_selection().is_empty()) {
      editor.get_selection().clear();
      return true;
    }
    return false;
  }
  if(event.m_key == Qt::Key_Return || event.m_key == Qt::Key_Enter) {
    auto& nodes = editor.get_selection().get_nodes();
    if(m_gesture != Gesture::NONE || nodes.size() != 1 ||
        !is_point_editable(context.get_document(), nodes.front())) {
      return false;
    }
    context.request_tool("path_edit");
    return true;
  }
  auto delta = Point(0, 0);
  if(event.m_key == Qt::Key_Left) {
    delta = Point(-1, 0);
  } else if(event.m_key == Qt::Key_Right) {
    delta = Point(1, 0);
  } else if(event.m_key == Qt::Key_Up) {
    delta = Point(0, -1);
  } else if(event.m_key == Qt::Key_Down) {
    delta = Point(0, 1);
  } else {
    return false;
  }
  if(m_gesture != Gesture::NONE) {
    return true;
  }
  auto nodes = get_movable_nodes(context);
  if(nodes.empty()) {
    return false;
  }
  if(is_alt(event.m_modifiers)) {
    resize_nudge(context, std::move(nodes), delta, event.m_modifiers);
    return true;
  }
  std::erase_if(nodes, [&] (const auto& id) {
    return is_stacked(context.get_document(), id, delta);
  });
  if(nodes.empty()) {
    return true;
  }
  auto& document = context.get_document();
  auto& measurer = context.get_text_measurer();
  auto step = delta * NUDGE;
  if(is_shift(event.m_modifiers)) {
    step = delta * LARGE_NUDGE;
    auto artboard = editor.get_context().get_artboard();
    auto nudged = std::vector<NodeId>();
    for(auto& id : editor.get_selection().get_nodes()) {
      if(find_artboard(document, id) == artboard) {
        nudged.push_back(id);
      }
    }
    if(auto bounds = get_world_bounds(
        document, nudged, BoundsType::GEOMETRY, measurer)) {
      step =
        get_large_nudge(document, artboard, bounds->get_top_left(), delta);
    }
  }
  if(auto bounds =
      get_world_bounds(document, nodes, BoundsType::GEOMETRY, measurer)) {
    step = keep_on_canvas(*bounds, step);
  }
  if(!editor.get_history().is_dirty()) {
    editor.get_history().seal();
  }
  editor.merge(std::make_unique<TranslateCommand>(
    std::move(nodes), step, get_nudge_label(delta)));
  return true;
}

bool SelectTool::key_release(ToolContext& context, const KeyEvent& event) {
  if(m_layout_handles.key_release(event)) {
    if(m_gesture == Gesture::NONE) {
      update_hover(context, PointerEvent(
        m_pointer, Qt::NoButton, Qt::NoButton, event.m_modifiers));
    }
    return true;
  }
  return event.m_key == Qt::Key_Left || event.m_key == Qt::Key_Right ||
    event.m_key == Qt::Key_Up || event.m_key == Qt::Key_Down;
}

void SelectTool::resize_nudge(ToolContext& context, std::vector<NodeId> nodes,
    Point direction, Qt::KeyboardModifiers modifiers) {
  auto& document = context.get_document();
  auto& editor = context.get_editor();
  auto& measurer = context.get_text_measurer();
  if(nodes.size() == 1) {
    auto node = document.find(nodes.front());
    auto text = node ? node->get_text() : nullptr;
    if(text && std::holds_alternative<PositionedFrame>(text->m_frame)) {
      return;
    }
  }
  auto bounds =
    get_world_bounds(document, nodes, BoundsType::GEOMETRY, measurer);
  if(!bounds) {
    return;
  }
  auto step = direction * NUDGE;
  if(is_shift(modifiers)) {
    step = direction * LARGE_NUDGE;
    auto axis = Point(std::abs(direction.m_x), std::abs(direction.m_y));
    auto farthest = std::optional<Rect>();
    auto artboard = NodeId();
    for(auto& id : nodes) {
      auto box =
        get_world_bounds(document, id, BoundsType::GEOMETRY, measurer);
      if(box && (!farthest || dot(axis, box->get_bottom_right()) >
          dot(axis, farthest->get_bottom_right()))) {
        farthest = box;
        artboard = find_artboard(document, id);
      }
    }
    if(farthest) {
      step = get_large_nudge(
        document, artboard, farthest->get_bottom_right(), direction);
    }
  }
  if(!is_within_canvas(Rect(bounds->m_x, bounds->m_y,
      bounds->m_width + step.m_x, bounds->m_height + step.m_y))) {
    return;
  }
  auto label = get_resize_nudge_label(direction);
  auto command = std::unique_ptr<Command>();
  if(nodes.size() == 1) {
    command =
      make_nudge_resize(document, nodes.front(), step, label, measurer);
  } else {
    auto resized = Rect(bounds->m_x, bounds->m_y,
      std::max(bounds->m_width + step.m_x, MINIMUM_NUDGED_SIZE),
      std::max(bounds->m_height + step.m_y, MINIMUM_NUDGED_SIZE));
    command = std::make_unique<TransformCommand>(std::move(nodes),
      make_rect_mapping(*bounds, resized), label, TransformMode::RESIZE,
      ResizeRule::RESPONSIVE, measurer);
  }
  if(!command) {
    return;
  }
  if(!editor.get_history().is_dirty()) {
    editor.get_history().seal();
  }
  editor.merge(std::move(command));
}

void SelectTool::cancel(ToolContext& context) {
  auto& editor = context.get_editor();
  if(m_gesture == Gesture::LAYOUT) {
    m_layout_handles.cancel(context);
  } else if(m_gesture == Gesture::HANDLE) {
    m_handles.cancel(context);
  } else if(m_gesture == Gesture::GUIDE) {
    m_guides.cancel(context);
  } else if(m_gesture == Gesture::MOVE && m_is_applied) {
    editor.undo();
  } else if(m_gesture == Gesture::MARQUEE) {
    editor.get_selection().select(m_base);
  }
  m_is_applied = false;
  m_clicked = NodeId();
  m_removed = NodeId();
  finish(context);
}

bool SelectTool::is_busy() const {
  return m_gesture != Gesture::NONE;
}

ToolCursor SelectTool::get_cursor() const {
  if(m_gesture == Gesture::HANDLE) {
    return m_handles.get_cursor();
  }
  return m_cursor;
}

void SelectTool::draw(
    const ToolContext& context, CanvasOverlay& overlay) const {
  m_guides.draw(context, overlay);
  auto& editor = context.get_editor();
  auto& selection = editor.get_selection();
  auto& document = context.get_document();
  auto& view = context.get_view();
  auto& edit = editor.get_context();
  draw_context(context, overlay);
  auto is_dragging = m_gesture == Gesture::MOVE ||
    (m_gesture == Gesture::HANDLE && m_handles.is_transforming());
  auto& hover = edit.get_hover();
  if(!is_dragging && m_gesture != Gesture::MARQUEE && !hover.is_nil() &&
      !selection.contains(hover) && !is_artboard(document, hover)) {
    draw_hover(context, hover, overlay);
  }
  if(m_gesture != Gesture::MOVE && m_gesture != Gesture::GUIDE) {
    m_handles.draw(context, overlay, m_gesture != Gesture::MARQUEE);
  }
  if(!is_dragging && m_gesture != Gesture::MARQUEE) {
    m_layout_handles.draw(context, overlay);
  }
  if(m_gesture == Gesture::MARQUEE) {
    auto corners = std::array<Point, 4>({m_marquee.get_top_left(),
      Point(m_marquee.get_right(), m_marquee.get_top()),
      m_marquee.get_bottom_right(),
      Point(m_marquee.get_left(), m_marquee.get_bottom())});
    auto rect = make_rect(view.map(corners[0]), view.map(corners[2]));
    auto shape = OverlayShape();
    shape.m_center = rect.get_center();
    shape.m_size = rect.get_size();
    shape.m_fill = MARQUEE_FILL;
    shape.m_stroke = SELECTION_COLOR;
    shape.m_stroke_width = LINE_WIDTH;
    overlay.m_shapes.push_back(shape);
  }
}

const std::optional<SelectionBox>& SelectTool::get_box(
    const ToolContext& context, const NodeId& id, CachedBox& cache) const {
  if(cache.m_id != id || cache.m_revision != context.get_revision()) {
    cache.m_id = id;
    cache.m_revision = context.get_revision();
    cache.m_box = get_selection_box(
      context.get_document(), std::vector{id}, context.get_text_measurer());
    cache.m_outline = get_outline(context.get_document(), id);
  }
  return cache.m_box;
}

NodeId SelectTool::resolve_target(ToolContext& context, const NodeId& hit,
    Qt::KeyboardModifiers modifiers, bool is_entering) const {
  auto& document = context.get_document();
  auto& edit = context.get_editor().get_context();
  if(is_control(modifiers)) {
    if(is_entering) {
      auto node = document.find(hit);
      auto parent = node ? document.find(node->m_parent) : nullptr;
      if(parent && is_enterable(*parent)) {
        edit.enter(parent->get_id());
      } else {
        while(!edit.get_container().is_nil()) {
          edit.exit();
        }
      }
    }
    return hit;
  }
  if(is_entering) {
    while(!edit.get_container().is_nil() &&
        !is_ancestor(document, edit.get_container(), hit)) {
      edit.exit();
    }
  }
  return edit.resolve(hit);
}

void SelectTool::begin_drag(ToolContext& context, const PointerEvent& event) {
  m_clicked = NodeId();
  m_removed = NodeId();
  if(m_is_marquee_pending) {
    m_gesture = Gesture::MARQUEE;
    return;
  }
  m_nodes = get_movable_nodes(context);
  auto box = m_handles.get_box(context);
  if(m_nodes.empty() || !box) {
    m_gesture = Gesture::NONE;
    return;
  }
  m_box = *box;
  m_delta = Point(0, 0);
  m_gesture = Gesture::MOVE;
  m_is_copied = false;
  context.get_editor().get_history().seal();
  if(is_alt(event.m_modifiers)) {
    copy(context);
  }
  context.get_editor().get_history().hold_layout(m_nodes);
  context.get_snap_service().begin(
    get_container(context.get_document(), m_nodes), m_nodes);
}

void SelectTool::copy(ToolContext& context) {
  m_is_copied = true;
  auto& document = context.get_document();
  if(std::any_of(m_nodes.begin(), m_nodes.end(), [&] (const auto& id) {
      return is_artboard(document, id);
    })) {
    return;
  }
  auto& editor = context.get_editor();
  if(m_is_applied) {
    editor.undo();
    m_is_applied = false;
  }
  m_delta = Point(0, 0);
  if(editor.execute(std::make_unique<DragCopyCommand>(m_nodes))) {
    m_nodes = get_top_level(document, editor.get_selection().get_nodes());
    m_is_applied = true;
  }
}

void SelectTool::update_move(ToolContext& context, const PointerEvent& event) {
  if(!m_is_copied && is_alt(event.m_modifiers)) {
    auto& history = context.get_editor().get_history();
    history.release_layout();
    copy(context);
    history.hold_layout(m_nodes);
    context.get_snap_service().begin(
      get_container(context.get_document(), m_nodes), m_nodes);
  }
  auto raw = context.to_document(event.m_position) - m_press_document;
  if(is_shift(event.m_modifiers)) {
    if(std::abs(raw.m_x) >= std::abs(raw.m_y)) {
      raw.m_y = 0;
    } else {
      raw.m_x = 0;
    }
  }
  auto bounds = m_box.m_transform.map(m_box.m_rect);
  auto origin = bounds.get_top_left();
  auto delta = Point(std::round(origin.m_x + raw.m_x) - origin.m_x,
    std::round(origin.m_y + raw.m_y) - origin.m_y);
  if(raw.m_x == 0) {
    delta.m_x = 0;
  }
  if(raw.m_y == 0) {
    delta.m_y = 0;
  }
  delta = context.get_snap_service().snap_move(
    bounds, delta, event.m_modifiers);
  if(raw.m_x == 0) {
    delta.m_x = 0;
  }
  if(raw.m_y == 0) {
    delta.m_y = 0;
  }
  delta = keep_on_canvas(bounds, delta);
  auto step = delta - m_delta;
  if(step == Point(0, 0)) {
    return;
  }
  context.get_editor().merge(
    std::make_unique<TranslateCommand>(m_nodes, step));
  m_delta = delta;
  m_is_applied = true;
}

void SelectTool::update_marquee(
    ToolContext& context, const PointerEvent& event) {
  auto& document = context.get_document();
  auto& hit_tester = context.get_hit_tester();
  m_marquee =
    make_rect(m_press_document, context.to_document(event.m_position));
  auto hits = hit_tester.query(m_scope, m_marquee);
  if(m_scope.is_nil()) {
    auto enclosed = hit_tester.query(m_scope, m_marquee, true);
    auto artboards = std::unordered_set<NodeId>(
      enclosed.begin(), enclosed.end());
    std::erase_if(hits, [&] (const auto& id) {
      return is_artboard(document, id) && !artboards.contains(id);
    });
  }
  auto& selection = context.get_editor().get_selection();
  auto nodes = m_base;
  auto members = std::unordered_set<NodeId>(nodes.begin(), nodes.end());
  auto reached = std::unordered_set<NodeId>(hits.begin(), hits.end());
  for(auto& id : selection.get_nodes()) {
    if(reached.contains(id) && members.insert(id).second) {
      nodes.push_back(id);
    }
  }
  for(auto& id : hits) {
    if(members.insert(id).second) {
      nodes.push_back(id);
    }
  }
  if(selection.get_nodes() != nodes) {
    selection.select(nodes);
  }
}

void SelectTool::update_hover(
    ToolContext& context, const PointerEvent& event) {
  auto& edit = context.get_editor().get_context();
  m_pointer = event.m_position;
  m_cursor = ToolCursor();
  auto is_grid = !m_layout_handles.get_grid(context).is_nil();
  auto handle = m_handles.hover(context, event.m_position);
  auto layout = m_layout_handles.hover(
    context, event.m_position, event.m_modifiers, static_cast<bool>(handle));
  if(is_grid && handle) {
    m_cursor = *handle;
  } else if(layout) {
    m_cursor = *layout;
  } else if(handle) {
    m_cursor = *handle;
  } else if(auto cursor = m_guides.hover(context, event.m_position)) {
    m_cursor = *cursor;
  }
  auto hover = context.get_titles().find(event.m_position);
  if(hover.is_nil()) {
    auto hit = find_hit(context, context.to_document(event.m_position));
    if(!hit.is_nil() && !is_artboard(context.get_document(), hit)) {
      hover = resolve_target(context, hit, event.m_modifiers, false);
    }
  }
  if(is_locked_node(context.get_document(), hover)) {
    edit.set_hover(NodeId());
  } else {
    edit.set_hover(hover);
  }
  measure(context, m_handles.get_box(context), hover, event.m_modifiers);
}

void SelectTool::drop(ToolContext& context, const PointerEvent& event) {
  auto& editor = context.get_editor();
  auto& document = context.get_document();
  editor.get_history().set_pointer(context.to_document(event.m_position));
  editor.get_history().release_settling();
  auto primary = editor.get_selection().get_primary();
  if(document.find(primary)) {
    editor.get_context().set_artboard(find_artboard(document, primary));
  }
}

void SelectTool::finish(ToolContext& context) {
  if(m_gesture == Gesture::LAYOUT) {
    m_layout_handles.release(context);
  } else if(m_gesture == Gesture::HANDLE) {
    m_handles.release(context);
  } else if(m_gesture == Gesture::MOVE) {
    context.get_editor().get_history().release_layout();
    context.get_editor().get_history().seal();
    context.get_snap_service().end();
  }
  m_gesture = Gesture::NONE;
  m_is_marquee_pending = false;
  m_marquee = Rect(0, 0, 0, 0);
  m_nodes.clear();
  m_base.clear();
}

void SelectTool::draw_context(
    const ToolContext& context, CanvasOverlay& overlay) const {
  auto& document = context.get_document();
  auto& container = context.get_editor().get_context().get_container();
  auto chain = std::vector<NodeId>();
  for(auto node = document.find(container); node && !node->get_artboard();
      node = document.find(node->m_parent)) {
    chain.push_back(node->get_id());
    if(node->m_parent.is_nil()) {
      break;
    }
  }
  if(chain.empty()) {
    return;
  }
  auto get_color = [&] (const NodeId& id) {
    if(is_locked_node(document, id)) {
      return LOCKED_CONTEXT_COLOR;
    } else if(is_repeat_grid(document, id) ||
        get_instance_role(document.get(id))) {
      return GRID_CONTEXT_COLOR;
    }
    return CONTEXT_COLOR;
  };
  auto& view = context.get_view();
  auto outline = [&] (const NodeId& id, std::size_t slot) ->
      const std::optional<SelectionBox>& {
    auto& box = get_box(context, id, m_context_boxes[slot]);
    if(box) {
      add_context_outline(
        overlay, to_view(view, box->get_corners()), get_color(id));
    }
    return box;
  };
  auto& inner = document.get(container);
  if(!get_instance_role(inner) && !is_scroll_group(document, container) &&
      has_visible_leaf(document, container)) {
    outline(container, 0);
  }
  auto find = [&] (auto is_kind) {
    auto i = std::find_if(chain.begin(), chain.end(), is_kind);
    if(i == chain.end()) {
      return NodeId();
    }
    return *i;
  };
  auto grid = find([&] (const auto& id) {
    return is_repeat_grid(document, id);
  });
  if(!grid.is_nil()) {
    outline(grid, 1);
  }
  auto component = find([&] (const auto& id) {
    return get_instance_role(document.get(id)) != nullptr;
  });
  if(!component.is_nil()) {
    outline(component, 2);
  }
  auto scroll = find([&] (const auto& id) {
    return is_scroll_group(document, id);
  });
  if(scroll.is_nil()) {
    return;
  }
  auto& box = outline(scroll, 3);
  if(!box) {
    return;
  }
  auto direction =
    document.get(scroll).get_group()->m_scroll_group->m_direction;
  auto corners = to_view(view, box->get_corners());
  for(auto i = std::size_t(0); i != corners.size(); ++i) {
    auto is_shown = i % 2 == 0 ? direction != ScrollDirection::HORIZONTAL :
      direction != ScrollDirection::VERTICAL;
    if(is_shown) {
      add_dashed_line(overlay, corners[i], corners[(i + 1) % corners.size()],
        SELECTION_COLOR);
    }
  }
}

void SelectTool::draw_hover(const ToolContext& context, const NodeId& hover,
    CanvasOverlay& overlay) const {
  auto& box = get_box(context, hover, m_hover_box);
  auto& view = context.get_view();
  if(m_hover_box.m_outline && add_path_outline(
      overlay, *m_hover_box.m_outline, view, SELECTION_COLOR)) {
    return;
  }
  if(box) {
    add_box_outline(
      overlay, inset_outline(to_view(view, box->get_corners())),
      SELECTION_COLOR);
  }
}

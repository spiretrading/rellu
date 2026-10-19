#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>
#include <variant>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CornerRadiusCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Tools/PathPoints.hpp"
#include "Rellu/Tools/SelectTool.hpp"
#include "Rellu/Tools/TextTool.hpp"

using namespace Rellu;

namespace {
  constexpr auto SELECTION_COLOR = make_color(20, 115, 230);
  constexpr auto CONTEXT_COLOR = make_color(20, 115, 230, 0.45);
  constexpr auto MARQUEE_FILL = make_color(20, 115, 230, 0.08);
  constexpr auto HANDLE_FILL = make_color(255, 255, 255);
  constexpr auto LINE_WIDTH = 1.0;
  constexpr auto MINIMUM_SIZE = 1.0;
  constexpr auto ANGLE_TOLERANCE = 1e-12;
  constexpr auto MAXIMUM_OUTLINES = std::size_t(1000);

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

  bool is_locked(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    while(node) {
      if(node->m_header.m_is_locked) {
        return true;
      }
      if(node->m_parent.is_nil()) {
        return false;
      }
      node = document.find(node->m_parent);
    }
    return false;
  }

  double normalize(double angle) {
    while(angle > std::numbers::pi) {
      angle -= 2 * std::numbers::pi;
    }
    while(angle <= -std::numbers::pi) {
      angle += 2 * std::numbers::pi;
    }
    return angle;
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

  void add_outline(CanvasOverlay& overlay, const std::array<Point, 4>& corners,
      Color color) {
    for(auto i = std::size_t(0); i != corners.size(); ++i) {
      auto start = corners[i];
      auto end = corners[(i + 1) % corners.size()];
      auto length = distance(start, end);
      if(length == 0) {
        continue;
      }
      auto extension = (end - start) * (LINE_WIDTH / 2 / length);
      overlay.m_shapes.push_back(make_overlay_line(
        start - extension, end + extension, LINE_WIDTH, color));
    }
  }

  bool is_handle_shown(Handle handle, const std::array<Point, 4>& corners) {
    if(is_corner(handle)) {
      return true;
    }
    auto minimum = 4 * SelectTool::HANDLE_REACH;
    if(handle == Handle::TOP || handle == Handle::BOTTOM) {
      return distance(corners[0], corners[1]) >= minimum;
    }
    return distance(corners[1], corners[2]) >= minimum;
  }

  Point get_view_point(
      const ToolContext& context, const SelectionBox& box, Handle handle) {
    return context.to_view(box.m_transform.map(box.get_point(handle)));
  }

  double get_angle(Point from, Point to) {
    return std::atan2(to.m_y - from.m_y, to.m_x - from.m_x);
  }

  double get_corner_radius(const CornerRadii& radii, std::size_t corner) {
    if(corner == 0) {
      return radii.m_top_left;
    } else if(corner == 1) {
      return radii.m_top_right;
    } else if(corner == 2) {
      return radii.m_bottom_right;
    }
    return radii.m_bottom_left;
  }

  Point get_corner_point(const Rect& rect, std::size_t corner) {
    if(corner == 0) {
      return rect.get_top_left();
    } else if(corner == 1) {
      return Point(rect.get_right(), rect.get_top());
    } else if(corner == 2) {
      return rect.get_bottom_right();
    }
    return Point(rect.get_left(), rect.get_bottom());
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

  SnapAxes get_snap_axes(Handle handle) {
    if(is_corner(handle)) {
      return SnapAxes::BOTH;
    } else if(handle == Handle::LEFT || handle == Handle::RIGHT) {
      return SnapAxes::HORIZONTAL;
    }
    return SnapAxes::VERTICAL;
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

  Point get_inward(std::size_t corner) {
    if(corner == 0) {
      return Point(1, 1);
    } else if(corner == 1) {
      return Point(-1, 1);
    } else if(corner == 2) {
      return Point(-1, -1);
    }
    return Point(1, -1);
  }
}

SelectTool::SelectTool()
  : m_gesture(Gesture::NONE),
    m_press(0, 0),
    m_press_document(0, 0),
    m_is_reselecting(false),
    m_is_marquee_pending(false),
    m_handle(Handle::TOP_LEFT),
    m_grab_offset(0, 0),
    m_delta(0, 0),
    m_resized(0, 0, 0, 0),
    m_angle(0),
    m_raw_angle(0),
    m_is_applied(false),
    m_marquee(0, 0, 0, 0),
    m_nudge_key(0),
    m_corner(0),
    m_radius(0),
    m_inset(0) {}

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
  m_is_reselecting = false;
  m_is_marquee_pending = false;
  m_is_applied = false;
  context.get_snap_service().measure(std::nullopt, Rect());
  if(!is_control(modifiers)) {
    if(m_layout_handles.press(context, event)) {
      m_gesture = Gesture::LAYOUT;
      return;
    }
    if(auto grab = find_grab(context, m_press)) {
      m_nodes = get_movable(context);
      m_box = *get_box(context);
      m_handle = grab->m_handle;
      m_cursor = get_cursor();
      if(grab->m_target == Target::HANDLE) {
        m_gesture = Gesture::RESIZE;
        m_resized = m_box.m_rect;
        m_grab_offset = Point(0, 0);
        if(auto inverse = invert(m_box.m_transform)) {
          m_grab_offset =
            m_box.get_point(m_handle) - inverse->map(m_press_document);
        }
      } else {
        m_gesture = Gesture::ROTATE;
        m_angle = 0;
        m_raw_angle = 0;
      }
      editor.get_history().seal();
      context.get_snap_service().begin(
        get_container(document, m_nodes), m_nodes);
      return;
    }
    if(auto corner = find_radius_handle(context, m_press)) {
      m_radius_handles = get_radius_handles(context);
      m_corner = *corner;
      m_radius = get_corner_radius(m_radius_handles->m_radii, m_corner);
      m_inset = get_inset(context, m_press);
      m_gesture = Gesture::RADIUS;
      m_cursor = get_cursor();
      editor.get_history().seal();
      return;
    }
  }
  auto select = [&] (const NodeId& target) {
    if(is_shift(modifiers)) {
      selection.toggle(target);
    } else if(!selection.contains(target)) {
      selection.select(target);
    } else {
      m_is_reselecting = selection.get_size() > 1;
    }
    m_clicked = target;
    if(selection.contains(target)) {
      m_gesture = Gesture::PENDING;
    }
  };
  auto title = context.get_titles().find(m_press);
  if(!title.is_nil()) {
    while(!edit.get_container().is_nil()) {
      edit.exit();
    }
    select(title);
    return;
  }
  auto hit = context.get_hit_tester().hit(
    m_press_document, HIT_TOLERANCE / context.get_zoom());
  auto background = NodeId();
  if(!hit.is_nil() && is_artboard(document, hit)) {
    background = hit;
    hit = NodeId();
  }
  if(!hit.is_nil()) {
    auto target = resolve_target(context, hit, modifiers, true);
    if(!target.is_nil()) {
      select(target);
      return;
    }
  }
  if(!is_shift(modifiers)) {
    if(auto box = get_box(context)) {
      auto& nodes = selection.get_nodes();
      if(std::none_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
          return is_artboard(document, id);
        }) && is_inside(box->get_corners(), m_press_document)) {
        m_gesture = Gesture::PENDING;
        return;
      }
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
  }
  if(m_gesture == Gesture::PENDING) {
    if(distance(event.m_position, m_press) < DRAG_THRESHOLD) {
      return;
    }
    begin_drag(context, event);
  }
  if(m_gesture == Gesture::MOVE) {
    update_move(context, event);
  } else if(m_gesture == Gesture::RESIZE) {
    update_resize(context, event);
  } else if(m_gesture == Gesture::ROTATE) {
    update_rotate(context, event);
  } else if(m_gesture == Gesture::MARQUEE) {
    update_marquee(context, event);
  } else if(m_gesture == Gesture::RADIUS) {
    update_radius(context, event);
  }
}

void SelectTool::release(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  if(m_gesture == Gesture::PENDING && m_is_reselecting &&
      !m_clicked.is_nil()) {
    context.get_editor().get_selection().select(m_clicked);
  }
  finish(context);
  update_hover(context, event);
}

void SelectTool::double_click(
    ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  auto& editor = context.get_editor();
  auto& document = context.get_document();
  auto hit = context.get_hit_tester().hit(context.to_document(
    event.m_position), HIT_TOLERANCE / context.get_zoom());
  if(hit.is_nil() || is_artboard(document, hit)) {
    return;
  }
  auto& edit = editor.get_context();
  auto target = edit.resolve(hit);
  if(is_text_reference(document, target)) {
    editor.execute(std::make_unique<MakeEditableCommand>(
      std::vector{target}, "Edit Text"));
  }
  auto node = document.find(target);
  if(node && node->get_text()) {
    editor.get_selection().select(target);
    auto caret = find_text_caret(
      context, target, event.m_position, get_default_font_library());
    editor.get_text_selection().set(target, caret, caret);
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
    m_cursor = ToolCursor();
  }
}

bool SelectTool::key_press(ToolContext& context, const KeyEvent& event) {
  auto& editor = context.get_editor();
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
  auto nodes = get_movable(context);
  if(nodes.empty()) {
    return false;
  }
  if(!event.m_is_repeat || m_nudge_key != event.m_key) {
    editor.get_history().seal();
  }
  m_nudge_key = event.m_key;
  auto distance = is_shift(event.m_modifiers) ? LARGE_NUDGE : NUDGE;
  editor.merge(make_nudge_command(std::move(nodes), delta * distance));
  return true;
}

bool SelectTool::key_release(ToolContext& context, const KeyEvent& event) {
  if(event.m_key != Qt::Key_Left && event.m_key != Qt::Key_Right &&
      event.m_key != Qt::Key_Up && event.m_key != Qt::Key_Down) {
    return false;
  }
  if(!event.m_is_repeat && event.m_key == m_nudge_key) {
    context.get_editor().get_history().seal();
    m_nudge_key = 0;
  }
  return true;
}

void SelectTool::cancel(ToolContext& context) {
  auto& editor = context.get_editor();
  if(m_gesture == Gesture::LAYOUT) {
    m_layout_handles.cancel(context);
  } else if((m_gesture == Gesture::MOVE || m_gesture == Gesture::RESIZE ||
      m_gesture == Gesture::ROTATE || m_gesture == Gesture::RADIUS) &&
      m_is_applied) {
    editor.undo();
  } else if(m_gesture == Gesture::MARQUEE) {
    editor.get_selection().select(m_base);
  }
  m_is_applied = false;
  m_is_reselecting = false;
  finish(context);
}

bool SelectTool::is_busy() const {
  return m_gesture != Gesture::NONE;
}

ToolCursor SelectTool::get_cursor() const {
  return m_cursor;
}

void SelectTool::draw(
    const ToolContext& context, CanvasOverlay& overlay) const {
  auto& editor = context.get_editor();
  auto& selection = editor.get_selection();
  auto& document = context.get_document();
  auto& view = context.get_view();
  auto& edit = editor.get_context();
  if(!edit.get_container().is_nil()) {
    if(auto& box = get_box(context, edit.get_container(), m_container_box)) {
      add_outline(overlay, to_view(view, box->get_corners()), CONTEXT_COLOR);
    }
  }
  auto is_dragging = m_gesture == Gesture::MOVE ||
    m_gesture == Gesture::RESIZE || m_gesture == Gesture::ROTATE;
  auto& hover = edit.get_hover();
  if(!is_dragging && m_gesture != Gesture::MARQUEE && !hover.is_nil() &&
      !selection.contains(hover) && !is_artboard(document, hover)) {
    if(auto& box = get_box(context, hover, m_hover_box)) {
      add_outline(
        overlay, to_view(view, box->get_corners()), SELECTION_COLOR);
    }
  }
  auto box = std::optional<SelectionBox>();
  if(is_dragging) {
    box = get_gesture_box();
  } else {
    update_cache(context);
    box = m_cached_box;
    if(m_cached_outlines.size() > 1) {
      for(auto& outline : m_cached_outlines) {
        add_outline(
          overlay, to_view(view, outline.get_corners()), SELECTION_COLOR);
      }
    }
  }
  if(box) {
    draw_box(context, *box, overlay, m_gesture != Gesture::MOVE &&
      !get_movable(context).empty() &&
      m_layout_handles.get_grid(context).is_nil());
  }
  if(!is_dragging && m_gesture != Gesture::MARQUEE) {
    m_layout_handles.draw(context, overlay);
  }
  if(!is_dragging && m_gesture != Gesture::MARQUEE) {
    if(auto handles = get_radius_handles(context)) {
      for(auto& point : handles->m_points) {
        auto shape = OverlayShape();
        shape.m_center = point;
        shape.m_size =
          Size(2 * RADIUS_HANDLE_RADIUS, 2 * RADIUS_HANDLE_RADIUS);
        shape.m_radius = RADIUS_HANDLE_RADIUS;
        shape.m_fill = HANDLE_FILL;
        shape.m_stroke = SELECTION_COLOR;
        shape.m_stroke_width = LINE_WIDTH;
        overlay.m_shapes.push_back(shape);
      }
    }
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

void SelectTool::update_cache(const ToolContext& context) const {
  auto& nodes = context.get_editor().get_selection().get_nodes();
  if(m_revision == context.get_revision() && m_cached_nodes == nodes) {
    return;
  }
  m_revision = context.get_revision();
  m_cached_nodes = nodes;
  auto& document = context.get_document();
  auto& measurer = context.get_text_measurer();
  m_cached_box = get_selection_box(document, nodes, measurer);
  m_cached_outlines.clear();
  if(nodes.size() > 1) {
    for(auto& id : get_top_level(document, nodes)) {
      if(m_cached_outlines.size() == MAXIMUM_OUTLINES) {
        break;
      }
      if(auto box = get_selection_box(document, std::vector{id}, measurer)) {
        m_cached_outlines.push_back(*box);
      }
    }
  }
}

std::optional<SelectionBox> SelectTool::get_box(
    const ToolContext& context) const {
  update_cache(context);
  return m_cached_box;
}

const std::optional<SelectionBox>& SelectTool::get_box(
    const ToolContext& context, const NodeId& id, CachedBox& cache) const {
  if(cache.m_id != id || cache.m_revision != context.get_revision()) {
    cache.m_id = id;
    cache.m_revision = context.get_revision();
    cache.m_box = get_selection_box(
      context.get_document(), std::vector{id}, context.get_text_measurer());
  }
  return cache.m_box;
}

std::vector<NodeId> SelectTool::get_movable(
    const ToolContext& context) const {
  auto& document = context.get_document();
  auto nodes = get_top_level(
    document, context.get_editor().get_selection().get_nodes());
  std::erase_if(nodes, [&] (const auto& id) {
    return is_locked(document, id);
  });
  return nodes;
}

bool SelectTool::is_rotatable(const ToolContext& context) const {
  auto& document = context.get_document();
  auto& nodes = context.get_editor().get_selection().get_nodes();
  return std::none_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
    return is_artboard(document, id);
  });
}

std::optional<SelectTool::Grab> SelectTool::find_grab(
    const ToolContext& context, Point position) const {
  auto box = get_box(context);
  if(!box || get_movable(context).empty() ||
      !m_layout_handles.get_grid(context).is_nil()) {
    return std::nullopt;
  }
  auto corners = to_view(context.get_view(), box->get_corners());
  auto find_handle = [&] (bool is_corner_wanted) -> std::optional<Grab> {
    for(auto handle : HANDLES) {
      if(is_corner(handle) != is_corner_wanted ||
          !is_handle_shown(handle, corners)) {
        continue;
      }
      if(distance(get_view_point(context, *box, handle), position) <=
          HANDLE_REACH) {
        return Grab(Target::HANDLE, handle);
      }
    }
    return std::nullopt;
  };
  if(auto grab = find_handle(true)) {
    return grab;
  }
  if(auto grab = find_handle(false)) {
    return grab;
  }
  if(!is_rotatable(context) || is_inside(corners, position)) {
    return std::nullopt;
  }
  for(auto handle : HANDLES) {
    if(is_corner(handle) && distance(get_view_point(context, *box, handle),
        position) <= ROTATION_REACH) {
      return Grab(Target::ROTATION, handle);
    }
  }
  return std::nullopt;
}

std::optional<SelectTool::RadiusHandles> SelectTool::get_radius_handles(
    const ToolContext& context) const {
  auto& document = context.get_document();
  auto& nodes = context.get_editor().get_selection().get_nodes();
  if(nodes.size() != 1 || get_movable(context).size() != 1) {
    return std::nullopt;
  }
  auto node = document.find(nodes.front());
  auto shape = node ? node->get_shape() : nullptr;
  auto rectangle =
    shape ? std::get_if<RectangleShape>(&shape->m_geometry) : nullptr;
  if(!rectangle) {
    return std::nullopt;
  }
  auto& rect = rectangle->m_bounds;
  auto transform = get_world_transform(document, node->get_id());
  auto& view = context.get_view();
  auto scale = std::sqrt(std::abs((view * transform).get_determinant()));
  if(!(rect.m_width * scale >= RADIUS_HANDLE_SIDE) ||
      !(rect.m_height * scale >= RADIUS_HANDLE_SIDE)) {
    return std::nullopt;
  }
  auto handles = RadiusHandles(node->get_id(), rect, transform,
    clamp_corner_radii(rectangle->m_radii, rect.get_size()),
    std::min(rect.m_width, rect.m_height) / 2, std::array<Point, 4>());
  auto minimum = RADIUS_HANDLE_INSET / scale;
  for(auto i = std::size_t(0); i != handles.m_points.size(); ++i) {
    auto inset = std::min(std::max(get_corner_radius(handles.m_radii, i),
      minimum), handles.m_limit);
    handles.m_points[i] = context.to_view(transform.map(
      get_corner_point(rect, i) + get_inward(i) * inset));
  }
  return handles;
}

std::optional<std::size_t> SelectTool::find_radius_handle(
    const ToolContext& context, Point position) const {
  auto handles = get_radius_handles(context);
  if(!handles) {
    return std::nullopt;
  }
  for(auto i = std::size_t(0); i != handles->m_points.size(); ++i) {
    if(distance(handles->m_points[i], position) <= HANDLE_REACH) {
      return i;
    }
  }
  return std::nullopt;
}

double SelectTool::get_inset(const ToolContext& context, Point position) const {
  auto inverse = invert(m_radius_handles->m_transform);
  if(!inverse) {
    return 0;
  }
  auto offset = inverse->map(context.to_document(position)) -
    get_corner_point(m_radius_handles->m_rect, m_corner);
  auto inward = get_inward(m_corner);
  return (offset.m_x * inward.m_x + offset.m_y * inward.m_y) / 2;
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
  m_is_reselecting = false;
  if(m_is_marquee_pending) {
    m_gesture = Gesture::MARQUEE;
    return;
  }
  m_nodes = get_movable(context);
  auto box = get_box(context);
  if(m_nodes.empty() || !box) {
    m_gesture = Gesture::NONE;
    return;
  }
  m_box = *box;
  m_delta = Point(0, 0);
  m_gesture = Gesture::MOVE;
  context.get_editor().get_history().seal();
  context.get_editor().get_history().hold_layout(m_nodes);
  context.get_snap_service().begin(
    get_container(context.get_document(), m_nodes), m_nodes);
}

void SelectTool::update_move(ToolContext& context, const PointerEvent& event) {
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
  auto step = delta - m_delta;
  if(step == Point(0, 0)) {
    return;
  }
  context.get_editor().merge(std::make_unique<TranslateCommand>(m_nodes, step));
  m_delta = delta;
  m_is_applied = true;
}

void SelectTool::update_resize(
    ToolContext& context, const PointerEvent& event) {
  auto inverse = invert(m_box.m_transform);
  if(!inverse) {
    return;
  }
  auto local =
    inverse->map(context.to_document(event.m_position)) + m_grab_offset;
  auto& transform = m_box.m_transform;
  if(transform.m_a == 1 && transform.m_b == 0 && transform.m_c == 0 &&
      transform.m_d == 1) {
    local = Point(std::round(local.m_x + transform.m_tx) - transform.m_tx,
      std::round(local.m_y + transform.m_ty) - transform.m_ty);
  }
  if(transform.m_b == 0 && transform.m_c == 0) {
    local = inverse->map(context.get_snap_service().snap_point(
      transform.map(local), get_snap_axes(m_handle), event.m_modifiers));
  }
  auto rect = resize(m_box.m_rect, m_handle, local,
    is_shift(event.m_modifiers), is_alt(event.m_modifiers), MINIMUM_SIZE);
  if(rect == m_resized) {
    return;
  }
  auto step = transform * make_rect_mapping(m_resized, rect) * *inverse;
  context.get_editor().merge(
    std::make_unique<TransformCommand>(m_nodes, step, "Resize"));
  m_resized = rect;
  m_is_applied = true;
}

void SelectTool::update_rotate(
    ToolContext& context, const PointerEvent& event) {
  auto center = m_box.get_center();
  auto start = m_press_document - center;
  auto current = context.to_document(event.m_position) - center;
  if(start == Point(0, 0) || current == Point(0, 0)) {
    return;
  }
  auto raw = std::atan2(current.m_y, current.m_x) -
    std::atan2(start.m_y, start.m_x);
  m_raw_angle += normalize(raw - m_raw_angle);
  auto angle = m_raw_angle;
  if(is_shift(event.m_modifiers)) {
    auto base = std::atan2(m_box.m_transform.m_b, m_box.m_transform.m_a);
    angle = std::round((base + angle) / ROTATION_STEP) * ROTATION_STEP - base;
  }
  auto step = angle - m_angle;
  if(std::abs(step) < ANGLE_TOLERANCE) {
    return;
  }
  context.get_editor().merge(make_rotate_command(m_nodes, step, center));
  m_angle = angle;
  m_is_applied = true;
}

void SelectTool::update_radius(
    ToolContext& context, const PointerEvent& event) {
  auto radius = std::clamp(
    std::round(m_radius + get_inset(context, event.m_position) - m_inset),
    0.0, m_radius_handles->m_limit);
  auto nodes = std::vector{m_radius_handles->m_id};
  if(is_alt(event.m_modifiers)) {
    context.get_editor().merge(std::make_unique<SetCornerRadiusCommand>(
      std::move(nodes), static_cast<Corner>(m_corner), radius));
  } else {
    context.get_editor().merge(
      std::make_unique<SetCornerRadiusCommand>(std::move(nodes), radius));
  }
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
  auto nodes = m_base;
  auto members = std::unordered_set<NodeId>(nodes.begin(), nodes.end());
  for(auto& id : hits) {
    if(members.insert(id).second) {
      nodes.push_back(id);
    }
  }
  auto& selection = context.get_editor().get_selection();
  if(selection.get_nodes() != nodes) {
    selection.select(nodes);
  }
}

void SelectTool::update_hover(
    ToolContext& context, const PointerEvent& event) {
  auto& edit = context.get_editor().get_context();
  m_cursor = ToolCursor();
  if(auto cursor = m_layout_handles.hover(context, event.m_position)) {
    m_cursor = *cursor;
  } else if(auto grab = find_grab(context, event.m_position)) {
    auto box = *get_box(context);
    auto center = context.to_view(box.get_center());
    auto point = get_view_point(context, box, grab->m_handle);
    if(grab->m_target == Target::HANDLE) {
      m_cursor = ToolCursor(CursorKind::RESIZE, get_angle(center, point));
    } else {
      m_cursor = ToolCursor(CursorKind::ROTATE, get_angle(center, point));
    }
  }
  auto hover = context.get_titles().find(event.m_position);
  if(hover.is_nil()) {
    auto hit = context.get_hit_tester().hit(context.to_document(
      event.m_position), HIT_TOLERANCE / context.get_zoom());
    if(!hit.is_nil() && !is_artboard(context.get_document(), hit)) {
      hover = resolve_target(context, hit, event.m_modifiers, false);
    }
  }
  edit.set_hover(hover);
  measure(context, get_box(context), hover, event.m_modifiers);
}

void SelectTool::finish(ToolContext& context) {
  if(m_gesture == Gesture::LAYOUT) {
    m_layout_handles.release(context);
  } else if(m_gesture == Gesture::MOVE || m_gesture == Gesture::RESIZE ||
      m_gesture == Gesture::ROTATE) {
    context.get_editor().get_history().release_layout();
    context.get_editor().get_history().seal();
    context.get_snap_service().end();
  } else if(m_gesture == Gesture::RADIUS) {
    context.get_editor().get_history().seal();
  }
  m_gesture = Gesture::NONE;
  m_radius_handles = std::nullopt;
  m_is_marquee_pending = false;
  m_marquee = Rect(0, 0, 0, 0);
  m_nodes.clear();
  m_base.clear();
}

std::optional<SelectionBox> SelectTool::get_gesture_box() const {
  if(m_gesture == Gesture::MOVE) {
    return SelectionBox(m_box.m_rect,
      make_translation(m_delta.m_x, m_delta.m_y) * m_box.m_transform);
  } else if(m_gesture == Gesture::RESIZE) {
    return SelectionBox(m_resized, m_box.m_transform);
  } else if(m_gesture == Gesture::ROTATE) {
    return SelectionBox(m_box.m_rect,
      make_rotation(m_angle, m_box.get_center()) * m_box.m_transform);
  }
  return std::nullopt;
}

void SelectTool::draw_box(const ToolContext& context, const SelectionBox& box,
    CanvasOverlay& overlay, bool has_handles) const {
  auto corners = to_view(context.get_view(), box.get_corners());
  add_outline(overlay, corners, SELECTION_COLOR);
  if(!has_handles) {
    return;
  }
  for(auto handle : HANDLES) {
    if(!is_handle_shown(handle, corners)) {
      continue;
    }
    auto shape = OverlayShape();
    shape.m_center = get_view_point(context, box, handle);
    shape.m_size = Size(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS);
    shape.m_radius = HANDLE_RADIUS;
    shape.m_fill = HANDLE_FILL;
    shape.m_stroke = SELECTION_COLOR;
    shape.m_stroke_width = LINE_WIDTH;
    overlay.m_shapes.push_back(shape);
  }
}

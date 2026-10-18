#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>
#include <variant>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Tools/SelectTool.hpp"

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
    m_nudge_key(0) {}

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
  if(!is_control(modifiers)) {
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
      context.get_snap_service().begin(m_nodes);
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
  auto node = document.find(target);
  if(!node || !is_enterable(*node) || target == hit) {
    return;
  }
  if(!edit.enter(target)) {
    return;
  }
  auto inner = edit.resolve(hit);
  if(!inner.is_nil()) {
    editor.get_selection().select(inner);
  }
}

void SelectTool::leave(ToolContext& context) {
  if(m_gesture == Gesture::NONE) {
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
  if((m_gesture == Gesture::MOVE || m_gesture == Gesture::RESIZE ||
      m_gesture == Gesture::ROTATE) && m_is_applied) {
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
      !get_movable(context).empty());
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
  if(!box || get_movable(context).empty()) {
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
  context.get_snap_service().begin(m_nodes);
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
  delta = context.get_snap_service().snap_move(bounds, delta);
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
  auto point = context.get_snap_service().snap_resize(
    context.to_document(event.m_position));
  auto local = inverse->map(point) + m_grab_offset;
  auto& transform = m_box.m_transform;
  if(transform.m_a == 1 && transform.m_b == 0 && transform.m_c == 0 &&
      transform.m_d == 1) {
    local = Point(std::round(local.m_x + transform.m_tx) - transform.m_tx,
      std::round(local.m_y + transform.m_ty) - transform.m_ty);
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
  if(auto grab = find_grab(context, event.m_position)) {
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
}

void SelectTool::finish(ToolContext& context) {
  if(m_gesture == Gesture::MOVE || m_gesture == Gesture::RESIZE ||
      m_gesture == Gesture::ROTATE) {
    context.get_editor().get_history().seal();
    context.get_snap_service().end();
  }
  m_gesture = Gesture::NONE;
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

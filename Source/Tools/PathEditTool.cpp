#include <algorithm>
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Tools/PathEditTool.hpp"

using namespace Rellu;

namespace {
  constexpr auto SELECTION_COLOR = make_color(38, 128, 235);
  constexpr auto MARQUEE_FILL = make_color(38, 128, 235, 0.08);
  constexpr auto HIT_TOLERANCE = 3.0;

  bool is_shift(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ShiftModifier);
  }

  bool is_alt(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::AltModifier);
  }

  bool is_arrow(int key) {
    return key == Qt::Key_Left || key == Qt::Key_Right ||
      key == Qt::Key_Up || key == Qt::Key_Down;
  }

  bool is_valid(const EditablePath& path, const AnchorIndex& index) {
    return index.m_contour < path.m_contours.size() && index.m_anchor <
      path.m_contours[index.m_contour].m_anchors.size();
  }

  Anchor& get_local(EditablePath& path, const AnchorIndex& index) {
    return path.m_contours[index.m_contour].m_anchors[index.m_anchor];
  }

  Point& get_handle(Anchor& anchor, HandleSide side) {
    if(side == HandleSide::IN) {
      return anchor.m_in;
    }
    return anchor.m_out;
  }

  Point get_direction(Point vector) {
    auto magnitude = length(vector);
    if(magnitude == 0) {
      return Point(0, 0);
    }
    return vector / magnitude;
  }

  Rect make_rect(Point first, Point second) {
    auto left = std::min(first.m_x, second.m_x);
    auto top = std::min(first.m_y, second.m_y);
    return Rect(left, top, std::max(first.m_x, second.m_x) - left,
      std::max(first.m_y, second.m_y) - top);
  }

  NodeId get_parent(const Document& document, const NodeId& id) {
    if(auto node = document.find(id)) {
      return node->m_parent;
    }
    return NodeId();
  }

  void normalize(std::vector<AnchorIndex>& selection) {
    std::sort(selection.begin(), selection.end());
    selection.erase(std::unique(selection.begin(), selection.end()),
      selection.end());
  }
}

PathEditTool::PathEditTool() noexcept
  : m_gesture(Gesture::NONE),
    m_press(0, 0),
    m_press_document(0, 0),
    m_is_smooth(false),
    m_is_applied(false),
    m_is_marquee_pending(false),
    m_marquee(0, 0, 0, 0),
    m_nudge_key(0) {}

NodeId PathEditTool::get_node(const ToolContext& context) const {
  if(auto path = load(context)) {
    return path->m_node;
  }
  return NodeId();
}

const std::vector<AnchorIndex>& PathEditTool::get_selection() const {
  return m_selection;
}

bool PathEditTool::is_edit_key(
    const ToolContext& context, const KeyEvent& event) const {
  return (event.m_key == Qt::Key_Delete ||
    event.m_key == Qt::Key_Backspace) && !m_selection.empty() &&
    load(context);
}

void PathEditTool::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  if(m_gesture != Gesture::NONE) {
    finish(context);
  }
  auto path = load(context);
  if(!path) {
    return;
  }
  context.get_editor().get_history().seal();
  m_press = event.m_position;
  m_press_document = context.to_document(event.m_position);
  m_is_applied = false;
  m_is_marquee_pending = false;
  m_base = path;
  m_anchor = std::nullopt;
  context.get_snap_service().begin(
    get_parent(context.get_document(), path->m_node),
    std::vector{path->m_node});
  if(auto handle = find_handle(context, *path, m_selection, event.m_position,
      ANCHOR_REACH)) {
    m_handle = handle;
    m_is_smooth = is_smooth(get_local(*path, handle->m_anchor));
    m_gesture = Gesture::HANDLE;
    return;
  }
  if(auto index = find_anchor(context, *path, event.m_position,
      ANCHOR_REACH)) {
    auto is_selected = std::find(m_selection.begin(), m_selection.end(),
      *index) != m_selection.end();
    if(is_shift(event.m_modifiers)) {
      if(is_selected) {
        std::erase(m_selection, *index);
        context.get_snap_service().end();
        return;
      }
      m_selection.push_back(*index);
      normalize(m_selection);
    } else if(!is_selected) {
      m_selection = std::vector{*index};
    }
    m_anchor = *index;
    m_gesture = Gesture::PENDING;
    return;
  }
  if(auto position = find_segment(context, *path, event.m_position,
      SEGMENT_REACH); position && position->m_position.m_t > 0 &&
      position->m_position.m_t < 1) {
    auto edited = *path;
    auto inserted = split_segment(edited.m_contours[position->m_contour],
      position->m_position.m_segment, position->m_position.m_t);
    context.get_editor().merge(make_path_command("Add Anchor Point", edited));
    m_is_applied = true;
    m_base = edited;
    m_selection = std::vector{AnchorIndex(position->m_contour, inserted)};
    m_anchor = m_selection.front();
    m_gesture = Gesture::PENDING;
    return;
  }
  m_gesture = Gesture::PENDING;
  m_is_marquee_pending = true;
  if(is_shift(event.m_modifiers)) {
    m_base_selection = m_selection;
  } else {
    m_base_selection.clear();
  }
}

void PathEditTool::move(ToolContext& context, const PointerEvent& event) {
  if(m_gesture == Gesture::NONE) {
    return;
  }
  if(!event.m_buttons.testFlag(Qt::LeftButton)) {
    finish(context);
    return;
  }
  if(m_gesture == Gesture::PENDING) {
    if(distance(event.m_position, m_press) < DRAG_THRESHOLD) {
      return;
    }
    if(m_is_marquee_pending) {
      m_gesture = Gesture::MARQUEE;
    } else {
      m_gesture = Gesture::MOVE;
    }
  }
  if(m_gesture == Gesture::MOVE) {
    auto delta = context.to_document(event.m_position) - m_press_document;
    if(is_shift(event.m_modifiers)) {
      delta = constrain_to_45_degrees(delta);
    } else if(m_base && m_anchor && is_valid(*m_base, *m_anchor)) {
      auto origin =
        m_base->m_transform.map(get_local(*m_base, *m_anchor).m_point);
      delta = context.get_snap_service().snap_point(
        origin + delta, SnapAxes::BOTH, event.m_modifiers) - origin;
    }
    move_points(context, delta);
  } else if(m_gesture == Gesture::HANDLE) {
    update_handle(context, event);
  } else if(m_gesture == Gesture::MARQUEE) {
    update_marquee(context, event);
  }
}

void PathEditTool::release(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  if(m_gesture == Gesture::PENDING && m_is_marquee_pending) {
    finish(context);
    click_away(context);
    return;
  }
  finish(context);
}

void PathEditTool::double_click(
    ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  if(m_gesture != Gesture::NONE) {
    finish(context);
  }
  auto path = load(context);
  if(!path) {
    return;
  }
  if(auto index = find_anchor(context, *path, event.m_position,
      ANCHOR_REACH)) {
    context.get_editor().get_history().seal();
    auto& contour = path->m_contours[index->m_contour];
    set_smooth(contour, index->m_anchor,
      !is_smooth(contour.m_anchors[index->m_anchor]));
    context.get_editor().merge(
      make_path_command("Convert Point Type", *path));
    context.get_editor().get_history().seal();
    m_selection = std::vector{*index};
    return;
  }
  press(context, event);
}

bool PathEditTool::key_press(ToolContext& context, const KeyEvent& event) {
  if(event.m_key == Qt::Key_Escape || event.m_key == Qt::Key_Return ||
      event.m_key == Qt::Key_Enter) {
    if(m_gesture != Gesture::NONE) {
      cancel(context);
      return true;
    }
    exit(context);
    return true;
  }
  if(event.m_key == Qt::Key_Delete || event.m_key == Qt::Key_Backspace) {
    if(m_gesture != Gesture::NONE || m_selection.empty() || !load(context)) {
      return false;
    }
    remove_selection(context);
    return true;
  }
  if(!is_arrow(event.m_key) || m_selection.empty()) {
    return false;
  }
  if(m_gesture != Gesture::NONE) {
    return true;
  }
  auto path = load(context);
  if(!path) {
    return false;
  }
  auto delta = Point(0, 0);
  if(event.m_key == Qt::Key_Left) {
    delta = Point(-1, 0);
  } else if(event.m_key == Qt::Key_Right) {
    delta = Point(1, 0);
  } else if(event.m_key == Qt::Key_Up) {
    delta = Point(0, -1);
  } else {
    delta = Point(0, 1);
  }
  if(!event.m_is_repeat || m_nudge_key != event.m_key) {
    context.get_editor().get_history().seal();
  }
  m_nudge_key = event.m_key;
  m_base = path;
  move_points(
    context, delta * (is_shift(event.m_modifiers) ? LARGE_NUDGE : NUDGE));
  m_base = std::nullopt;
  m_is_applied = false;
  return true;
}

bool PathEditTool::key_release(ToolContext& context, const KeyEvent& event) {
  if(!is_arrow(event.m_key)) {
    return false;
  }
  if(!event.m_is_repeat && event.m_key == m_nudge_key) {
    context.get_editor().get_history().seal();
    m_nudge_key = 0;
  }
  return true;
}

void PathEditTool::cancel(ToolContext& context) {
  if(m_gesture == Gesture::MARQUEE) {
    m_selection = m_base_selection;
  } else if(m_gesture != Gesture::NONE && m_is_applied) {
    context.get_editor().undo();
  }
  m_is_applied = false;
  finish(context);
}

bool PathEditTool::is_busy() const {
  return m_gesture != Gesture::NONE;
}

void PathEditTool::draw(
    const ToolContext& context, CanvasOverlay& overlay) const {
  auto path = load(context);
  if(!path) {
    return;
  }
  auto selection = m_selection;
  std::erase_if(selection, [&] (const auto& index) {
    return !is_valid(*path, index);
  });
  draw_path(context, *path, selection, selection, overlay);
  if(m_gesture == Gesture::MARQUEE) {
    auto start = context.to_view(m_marquee.get_top_left());
    auto end = context.to_view(m_marquee.get_bottom_right());
    auto rect = make_rect(start, end);
    overlay.m_shapes.push_back(OverlayShape(
      Point(rect.m_x + rect.m_width / 2, rect.m_y + rect.m_height / 2),
      Size(rect.m_width, rect.m_height), 0, 0, MARQUEE_FILL, SELECTION_COLOR,
      1));
  }
}

std::optional<EditablePath> PathEditTool::load(ToolContext& context) {
  auto& nodes = context.get_editor().get_selection().get_nodes();
  auto path = std::optional<EditablePath>();
  if(nodes.size() == 1) {
    path = load_path(context.get_document(), nodes.front());
  }
  if(!path) {
    exit(context);
    return std::nullopt;
  }
  if(path->m_node != m_node) {
    m_node = path->m_node;
    m_selection.clear();
  }
  std::erase_if(m_selection, [&] (const auto& index) {
    return !is_valid(*path, index);
  });
  return path;
}

std::optional<EditablePath> PathEditTool::load(
    const ToolContext& context) const {
  auto& nodes = context.get_editor().get_selection().get_nodes();
  if(nodes.size() != 1) {
    return std::nullopt;
  }
  return load_path(context.get_document(), nodes.front());
}

void PathEditTool::exit(ToolContext& context) {
  m_gesture = Gesture::NONE;
  m_selection.clear();
  m_base = std::nullopt;
  m_handle = std::nullopt;
  m_node = NodeId();
  context.request_tool("select");
}

void PathEditTool::move_points(ToolContext& context, Point delta) {
  if(!m_base) {
    return;
  }
  auto inverse = invert(m_base->m_transform);
  if(!inverse) {
    return;
  }
  auto local = inverse->map(delta) - inverse->map(Point(0, 0));
  auto edited = *m_base;
  for(auto& index : m_selection) {
    if(!is_valid(edited, index)) {
      continue;
    }
    auto& anchor = get_local(edited, index);
    anchor.m_point = anchor.m_point + local;
    anchor.m_in = anchor.m_in + local;
    anchor.m_out = anchor.m_out + local;
  }
  context.get_editor().merge(make_path_command("Move Anchor Point", edited));
  m_is_applied = true;
}

void PathEditTool::update_handle(
    ToolContext& context, const PointerEvent& event) {
  if(!m_base || !m_handle || !is_valid(*m_base, m_handle->m_anchor)) {
    return;
  }
  auto inverse = invert(m_base->m_transform);
  if(!inverse) {
    return;
  }
  auto edited = *m_base;
  auto& anchor = get_local(edited, m_handle->m_anchor);
  auto center = edited.m_transform.map(anchor.m_point);
  auto handle = context.to_document(event.m_position);
  if(is_shift(event.m_modifiers)) {
    handle = center + constrain_to_45_degrees(handle - center);
  } else {
    handle = context.get_snap_service().snap_point(
      handle, SnapAxes::BOTH, event.m_modifiers);
  }
  auto local = inverse->map(handle);
  get_handle(anchor, m_handle->m_side) = local;
  if(m_is_smooth && !is_alt(event.m_modifiers)) {
    auto side = HandleSide::IN;
    if(m_handle->m_side == HandleSide::IN) {
      side = HandleSide::OUT;
    }
    auto& opposite = get_handle(anchor, side);
    auto direction = get_direction(anchor.m_point - local);
    if(direction != Point(0, 0)) {
      opposite =
        anchor.m_point + direction * distance(anchor.m_point, opposite);
    }
  }
  context.get_editor().merge(
    make_path_command("Move Control Point", edited));
  m_is_applied = true;
}

void PathEditTool::update_marquee(
    ToolContext& context, const PointerEvent& event) {
  m_marquee = make_rect(
    m_press_document, context.to_document(event.m_position));
  auto path = load(context);
  if(!path) {
    return;
  }
  m_selection = m_base_selection;
  for(auto i = std::size_t(0); i != path->m_contours.size(); ++i) {
    auto& anchors = path->m_contours[i].m_anchors;
    for(auto j = std::size_t(0); j != anchors.size(); ++j) {
      if(m_marquee.contains(path->m_transform.map(anchors[j].m_point))) {
        m_selection.push_back(AnchorIndex(i, j));
      }
    }
  }
  normalize(m_selection);
}

void PathEditTool::click_away(ToolContext& context) {
  auto& editor = context.get_editor();
  auto hit = context.get_hit_tester().hit(
    m_press_document, HIT_TOLERANCE / context.get_zoom());
  if(!hit.is_nil() && hit == m_node) {
    m_selection.clear();
    return;
  }
  auto target = NodeId();
  if(!hit.is_nil()) {
    target = editor.get_context().resolve(hit);
    if(auto node = context.get_document().find(target);
        node && node->get_artboard()) {
      target = NodeId();
    }
  }
  exit(context);
  if(target.is_nil()) {
    editor.get_selection().clear();
  } else {
    editor.get_selection().select(target);
  }
}

void PathEditTool::remove_selection(ToolContext& context) {
  auto path = load(context);
  if(!path) {
    return;
  }
  auto selection = m_selection;
  std::sort(selection.rbegin(), selection.rend());
  for(auto& index : selection) {
    remove_anchor(path->m_contours[index.m_contour], index.m_anchor);
  }
  std::erase_if(path->m_contours, [] (const auto& contour) {
    return contour.m_anchors.size() < 2;
  });
  m_selection.clear();
  auto& editor = context.get_editor();
  editor.get_history().seal();
  if(path->m_contours.empty()) {
    editor.execute(
      std::make_unique<DeleteCommand>(std::vector{path->m_node}));
    exit(context);
    return;
  }
  editor.merge(make_path_command("Delete", *path));
  editor.get_history().seal();
}

void PathEditTool::finish(ToolContext& context) {
  if(m_is_applied) {
    context.get_editor().get_history().seal();
  }
  if(m_gesture != Gesture::NONE) {
    context.get_snap_service().end();
  }
  m_anchor = std::nullopt;
  m_gesture = Gesture::NONE;
  m_is_applied = false;
  m_is_marquee_pending = false;
  m_base = std::nullopt;
  m_handle = std::nullopt;
}

#include <algorithm>
#include <cmath>
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Tools/DrawTool.hpp"
#include "Rellu/Tools/SelectionBox.hpp"

using namespace Rellu;

namespace {
  constexpr auto SELECTION_COLOR = make_color(20, 115, 230);
  constexpr auto LINE_WIDTH = 1.0;

  double get_sign(double value) {
    if(value < 0) {
      return -1;
    }
    return 1;
  }

  Point round_point(Point point) {
    return Point(std::round(point.m_x), std::round(point.m_y));
  }
}

DrawTool::DrawTool(std::string prefix)
  : m_is_pressed(false),
    m_is_applied(false),
    m_prefix(std::move(prefix)),
    m_press(0, 0),
    m_start(0, 0) {}

void DrawTool::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  if(m_is_pressed) {
    finish(context);
  }
  m_is_pressed = true;
  m_is_applied = false;
  m_press = event.m_position;
  m_start = context.to_document(event.m_position);
  m_header = std::nullopt;
  auto& snap = context.get_snap_service();
  snap.begin(find_parent(context, m_start), std::vector<NodeId>());
  m_start = snap.snap_point(m_start, SnapAxes::BOTH, event.m_modifiers);
  context.get_editor().get_history().seal();
}

void DrawTool::move(ToolContext& context, const PointerEvent& event) {
  if(!m_is_pressed) {
    return;
  }
  if(!event.m_buttons.testFlag(Qt::LeftButton)) {
    finish(context);
    return;
  }
  update(context, event);
}

void DrawTool::release(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton || !m_is_pressed) {
    return;
  }
  update(context, event);
  if(!m_is_applied) {
    click(context, m_start);
  }
  finish(context);
}

bool DrawTool::key_press(ToolContext& context, const KeyEvent& event) {
  if(event.m_key != Qt::Key_Escape) {
    return false;
  }
  if(m_is_pressed) {
    cancel(context);
  } else {
    context.request_tool("select");
  }
  return true;
}

void DrawTool::cancel(ToolContext& context) {
  if(m_is_applied) {
    context.get_editor().undo();
  }
  m_is_applied = false;
  finish(context);
}

bool DrawTool::is_busy() const {
  return m_is_pressed;
}

ToolCursor DrawTool::get_cursor() const {
  return ToolCursor(CursorKind::CROSSHAIR);
}

void DrawTool::draw(const ToolContext& context, CanvasOverlay& overlay) const {
  auto& document = context.get_document();
  auto nodes = context.get_editor().get_selection().get_nodes();
  std::erase_if(nodes, [&] (const auto& id) {
    auto node = document.find(id);
    return !node || node->get_artboard();
  });
  auto box = get_selection_box(document, nodes, context.get_text_measurer());
  if(!box) {
    return;
  }
  auto corners = box->get_corners();
  for(auto i = std::size_t(0); i != corners.size(); ++i) {
    auto start = context.to_view(corners[i]);
    auto end = context.to_view(corners[(i + 1) % corners.size()]);
    auto length = distance(start, end);
    if(length == 0) {
      continue;
    }
    auto extension = (end - start) * (LINE_WIDTH / 2 / length);
    overlay.m_shapes.push_back(make_overlay_line(
      start - extension, end + extension, LINE_WIDTH, SELECTION_COLOR));
  }
}

void DrawTool::click(ToolContext& context, Point point) {}

NodeId DrawTool::find_parent(const ToolContext& context, Point point) const {
  return find_drawing_parent(context.get_document(), point);
}

void DrawTool::update(ToolContext& context, const PointerEvent& event) {
  if(!m_is_applied && distance(event.m_position, m_press) < DRAG_THRESHOLD) {
    return;
  }
  if(!m_header) {
    m_header = NodeHeader();
    m_header->m_id = generate_node_id();
    m_header->m_name = make_node_name(context.get_document(), m_prefix);
  }
  auto end = context.get_snap_service().snap_point(
    context.to_document(event.m_position), SnapAxes::BOTH, event.m_modifiers);
  auto command =
    make_command(context, *m_header, m_start, end, event.m_modifiers);
  if(!command) {
    return;
  }
  if(context.get_editor().merge(std::move(command))) {
    m_is_applied = true;
  }
}

void DrawTool::finish(ToolContext& context) {
  if(m_is_applied) {
    context.get_editor().get_history().seal();
  }
  if(m_is_pressed) {
    context.get_snap_service().end();
  }
  m_is_pressed = false;
  m_is_applied = false;
  m_header = std::nullopt;
}

Rect Rellu::make_drawn_box(
    Point start, Point end, Qt::KeyboardModifiers modifiers) {
  auto origin = round_point(start);
  auto delta = round_point(end) - origin;
  if(modifiers.testFlag(Qt::ShiftModifier)) {
    auto side = std::max(std::abs(delta.m_x), std::abs(delta.m_y));
    delta = Point(get_sign(delta.m_x) * side, get_sign(delta.m_y) * side);
  }
  if(modifiers.testFlag(Qt::AltModifier)) {
    auto half = Point(std::abs(delta.m_x), std::abs(delta.m_y));
    return Rect(origin.m_x - half.m_x, origin.m_y - half.m_y, 2 * half.m_x,
      2 * half.m_y);
  }
  auto corner = origin + delta;
  return Rect(std::min(origin.m_x, corner.m_x),
    std::min(origin.m_y, corner.m_y), std::abs(delta.m_x),
    std::abs(delta.m_y));
}

NodeId Rellu::find_drawing_parent(const Document& document, Point point) {
  auto& canvas = document.get_canvas();
  for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
    auto node = document.find(*i);
    if(!node || !node->m_header.m_is_visible) {
      continue;
    }
    if(auto artboard = node->get_artboard();
        artboard && artboard->m_frame.contains(point)) {
      return *i;
    }
  }
  return NodeId();
}

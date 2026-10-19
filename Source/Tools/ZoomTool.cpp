#include <algorithm>
#include <cmath>
#include "Rellu/Tools/ZoomTool.hpp"

using namespace Rellu;

namespace {
  constexpr auto MARQUEE_COLOR = make_color(20, 115, 230);
  constexpr auto MARQUEE_FILL = make_color(20, 115, 230, 0.08);
  constexpr auto LINE_WIDTH = 1.0;

  Rect make_rect(Point first, Point second) {
    auto left = std::min(first.m_x, second.m_x);
    auto top = std::min(first.m_y, second.m_y);
    return Rect(left, top, std::max(first.m_x, second.m_x) - left,
      std::max(first.m_y, second.m_y) - top);
  }
}

ZoomTool::ZoomTool()
  : m_is_pressed(false),
    m_is_dragging(false),
    m_press(0, 0),
    m_position(0, 0) {}

void ZoomTool::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  m_is_pressed = true;
  m_is_dragging = false;
  m_press = event.m_position;
  m_position = event.m_position;
}

void ZoomTool::move(ToolContext& context, const PointerEvent& event) {
  if(!m_is_pressed) {
    return;
  }
  if(!event.m_buttons.testFlag(Qt::LeftButton)) {
    cancel(context);
    return;
  }
  m_position = event.m_position;
  if(distance(m_position, m_press) >= DRAG_THRESHOLD) {
    m_is_dragging = true;
  }
}

void ZoomTool::release(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton || !m_is_pressed) {
    return;
  }
  m_position = event.m_position;
  if(distance(m_position, m_press) >= DRAG_THRESHOLD) {
    m_is_dragging = true;
  }
  auto is_dragging = m_is_dragging;
  m_is_pressed = false;
  m_is_dragging = false;
  if(is_dragging) {
    context.zoom_to(make_rect(
      context.to_document(m_press), context.to_document(m_position)));
  } else if(event.m_modifiers.testFlag(Qt::AltModifier)) {
    context.zoom(1 / ZOOM_STEP, m_position);
  } else {
    context.zoom(ZOOM_STEP, m_position);
  }
}

bool ZoomTool::key_press(ToolContext& context, const KeyEvent& event) {
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

void ZoomTool::cancel(ToolContext& context) {
  m_is_pressed = false;
  m_is_dragging = false;
}

bool ZoomTool::is_busy() const {
  return m_is_pressed;
}

void ZoomTool::draw(const ToolContext& context, CanvasOverlay& overlay) const {
  if(!m_is_dragging) {
    return;
  }
  auto rect = make_rect(m_press, m_position);
  auto shape = OverlayShape();
  shape.m_center = rect.get_center();
  shape.m_size = rect.get_size();
  shape.m_fill = MARQUEE_FILL;
  shape.m_stroke = MARQUEE_COLOR;
  shape.m_stroke_width = LINE_WIDTH;
  overlay.m_shapes.push_back(shape);
}

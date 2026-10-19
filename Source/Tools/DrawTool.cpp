#include <algorithm>
#include <cmath>
#include <optional>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Tools/DrawTool.hpp"
#include "Rellu/Tools/SelectionBox.hpp"

using namespace Rellu;

namespace {
  constexpr auto SELECTION_COLOR = make_color(38, 128, 235);
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

  struct StackPlace {
    bool m_is_vertical;
    NodeId m_next;
    double m_start;
    double m_end;
  };

  std::optional<StackPlace> find_stack_place(const Document& document,
      const NodeId& stack, const NodeId& id, Point point) {
    auto node = document.find(stack);
    auto group = node ? node->get_group() : nullptr;
    if(!group || !group->m_stack) {
      return std::nullopt;
    }
    auto inverse = invert(get_world_transform(document, stack));
    if(!inverse) {
      return std::nullopt;
    }
    auto place = StackPlace(
      group->m_stack->m_orientation == Orientation::VERTICAL, NodeId(), 0, 0);
    auto get_along = [&] (Point value) {
      return place.m_is_vertical ? value.m_y : value.m_x;
    };
    auto start = get_along(inverse->map(point));
    auto is_found = false;
    for(auto& item : get_stack_items(document, stack)) {
      if(item == id || !document.get(item).m_header.m_is_visible) {
        continue;
      }
      auto rect = get_bounds(document, item, get_transform(document, item));
      if(!rect) {
        continue;
      }
      is_found = true;
      place.m_end = get_along(rect->get_bottom_right());
      if(place.m_end > start) {
        place.m_next = item;
        place.m_start = get_along(rect->get_top_left());
        break;
      }
    }
    if(!is_found) {
      return std::nullopt;
    }
    return place;
  }
}

DrawTool::DrawTool(std::string prefix)
  : m_is_pressed(false),
    m_is_applied(false),
    m_is_held(false),
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
  } else if(m_header) {
    complete(context, m_header->m_id);
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

void DrawTool::complete(ToolContext& context, const NodeId& id) {}

NodeId DrawTool::find_parent(const ToolContext& context, Point point) const {
  return find_drawing_parent(context, point);
}

void DrawTool::update(ToolContext& context, const PointerEvent& event) {
  if(!m_is_applied && distance(event.m_position, m_press) < DRAG_THRESHOLD) {
    return;
  }
  if(!m_header) {
    m_header = NodeHeader();
    m_header->m_id = generate_node_id();
    m_header->m_name = make_node_name(context.get_document(), m_prefix);
    m_is_held = hold_drawn_layout(
      context, find_parent(context, m_start), m_header->m_id);
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
  if(m_is_held) {
    auto& editor = context.get_editor();
    auto& document = context.get_document();
    if(auto node = m_is_applied ? document.find(m_header->m_id) : nullptr) {
      auto id = node->get_id();
      auto parent = node->m_parent;
      if(auto bounds = get_bounds(document, id, get_transform(document, id))) {
        auto offset = get_stack_offset(document, parent, id, *bounds, m_start);
        if(offset != Point(0, 0)) {
          auto header = node->m_header;
          header.m_transform =
            make_translation(offset.m_x, offset.m_y) * header.m_transform;
          editor.merge(std::make_unique<InsertNodeCommand>(
            std::string(), parent, std::move(header), node->m_payload));
        }
      }
    }
    editor.get_history().release_layout();
    m_is_held = false;
  }
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

bool Rellu::hold_drawn_layout(
    ToolContext& context, const NodeId& parent, const NodeId& id) {
  auto node = context.get_document().find(parent);
  auto group = node ? node->get_group() : nullptr;
  if(!group || !group->m_stack) {
    return false;
  }
  context.get_editor().get_history().hold_layout(std::vector{id});
  return true;
}

Point Rellu::get_stack_offset(const Document& document, const NodeId& stack,
    const NodeId& id, const Rect& bounds, Point point) {
  auto place = find_stack_place(document, stack, id, point);
  if(!place) {
    return Point(0, 0);
  }
  auto key = place->m_end + 1;
  if(!place->m_next.is_nil()) {
    key = place->m_start;
  }
  if(place->m_is_vertical) {
    return Point(0, key - bounds.get_center().m_y);
  }
  return Point(key - bounds.get_center().m_x, 0);
}

std::optional<std::size_t> Rellu::get_stack_index(const Document& document,
    const NodeId& stack, const NodeId& id, Point point) {
  auto place = find_stack_place(document, stack, id, point);
  if(!place) {
    return std::nullopt;
  }
  auto children = document.get(stack).m_children;
  std::erase(children, id);
  if(place->m_next.is_nil()) {
    auto& role = *document.get(stack).get_group()->m_stack;
    if(role.m_has_background && !children.empty()) {
      return 1;
    }
    return 0;
  }
  return static_cast<std::size_t>(std::find(children.begin(), children.end(),
    place->m_next) - children.begin()) + 1;
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

NodeId Rellu::find_drawing_parent(const ToolContext& context, Point point) {
  auto& document = context.get_document();
  auto container = context.get_editor().get_context().get_container();
  auto current = container;
  while(auto node = document.find(current)) {
    if(auto group = node->get_group()) {
      if(group->m_repeat_grid && current == container) {
        return find_drawing_parent(document, point);
      }
    } else if(!node->get_artboard()) {
      return find_drawing_parent(document, point);
    }
    current = node->m_parent;
  }
  if(!container.is_nil() && document.find(container)) {
    return container;
  }
  return find_drawing_parent(document, point);
}

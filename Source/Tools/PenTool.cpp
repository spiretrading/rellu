#include <algorithm>
#include <utility>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Tools/DrawTool.hpp"
#include "Rellu/Tools/PenTool.hpp"

using namespace Rellu;

namespace {
  bool is_shift(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ShiftModifier);
  }

  bool is_alt(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::AltModifier);
  }

  bool is_path(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->get_shape() &&
      std::holds_alternative<PathShape>(node->get_shape()->m_geometry);
  }

  void reverse(Contour& contour) {
    std::reverse(contour.m_anchors.begin(), contour.m_anchors.end());
    for(auto& anchor : contour.m_anchors) {
      std::swap(anchor.m_in, anchor.m_out);
    }
  }

  Point to_local(const EditablePath& path, Point point) {
    if(auto inverse = invert(path.m_transform)) {
      return inverse->map(point);
    }
    return point;
  }

  NodeId get_parent(const Document& document, const NodeId& id) {
    if(auto node = document.find(id)) {
      return node->m_parent;
    }
    return NodeId();
  }
}

PenTool::PenTool() noexcept
  : m_is_drawing(false),
    m_contour(0),
    m_is_reversed(false),
    m_is_dragging(false),
    m_is_dragged(false),
    m_is_closing(false),
    m_anchor(0),
    m_press(0, 0) {}

NodeId PenTool::get_path(const ToolContext& context) const {
  if(load_drawing(context)) {
    return m_node;
  }
  return NodeId();
}

void PenTool::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  auto& editor = context.get_editor();
  editor.get_history().seal();
  m_is_dragging = false;
  m_is_dragged = false;
  m_is_closing = false;
  m_press = event.m_position;
  m_modifiers = event.m_modifiers;
  auto point = context.to_document(event.m_position);
  if(auto path = load_drawing(context)) {
    auto& contour = path->m_contours[m_contour];
    auto& anchors = contour.m_anchors;
    auto view = context.get_view() * path->m_transform;
    if(anchors.size() > 1 && distance(view.map(anchors.front().m_point),
        event.m_position) <= ANCHOR_REACH) {
      contour.m_is_closed = true;
      apply(context, *path, "Close Path");
      m_anchor = 0;
      m_is_closing = true;
      m_is_dragging = true;
      return;
    }
    m_anchor = anchors.size() - 1;
    m_is_dragging = true;
    context.get_snap_service().begin(get_parent(context.get_document(),
      path->m_node), std::vector{path->m_node});
    if(distance(view.map(anchors.back().m_point), event.m_position) <=
        ANCHOR_REACH) {
      return;
    }
    if(is_shift(event.m_modifiers)) {
      auto previous = path->m_transform.map(anchors.back().m_point);
      point = previous + constrain_to_45_degrees(point - previous);
    } else {
      point = context.get_snap_service().snap_point(
        point, SnapAxes::BOTH, event.m_modifiers);
    }
    anchors.push_back(make_anchor(to_local(*path, point)));
    m_anchor = anchors.size() - 1;
    apply(context, *path, "Add Anchor Point");
    return;
  }
  finish();
  if(auto path = load_selected(context)) {
    if(auto index = find_anchor(context, *path, event.m_position,
        ANCHOR_REACH)) {
      auto& contour = path->m_contours[index->m_contour];
      if(!contour.m_is_closed && (index->m_anchor == 0 ||
          index->m_anchor + 1 == contour.m_anchors.size())) {
        m_is_drawing = true;
        m_node = path->m_node;
        m_contour = index->m_contour;
        m_is_reversed =
          index->m_anchor == 0 && contour.m_anchors.size() > 1;
        editor.get_history().record("Extend Path", nullptr, nullptr);
      }
      return;
    }
    if(auto position = find_segment(context, *path, event.m_position,
        SEGMENT_REACH); position && position->m_position.m_t > 0 &&
        position->m_position.m_t < 1) {
      split_segment(path->m_contours[position->m_contour],
        position->m_position.m_segment, position->m_position.m_t);
      apply(context, *path, "Add Anchor Point");
      return;
    }
  }
  start(context, point);
}

void PenTool::move(ToolContext& context, const PointerEvent& event) {
  m_hover = event.m_position;
  m_modifiers = event.m_modifiers;
  if(!m_is_dragging) {
    return;
  }
  if(!event.m_buttons.testFlag(Qt::LeftButton)) {
    m_is_dragging = false;
    context.get_snap_service().end();
    if(m_is_closing) {
      finish();
    }
    return;
  }
  if(!m_is_dragged && distance(event.m_position, m_press) < DRAG_THRESHOLD) {
    return;
  }
  m_is_dragged = true;
  drag(context, event);
}

void PenTool::release(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton || !m_is_dragging) {
    return;
  }
  if(m_is_dragged) {
    drag(context, event);
  }
  context.get_snap_service().end();
  m_is_dragging = false;
  m_is_dragged = false;
  if(m_is_closing) {
    finish();
  }
}

void PenTool::double_click(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  auto path = load_drawing(context);
  if(!path) {
    path = load_selected(context);
  }
  if(path) {
    if(auto index = find_anchor(context, *path, event.m_position,
        ANCHOR_REACH)) {
      context.get_editor().get_history().seal();
      auto& contour = path->m_contours[index->m_contour];
      set_smooth(contour, index->m_anchor,
        !is_smooth(contour.m_anchors[index->m_anchor]));
      apply(context, *path, "Convert Point Type");
      return;
    }
  }
  press(context, event);
}

void PenTool::leave(ToolContext& context) {
  m_hover = std::nullopt;
}

bool PenTool::key_press(ToolContext& context, const KeyEvent& event) {
  if(event.m_key == Qt::Key_Escape) {
    if(load_drawing(context) || m_is_dragging) {
      finish();
      context.get_editor().get_history().seal();
    } else {
      finish();
      context.request_tool("select");
    }
    return true;
  }
  if(event.m_key == Qt::Key_Return || event.m_key == Qt::Key_Enter) {
    if(!load_drawing(context)) {
      return false;
    }
    finish();
    context.get_editor().get_history().seal();
    return true;
  }
  return false;
}

void PenTool::cancel(ToolContext& context) {
  if(m_is_dragging) {
    context.get_snap_service().end();
  }
  finish();
  context.get_editor().get_history().seal();
}

bool PenTool::is_busy() const {
  return m_is_dragging;
}

ToolCursor PenTool::get_cursor() const {
  return ToolCursor(CursorKind::CROSSHAIR);
}

void PenTool::draw(const ToolContext& context, CanvasOverlay& overlay) const {
  if(auto path = load_drawing(context)) {
    auto& contour = path->m_contours[m_contour];
    auto active = AnchorIndex(m_contour, contour.m_anchors.size() - 1);
    if(m_is_dragging) {
      active = AnchorIndex(m_contour, m_anchor);
    }
    draw_path(context, *path, std::vector{active}, std::vector{active},
      overlay);
    if(m_is_dragging || !m_hover || contour.m_is_closed) {
      return;
    }
    auto view = transform(contour, context.get_view() * path->m_transform);
    auto& last = view.m_anchors.back();
    auto target = *m_hover;
    if(is_shift(m_modifiers)) {
      target = last.m_point + constrain_to_45_degrees(target - last.m_point);
    }
    auto end = make_anchor(target);
    if(view.m_anchors.size() > 1 && distance(view.m_anchors.front().m_point,
        *m_hover) <= ANCHOR_REACH) {
      end = view.m_anchors.front();
    }
    draw_path_curve(last, end, overlay);
  } else if(auto path = load_selected(context)) {
    draw_path(context, *path, std::vector<AnchorIndex>(),
      std::vector<AnchorIndex>(), overlay);
  }
}

std::optional<EditablePath> PenTool::load_drawing(
    const ToolContext& context) const {
  if(!m_is_drawing) {
    return std::nullopt;
  }
  auto& document = context.get_document();
  if(!is_path(document, m_node)) {
    return std::nullopt;
  }
  auto path = load_path(document, m_node);
  if(!path || m_contour >= path->m_contours.size()) {
    return std::nullopt;
  }
  auto& contour = path->m_contours[m_contour];
  if(contour.m_anchors.empty() || (contour.m_is_closed && !m_is_closing)) {
    return std::nullopt;
  }
  if(m_is_reversed) {
    reverse(contour);
  }
  if(!contour.m_is_closed && m_tail &&
      m_tail->m_point == contour.m_anchors.back().m_point) {
    contour.m_anchors.back() = *m_tail;
  }
  return path;
}

std::optional<EditablePath> PenTool::load_selected(
    const ToolContext& context) const {
  auto& document = context.get_document();
  auto& nodes = context.get_editor().get_selection().get_nodes();
  if(nodes.size() != 1 || !is_path(document, nodes.front())) {
    return std::nullopt;
  }
  return load_path(document, nodes.front());
}

void PenTool::apply(ToolContext& context, const EditablePath& path,
    const std::string& label) {
  if(m_is_drawing && path.m_node == m_node) {
    m_is_reversed = false;
    auto& contour = path.m_contours[m_contour];
    if(contour.m_is_closed) {
      m_tail = std::nullopt;
    } else {
      m_tail = contour.m_anchors.back();
    }
  }
  context.get_editor().merge(make_path_command(label, path));
}

void PenTool::start(ToolContext& context, Point point) {
  auto& document = context.get_document();
  auto parent = find_drawing_parent(context, point);
  auto& snap = context.get_snap_service();
  snap.begin(parent, std::vector<NodeId>());
  point = snap.snap_point(point, SnapAxes::BOTH, m_modifiers);
  auto inverse = invert(get_world_transform(document, parent));
  if(!inverse) {
    return;
  }
  auto outline = Path();
  outline.move_to(inverse->map(point));
  auto header = NodeHeader();
  header.m_id = generate_node_id();
  header.m_name = make_node_name(
    document, std::string(localize("name", "Path")) + ' ');
  auto id = header.m_id;
  auto shape = Shape(PathShape(Shared(std::move(outline)), FillRule::NON_ZERO),
    make_default_path_style());
  auto commands = std::vector<std::unique_ptr<Command>>();
  commands.push_back(std::make_unique<InsertNodeCommand>(
    "Draw Path", parent, std::move(header), std::move(shape)));
  if(auto index = get_stack_index(document, parent, id, point)) {
    commands.push_back(
      std::make_unique<ReparentCommand>(std::vector{id}, parent, *index));
  }
  if(!context.get_editor().execute(
      std::make_unique<CompositeCommand>("Draw Path", std::move(commands)))) {
    return;
  }
  m_is_drawing = true;
  m_node = id;
  m_contour = 0;
  m_is_reversed = false;
  m_tail = std::nullopt;
  m_anchor = 0;
  m_is_dragging = true;
}

void PenTool::drag(ToolContext& context, const PointerEvent& event) {
  auto path = load_drawing(context);
  if(!path) {
    m_is_dragging = false;
    return;
  }
  auto& contour = path->m_contours[m_contour];
  if(m_anchor >= contour.m_anchors.size()) {
    m_is_dragging = false;
    return;
  }
  auto& anchor = contour.m_anchors[m_anchor];
  auto center = path->m_transform.map(anchor.m_point);
  auto handle = context.to_document(event.m_position);
  if(is_shift(event.m_modifiers)) {
    handle = center + constrain_to_45_degrees(handle - center);
  } else {
    handle = context.get_snap_service().snap_point(
      handle, SnapAxes::BOTH, event.m_modifiers);
  }
  auto local = to_local(*path, handle);
  auto mirror = anchor.m_point * 2 - local;
  if(!is_alt(event.m_modifiers)) {
    anchor.m_out = local;
    anchor.m_in = mirror;
  } else if(m_is_closing) {
    anchor.m_in = mirror;
  } else {
    anchor.m_out = local;
  }
  if(m_is_closing) {
    apply(context, *path, "Close Path");
  } else if(m_anchor == 0) {
    apply(context, *path, "Draw Path");
  } else {
    apply(context, *path, "Add Anchor Point");
  }
}

void PenTool::finish() {
  m_is_drawing = false;
  m_is_reversed = false;
  m_tail = std::nullopt;
  m_is_dragging = false;
  m_is_dragged = false;
  m_is_closing = false;
}

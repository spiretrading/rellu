#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Tools/PrototypeTool.hpp"
#include "Rellu/Tools/SelectionBox.hpp"
#include "Rellu/Tools/ToolContext.hpp"

using namespace Rellu;

namespace {
  constexpr auto WIRE_COLOR = make_color(38, 128, 235);
  constexpr auto TRIGGER_WIRE_COLOR = make_color(126, 188, 230);
  constexpr auto OTHER_WIRE_COLOR = make_color(102, 102, 102);
  constexpr auto SELECTED_WIRE_WIDTH = 4.0;
  constexpr auto HANDLE_COLOR = make_color(38, 128, 235);
  constexpr auto WHITE = make_color(255, 255, 255);
  constexpr auto WIRE_WIDTH = 2.0;
  constexpr auto OUTLINE_WIDTH = 1.0;
  constexpr auto TARGET_WIDTH = 2.0;
  constexpr auto ARROW_LENGTH = 8.0;
  constexpr auto ARROW_SPREAD = 5.0;
  constexpr auto WIRE_SEGMENTS = 24;
  constexpr auto WIRE_ENTRY = 50.0;
  constexpr auto LABEL_PADDING = 6.0;
  constexpr auto LABEL_FONT_SIZE = 11.0;
  constexpr auto OFFSET_FILL = make_color(255, 74, 225, 0.4);
  constexpr auto OFFSET_LABEL_SIZE = 10.0;
  constexpr auto OFFSET_LABEL_GAP = 6.0;
  constexpr auto OFFSET_LABEL_ASCENT = 6.0;
  const auto MARKER_GREY = QColor(198, 198, 198);
  const auto MARKER_BLUE = QColor(38, 128, 235);
  const auto OFFSET_LABEL_COLOR = QColor(255, 73, 225);

  bool is_artboard(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->get_artboard();
  }

  bool is_shift(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ShiftModifier);
  }

  NodeId find_artboard(const Document& document, const NodeId& id) {
    auto current = id;
    for(auto depth = 0; depth != 512 && !current.is_nil(); ++depth) {
      auto node = document.find(current);
      if(!node) {
        return NodeId();
      }
      if(node->get_artboard()) {
        return current;
      }
      current = node->m_parent;
    }
    return NodeId();
  }

  void add_line(CanvasOverlay& overlay, Point start, Point end, double width,
      Color color) {
    auto length = std::hypot(end.m_x - start.m_x, end.m_y - start.m_y);
    if(length == 0) {
      return;
    }
    auto extension = (end - start) * (width / 2 / length);
    overlay.m_shapes.push_back(
      make_overlay_line(start - extension, end + extension, width, color));
  }

  void add_outline(CanvasOverlay& overlay, const std::array<Point, 4>& corners,
      double width, Color color) {
    for(auto i = std::size_t(0); i != corners.size(); ++i) {
      add_line(overlay, corners[i], corners[(i + 1) % corners.size()], width,
        color);
    }
  }

  std::array<Point, 4> to_view(
      const ToolContext& context, const std::array<Point, 4>& corners) {
    auto points = corners;
    for(auto& point : points) {
      point = context.to_view(point);
    }
    return points;
  }

  Point get_entry(const ToolContext& context, const Rect& frame) {
    auto view = context.get_view().map(frame);
    return Point(view.m_x,
      view.m_y + std::min(view.m_height / 2, WIRE_ENTRY));
  }

  void add_wire(CanvasOverlay& overlay, Point start, Point end,
      bool has_arrow, double width = WIRE_WIDTH, Color color = WIRE_COLOR) {
    auto reach = std::max(40.0, std::abs(end.m_x - start.m_x) / 2);
    auto first = Point(start.m_x + reach, start.m_y);
    auto second = Point(end.m_x - reach, end.m_y);
    if(!has_arrow) {
      second = end;
    }
    auto previous = start;
    for(auto i = 1; i <= WIRE_SEGMENTS; ++i) {
      auto t = static_cast<double>(i) / WIRE_SEGMENTS;
      auto u = 1 - t;
      auto point = Point(
        u * u * u * start.m_x + 3 * u * u * t * first.m_x +
          3 * u * t * t * second.m_x + t * t * t * end.m_x,
        u * u * u * start.m_y + 3 * u * u * t * first.m_y +
          3 * u * t * t * second.m_y + t * t * t * end.m_y);
      add_line(overlay, previous, point, width, color);
      previous = point;
    }
    if(has_arrow) {
      add_line(overlay, end,
        Point(end.m_x - ARROW_LENGTH, end.m_y - ARROW_SPREAD), width, color);
      add_line(overlay, end,
        Point(end.m_x - ARROW_LENGTH, end.m_y + ARROW_SPREAD), width, color);
    }
  }

  struct OffsetLayout {
    InteractionId m_interaction;
    NodeId m_destination;
    double m_stored;
    double m_offset;
    double m_left;
    double m_width;
    double m_top;
    double m_line;
    Rect m_handle;
    bool m_is_small;
  };

  const Interaction* find_current(const ToolContext& context) {
    auto& editor = context.get_editor();
    auto& selection = editor.get_selection();
    if(selection.get_size() != 1) {
      return nullptr;
    }
    auto interactions =
      get_interactions(context.get_document(), selection.get_primary());
    if(interactions.empty()) {
      return nullptr;
    }
    auto id = editor.get_context().get_interaction();
    if(std::ranges::none_of(interactions, [&] (auto interaction) {
        return interaction->m_id == id;
      })) {
      id = find_default_interaction(
        context.get_document(), selection.get_primary());
    }
    for(auto interaction : interactions) {
      if(interaction->m_id == id) {
        return interaction;
      }
    }
    return nullptr;
  }

  std::optional<OffsetLayout> layout_offset(const ToolContext& context) {
    auto interaction = find_current(context);
    if(!interaction) {
      return std::nullopt;
    }
    auto scroll = std::get_if<ScrollTransition>(&interaction->m_action);
    auto& document = context.get_document();
    if(!scroll || scroll->m_destination.is_nil() ||
        !document.find(scroll->m_destination)) {
      return std::nullopt;
    }
    auto artboard =
      document.find(find_artboard(document, interaction->m_owner));
    if(!artboard || !artboard->get_artboard()) {
      return std::nullopt;
    }
    auto& frame = artboard->get_artboard()->m_frame;
    auto bounds = get_world_bounds(document, scroll->m_destination,
      BoundsType::GEOMETRY, context.get_text_measurer());
    auto top = bounds ? bounds->m_y : frame.m_y;
    auto layout = OffsetLayout();
    layout.m_interaction = interaction->m_id;
    layout.m_destination = scroll->m_destination;
    layout.m_stored = scroll->m_offset;
    layout.m_offset =
      clamp_scroll_offset(document, scroll->m_destination, scroll->m_offset);
    auto origin = context.to_view(Point(frame.m_x, top));
    layout.m_left = std::round(origin.m_x);
    layout.m_width = frame.m_width * context.get_zoom();
    layout.m_top = origin.m_y;
    layout.m_line = origin.m_y + layout.m_offset * context.get_zoom();
    layout.m_is_small =
      context.get_zoom() < PrototypeTool::SMALL_OFFSET_HANDLE_ZOOM;
    auto size = layout.m_is_small ? PrototypeTool::SMALL_OFFSET_HANDLE_SIZE :
      PrototypeTool::OFFSET_HANDLE_SIZE;
    layout.m_handle = Rect(layout.m_left - size.m_width,
      layout.m_line - size.m_height / 2, size.m_width, size.m_height);
    return layout;
  }

  std::optional<NodeId> get_destination(const Interaction& interaction) {
    if(auto transition =
        std::get_if<ArtboardTransition>(&interaction.m_action)) {
      return transition->m_destination;
    } else if(auto overlay = std::get_if<Overlay>(&interaction.m_action)) {
      return overlay->m_destination;
    }
    return std::nullopt;
  }
}

PrototypeTool::PrototypeTool()
  : m_initial_offset(0),
    m_offset_start(0),
    m_is_offset_applied(false),
    m_is_over_offset(false) {}

PrototypeTool::~PrototypeTool() = default;

std::optional<Point> PrototypeTool::get_handle(
    const ToolContext& context) const {
  auto wired = get_wired(context);
  if(wired.is_nil()) {
    return std::nullopt;
  }
  auto box = get_selection_box(context.get_document(), std::vector{wired},
    context.get_text_measurer());
  if(!box) {
    return std::nullopt;
  }
  return context.to_view(box->m_transform.map(box->get_point(Handle::RIGHT)));
}

std::optional<Rect> PrototypeTool::get_marker(
    const ToolContext& context, const NodeId& artboard) const {
  auto node = context.get_document().find(artboard);
  if(!node || !node->get_artboard()) {
    return std::nullopt;
  }
  auto frame = context.get_view().map(node->get_artboard()->m_frame);
  return Rect(frame.m_x - MARKER_WIDTH, frame.m_y, MARKER_WIDTH,
    MARKER_HEIGHT);
}

std::optional<Rect> PrototypeTool::get_offset_handle(
    const ToolContext& context) const {
  if(auto layout = layout_offset(context)) {
    return layout->m_handle;
  }
  return std::nullopt;
}

NodeId PrototypeTool::find_destination(
    const ToolContext& context, Point position) const {
  auto& document = context.get_document();
  auto point = context.to_document(position);
  auto& canvas = document.get_canvas();
  for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
    auto node = document.find(*i);
    if(node && node->m_header.m_is_visible && node->get_artboard() &&
        node->get_artboard()->m_frame.contains(point)) {
      return *i;
    }
  }
  return NodeId();
}

void PrototypeTool::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  cancel(context);
  auto& editor = context.get_editor();
  auto& document = context.get_document();
  auto& selection = editor.get_selection();
  auto& edit = editor.get_context();
  if(auto layout = layout_offset(context);
      layout && layout->m_handle.contains(event.m_position)) {
    editor.get_history().seal();
    m_offset_interaction = layout->m_interaction;
    m_initial_offset = layout->m_stored;
    m_offset_start = context.to_document(event.m_position).m_y;
    m_is_offset_applied = false;
    return;
  }
  if(auto handle = get_handle(context)) {
    auto distance = std::hypot(event.m_position.m_x - handle->m_x,
      event.m_position.m_y - handle->m_y);
    if(distance <= HANDLE_RADIUS + 2) {
      m_source = get_wired(context);
      m_wire_end = event.m_position;
      m_target = NodeId();
      return;
    }
  }
  auto markers = std::vector<NodeId>();
  for(auto& flow : document.get_prototype().m_flows) {
    markers.push_back(flow.m_artboard);
  }
  if(auto marked = get_marked_artboard(context); !marked.is_nil()) {
    markers.push_back(marked);
  }
  for(auto& artboard : markers) {
    if(auto marker = get_marker(context, artboard);
        marker && marker->contains(event.m_position)) {
      if(find_flow(document, artboard)) {
        editor.execute(std::make_unique<RemoveFlowCommand>(artboard));
      } else {
        editor.execute(
          std::make_unique<AddFlowCommand>(artboard, std::string()));
      }
      return;
    }
  }
  auto select = [&] (const NodeId& target) {
    if(is_shift(event.m_modifiers)) {
      selection.toggle(target);
    } else if(!selection.contains(target) || selection.get_size() > 1) {
      selection.select(target);
    }
  };
  auto title = context.get_titles().find(event.m_position);
  if(!title.is_nil()) {
    while(!edit.get_container().is_nil()) {
      edit.exit();
    }
    select(title);
    return;
  }
  auto hit = context.get_hit_tester().hit(context.to_document(
    event.m_position), HIT_TOLERANCE / context.get_zoom());
  if(!hit.is_nil() && !is_artboard(document, hit)) {
    auto target = edit.resolve(hit);
    if(!target.is_nil()) {
      select(target);
      return;
    }
  }
  if(!is_shift(event.m_modifiers)) {
    selection.clear();
  }
}

void PrototypeTool::move(ToolContext& context, const PointerEvent& event) {
  if(!m_offset_interaction.is_nil()) {
    auto layout = layout_offset(context);
    if(!layout || layout->m_interaction != m_offset_interaction) {
      return;
    }
    auto delta =
      std::round(context.to_document(event.m_position).m_y - m_offset_start);
    auto offset = clamp_scroll_offset(context.get_document(),
      layout->m_destination, m_initial_offset + delta);
    if(offset == layout->m_offset) {
      return;
    }
    try {
      if(context.get_editor().merge(std::make_unique<SetScrollOffsetCommand>(
          m_offset_interaction, offset))) {
        m_is_offset_applied = true;
      }
    } catch(const std::invalid_argument&) {}
    return;
  }
  if(auto layout = layout_offset(context)) {
    m_is_over_offset = layout->m_handle.contains(event.m_position);
  } else {
    m_is_over_offset = false;
  }
  if(!m_source.is_nil()) {
    m_wire_end = event.m_position;
    m_target = find_destination(context, event.m_position);
    return;
  }
  auto& edit = context.get_editor().get_context();
  auto hover = context.get_titles().find(event.m_position);
  if(hover.is_nil()) {
    auto hit = context.get_hit_tester().hit(context.to_document(
      event.m_position), HIT_TOLERANCE / context.get_zoom());
    if(!hit.is_nil() && !is_artboard(context.get_document(), hit)) {
      hover = edit.resolve(hit);
    }
  }
  edit.set_hover(hover);
}

void PrototypeTool::release(
    ToolContext& context, const PointerEvent& event) {
  if(!m_offset_interaction.is_nil()) {
    context.get_editor().get_history().seal();
    m_offset_interaction = InteractionId();
    m_is_offset_applied = false;
    return;
  }
  if(m_source.is_nil()) {
    return;
  }
  auto destination = find_destination(context, event.m_position);
  if(!destination.is_nil() && context.get_document().find(m_source)) {
    connect(context, destination);
  }
  cancel(context);
}

void PrototypeTool::double_click(
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
  if(target.is_nil() || target == hit || !edit.enter(target)) {
    return;
  }
  if(auto inner = edit.resolve(hit); !inner.is_nil()) {
    editor.get_selection().select(inner);
  }
}

void PrototypeTool::leave(ToolContext& context) {
  m_is_over_offset = false;
  if(m_source.is_nil()) {
    context.get_editor().get_context().set_hover(NodeId());
  }
}

bool PrototypeTool::key_press(ToolContext& context, const KeyEvent& event) {
  if(event.m_key == Qt::Key_Escape && is_busy()) {
    cancel(context);
    return true;
  }
  return false;
}

void PrototypeTool::cancel(ToolContext& context) {
  if(!m_offset_interaction.is_nil()) {
    if(m_is_offset_applied) {
      context.get_editor().undo();
    }
    m_offset_interaction = InteractionId();
    m_is_offset_applied = false;
  }
  m_source = NodeId();
  m_wire_end.reset();
  m_target = NodeId();
}

bool PrototypeTool::is_busy() const {
  return !m_source.is_nil() || !m_offset_interaction.is_nil();
}

ToolCursor PrototypeTool::get_cursor() const {
  if(!m_offset_interaction.is_nil() || m_is_over_offset) {
    return ToolCursor(CursorKind::RESIZE, std::numbers::pi / 2);
  }
  return ToolCursor();
}

void PrototypeTool::draw(
    const ToolContext& context, CanvasOverlay& overlay) const {
  auto& document = context.get_document();
  auto& editor = context.get_editor();
  auto& selection = editor.get_selection();
  auto& measurer = context.get_text_measurer();
  auto scale = context.get_scale();
  auto& hover = editor.get_context().get_hover();
  if(!is_busy() && !hover.is_nil() && !selection.contains(hover) &&
      !is_artboard(document, hover)) {
    if(auto box = get_selection_box(document, std::vector{hover}, measurer)) {
      add_outline(overlay, to_view(context, box->get_corners()),
        OUTLINE_WIDTH, WIRE_COLOR);
    }
  }
  for(auto& id : selection.get_nodes()) {
    if(is_artboard(document, id)) {
      continue;
    }
    if(auto box = get_selection_box(document, std::vector{id}, measurer)) {
      add_outline(overlay, to_view(context, box->get_corners()),
        OUTLINE_WIDTH, WIRE_COLOR);
    }
  }
  auto marked = get_marked_artboard(context);
  for(auto& title : context.get_titles().get_titles()) {
    auto flow = find_flow(document, title.m_artboard);
    if(!flow && title.m_artboard != marked) {
      continue;
    }
    auto marker = get_marker(context, title.m_artboard);
    if(!marker) {
      continue;
    }
    overlay.m_images.push_back(OverlayImage(
      get_marker_image(static_cast<bool>(flow), scale),
      marker->get_top_left()));
    if(flow && !flow->m_name.empty()) {
      auto label = get_label_image(flow->m_name, scale);
      auto width = label->width() / scale;
      overlay.m_images.push_back(OverlayImage(label,
        Point(marker->m_x - width - 2, marker->m_y)));
    }
  }
  auto wired = get_wired(context);
  if(!wired.is_nil()) {
    auto owners = std::vector<NodeId>({wired});
    if(is_artboard(document, wired)) {
      auto pending = std::vector<NodeId>(document.get_children(wired));
      while(!pending.empty()) {
        auto id = pending.back();
        pending.pop_back();
        owners.push_back(id);
        auto& children = document.get_children(id);
        pending.insert(pending.end(), children.begin(), children.end());
      }
    }
    auto current = find_current(context);
    auto selected = std::optional<std::pair<Point, Point>>();
    for(auto& owner : owners) {
      auto interactions = get_interactions(document, owner);
      if(interactions.empty()) {
        continue;
      }
      auto box = get_selection_box(document, std::vector{owner}, measurer);
      if(!box) {
        continue;
      }
      auto start =
        context.to_view(box->m_transform.map(box->get_point(Handle::RIGHT)));
      for(auto interaction : interactions) {
        auto destination = get_destination(*interaction);
        if(!destination) {
          continue;
        }
        auto node = document.find(*destination);
        if(!node || !node->get_artboard()) {
          continue;
        }
        auto end = get_entry(context, node->get_artboard()->m_frame);
        if(interaction == current) {
          selected.emplace(start, end);
        } else if(owner == wired) {
          add_wire(overlay, start, end, true, WIRE_WIDTH, TRIGGER_WIRE_COLOR);
        } else {
          add_wire(overlay, start, end, true, WIRE_WIDTH, OTHER_WIRE_COLOR);
        }
      }
    }
    if(selected) {
      add_wire(overlay, selected->first, selected->second, true,
        SELECTED_WIRE_WIDTH, WIRE_COLOR);
    }
  }
  if(auto layout = layout_offset(context)) {
    if(layout->m_offset != 0) {
      auto band = OverlayShape();
      band.m_center = Point(layout->m_left + layout->m_width / 2,
        (layout->m_top + layout->m_line) / 2);
      band.m_size =
        Size(layout->m_width, std::abs(layout->m_line - layout->m_top));
      band.m_fill = OFFSET_FILL;
      overlay.m_shapes.push_back(band);
      if(m_offset_interaction == layout->m_interaction) {
        auto text = QString::number(layout->m_offset, 'g', 15);
        auto label = get_offset_label_image(text, scale);
        overlay.m_images.push_back(OverlayImage(label, Point(
          layout->m_left + layout->m_width + OFFSET_LABEL_GAP,
          band.m_center.m_y + OFFSET_LABEL_ASCENT / 2 -
            label->height() / scale)));
      }
    }
    overlay.m_shapes.push_back(make_overlay_line(
      Point(layout->m_left, layout->m_line),
      Point(layout->m_left + layout->m_width, layout->m_line), 1 / scale,
      WIRE_COLOR));
    overlay.m_images.push_back(OverlayImage(
      get_offset_handle_image(layout->m_is_small, scale),
      layout->m_handle.get_top_left()));
  }
  if(auto handle = get_handle(context)) {
    if(m_wire_end) {
      if(auto node = document.find(m_target); node && node->get_artboard()) {
        auto frame = context.get_view().map(node->get_artboard()->m_frame);
        add_outline(overlay, std::array<Point, 4>({frame.get_top_left(),
          Point(frame.get_right(), frame.get_top()), frame.get_bottom_right(),
          Point(frame.get_left(), frame.get_bottom())}), TARGET_WIDTH,
          WIRE_COLOR);
      }
      add_wire(overlay, *handle, *m_wire_end, false);
    }
    auto circle = OverlayShape();
    circle.m_center = *handle;
    circle.m_size = Size(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS);
    circle.m_radius = HANDLE_RADIUS;
    circle.m_fill = HANDLE_COLOR;
    overlay.m_shapes.push_back(circle);
    auto tip = Point(handle->m_x + 2.5, handle->m_y);
    add_line(overlay, Point(handle->m_x - 1.5, handle->m_y - 4), tip, 1.5,
      WHITE);
    add_line(overlay, Point(handle->m_x - 1.5, handle->m_y + 4), tip, 1.5,
      WHITE);
  }
}

NodeId PrototypeTool::get_wired(const ToolContext& context) const {
  auto& selection = context.get_editor().get_selection();
  if(selection.get_size() != 1) {
    return NodeId();
  }
  auto& primary = selection.get_primary();
  if(!context.get_document().find(primary)) {
    return NodeId();
  }
  return primary;
}

NodeId PrototypeTool::get_marked_artboard(const ToolContext& context) const {
  auto& document = context.get_document();
  auto& editor = context.get_editor();
  for(auto& id : editor.get_selection().get_nodes()) {
    if(auto artboard = find_artboard(document, id); !artboard.is_nil()) {
      return artboard;
    }
  }
  return find_artboard(document, editor.get_context().get_hover());
}

void PrototypeTool::connect(ToolContext& context, const NodeId& destination) {
  auto& document = context.get_document();
  for(auto interaction : get_interactions(document, m_source)) {
    if(!std::holds_alternative<TapTrigger>(interaction->m_trigger)) {
      continue;
    }
    auto changed = *interaction;
    auto adopt = [&] (NodeId& target, Animation& animation) {
      if(target.is_nil() && animation == make_empty_animation()) {
        animation = make_default_animation();
      }
      target = destination;
    };
    if(auto transition =
        std::get_if<ArtboardTransition>(&changed.m_action)) {
      adopt(transition->m_destination, transition->m_animation);
    } else if(auto overlay = std::get_if<Overlay>(&changed.m_action)) {
      adopt(overlay->m_destination, overlay->m_animation);
    } else {
      continue;
    }
    context.get_editor().execute(
      std::make_unique<SetInteractionCommand>(std::move(changed)));
    return;
  }
  auto commands = std::vector<std::unique_ptr<Command>>();
  commands.push_back(std::make_unique<AddInteractionCommand>(
    make_default_interaction(m_source, destination)));
  auto artboard = find_artboard(document, m_source);
  if(document.get_prototype().m_flows.empty() && !artboard.is_nil()) {
    commands.push_back(
      std::make_unique<AddFlowCommand>(artboard, std::string()));
  }
  context.get_editor().execute(std::make_unique<CompositeCommand>(
    "Add Interaction", std::move(commands)));
}

std::shared_ptr<const QImage> PrototypeTool::get_marker_image(
    bool is_flow, double scale) const {
  auto key = QString("marker|%1|%2").arg(is_flow).arg(scale).toStdString();
  if(auto image = m_images.find(key); image != m_images.end()) {
    return image->second;
  }
  auto width = std::max(1, static_cast<int>(std::ceil(MARKER_WIDTH * scale)));
  auto height =
    std::max(1, static_cast<int>(std::ceil(MARKER_HEIGHT * scale)));
  auto image = std::make_shared<QImage>(
    width, height, QImage::Format_RGBA8888_Premultiplied);
  image->fill(Qt::transparent);
  auto painter = QPainter(image.get());
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(scale, scale);
  auto radius = 3.0;
  auto shape = QPainterPath();
  shape.moveTo(MARKER_WIDTH, 0);
  shape.lineTo(radius, 0);
  shape.quadTo(0, 0, 0, radius);
  shape.lineTo(0, MARKER_HEIGHT - radius);
  shape.quadTo(0, MARKER_HEIGHT, radius, MARKER_HEIGHT);
  shape.lineTo(MARKER_WIDTH, MARKER_HEIGHT);
  shape.closeSubpath();
  painter.fillPath(shape, is_flow ? MARKER_BLUE : MARKER_GREY);
  auto house = QPainterPath();
  auto center = MARKER_WIDTH / 2;
  auto middle = MARKER_HEIGHT / 2;
  house.moveTo(center, middle - 4.5);
  house.lineTo(center + 4.5, middle);
  house.lineTo(center + 3, middle);
  house.lineTo(center + 3, middle + 4);
  house.lineTo(center - 3, middle + 4);
  house.lineTo(center - 3, middle);
  house.lineTo(center - 4.5, middle);
  house.closeSubpath();
  painter.fillPath(house, Qt::white);
  painter.end();
  m_images.emplace(key, image);
  return image;
}

std::shared_ptr<const QImage> PrototypeTool::get_label_image(
    const std::string& name, double scale) const {
  auto key = QString("label|%1|%2").arg(QString::fromStdString(name)).arg(
    scale).toStdString();
  if(auto image = m_images.find(key); image != m_images.end()) {
    return image->second;
  }
  auto font = QFont();
  font.setPixelSize(static_cast<int>(LABEL_FONT_SIZE));
  auto text = QString::fromStdString(name);
  auto text_width = QFontMetricsF(font).horizontalAdvance(text);
  auto width = text_width + 2 * LABEL_PADDING;
  auto image = std::make_shared<QImage>(
    std::max(1, static_cast<int>(std::ceil(width * scale))),
    std::max(1, static_cast<int>(std::ceil(MARKER_HEIGHT * scale))),
    QImage::Format_RGBA8888_Premultiplied);
  image->fill(Qt::transparent);
  auto painter = QPainter(image.get());
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(scale, scale);
  painter.setPen(Qt::NoPen);
  painter.setBrush(MARKER_BLUE);
  painter.drawRoundedRect(QRectF(0, 2, width, MARKER_HEIGHT - 4), 3, 3);
  painter.setFont(font);
  painter.setPen(Qt::white);
  painter.drawText(QRectF(0, 0, width, MARKER_HEIGHT), Qt::AlignCenter, text);
  painter.end();
  if(m_images.size() > 256) {
    m_images.clear();
  }
  m_images.emplace(key, image);
  return image;
}

std::shared_ptr<const QImage> PrototypeTool::get_offset_handle_image(
    bool is_small, double scale) const {
  auto key = QString("offset|%1|%2").arg(is_small).arg(scale).toStdString();
  if(auto image = m_images.find(key); image != m_images.end()) {
    return image->second;
  }
  auto size = is_small ? SMALL_OFFSET_HANDLE_SIZE : OFFSET_HANDLE_SIZE;
  auto image = std::make_shared<QImage>(
    std::max(1, static_cast<int>(std::ceil(size.m_width * scale))),
    std::max(1, static_cast<int>(std::ceil(size.m_height * scale))),
    QImage::Format_RGBA8888_Premultiplied);
  image->fill(Qt::transparent);
  auto painter = QPainter(image.get());
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(scale, scale);
  auto radius = 3.0;
  auto shape = QPainterPath();
  shape.moveTo(size.m_width, 0);
  shape.lineTo(radius, 0);
  shape.quadTo(0, 0, 0, radius);
  shape.lineTo(0, size.m_height - radius);
  shape.quadTo(0, size.m_height, radius, size.m_height);
  shape.lineTo(size.m_width, size.m_height);
  shape.closeSubpath();
  painter.fillPath(shape, MARKER_BLUE);
  if(!is_small) {
    auto bolt = QPainterPath();
    bolt.moveTo(11.6, 2.2);
    bolt.lineTo(4.8, 7);
    bolt.lineTo(7.6, 7);
    bolt.lineTo(7, 10.4);
    bolt.lineTo(12.4, 5.4);
    bolt.lineTo(9.8, 5.4);
    bolt.closeSubpath();
    painter.fillPath(bolt, Qt::white);
  }
  painter.end();
  m_images.emplace(key, image);
  return image;
}

std::shared_ptr<const QImage> PrototypeTool::get_offset_label_image(
    const QString& text, double scale) const {
  auto key = QString("offset_label|%1|%2").arg(text).arg(scale).toStdString();
  if(auto image = m_images.find(key); image != m_images.end()) {
    return image->second;
  }
  auto font = QFont();
  font.setPixelSize(static_cast<int>(OFFSET_LABEL_SIZE));
  auto metrics = QFontMetricsF(font);
  auto image = std::make_shared<QImage>(std::max(1,
    static_cast<int>(std::ceil(metrics.horizontalAdvance(text) * scale))),
    std::max(1, static_cast<int>(std::ceil(metrics.ascent() * scale))),
    QImage::Format_RGBA8888_Premultiplied);
  image->fill(Qt::transparent);
  auto painter = QPainter(image.get());
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(scale, scale);
  painter.setFont(font);
  painter.setPen(OFFSET_LABEL_COLOR);
  painter.drawText(QPointF(0, metrics.ascent()), text);
  painter.end();
  if(m_images.size() > 256) {
    m_images.clear();
  }
  m_images.emplace(key, image);
  return image;
}

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <variant>
#include <QPainter>
#include <QPainterPath>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/CornerRadiusCommands.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Tools/SelectionHandles.hpp"

using namespace Rellu;

namespace {
  constexpr auto SELECTION_COLOR = make_color(38, 128, 235);
  constexpr auto HANDLE_FILL = make_color(255, 255, 255);
  constexpr auto LOCKED_COLOR = make_color(150, 150, 150);
  constexpr auto GRIP_COLOR = make_color(255, 255, 255);
  constexpr auto LINE_WIDTH = 1.0;
  constexpr auto DASH_LENGTH = 2.0;
  constexpr auto RADIUS_DOT_RADIUS = 1.25;
  constexpr auto BADGE_OFFSET = 3.0;
  constexpr auto MINIMUM_SIZE = 1.0;
  constexpr auto ANGLE_TOLERANCE = 1e-12;
  constexpr auto MAXIMUM_OUTLINES = std::size_t(1000);
  constexpr auto GRIP_INSET = 5.4;
  constexpr auto GRIP_GAP = 3.0;
  constexpr auto DISTANT_ZOOM = 0.33;
  constexpr auto DIAGONAL_TOLERANCE = 22.5;

  struct Frame {
    Point m_origin;
    Point m_across;
    Point m_down;
    Size m_size;
  };

  bool is_shift(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ShiftModifier);
  }

  bool is_alt(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::AltModifier);
  }

  bool is_artboard(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->get_artboard();
  }

  bool is_responsive(
      const Document& document, const std::vector<NodeId>& nodes) {
    if(std::none_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
        return document.get(id).m_header.m_constraints.m_is_enabled;
      })) {
      return false;
    } else if(nodes.size() != 1) {
      return true;
    }
    auto& node = document.get(nodes.front());
    if(node.get_artboard()) {
      return true;
    } else if(auto shape = node.get_shape()) {
      return std::holds_alternative<CompoundShape>(shape->m_geometry);
    } else if(auto group = node.get_group(); group && !group->m_repeat_grid) {
      return true;
    }
    return !find_instance(document, nodes.front()).is_nil();
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

  std::array<Point, 4> to_view(
      const Matrix& view, const std::array<Point, 4>& corners) {
    return std::array<Point, 4>({view.map(corners[0]), view.map(corners[1]),
      view.map(corners[2]), view.map(corners[3])});
  }

  void add_dashes(
      CanvasOverlay& overlay, Point start, Point end, Color color) {
    auto length = distance(start, end);
    if(length == 0) {
      return;
    }
    auto direction = (end - start) / length;
    for(auto offset = 0.0; offset < length; offset += 2 * DASH_LENGTH) {
      overlay.m_shapes.push_back(make_overlay_line(start + direction * offset,
        start + direction * std::min(offset + DASH_LENGTH, length),
        LINE_WIDTH, color));
    }
  }

  Size get_size(const std::array<Point, 4>& corners) {
    return Size(
      distance(corners[0], corners[1]), distance(corners[1], corners[2]));
  }

  Frame get_frame(const std::array<Point, 4>& corners) {
    auto across = corners[1] - corners[0];
    auto down = corners[3] - corners[0];
    auto size = Size(length(across), length(down));
    if(size.m_width > 0) {
      across = across / size.m_width;
    }
    if(size.m_height > 0) {
      down = down / size.m_height;
    }
    if(size.m_width == 0 && size.m_height == 0) {
      across = Point(1, 0);
      down = Point(0, 1);
    } else if(size.m_width == 0) {
      across = Point(down.m_y, -down.m_x);
    } else if(size.m_height == 0) {
      down = Point(-across.m_y, across.m_x);
    }
    return Frame(corners[0], across, down, size);
  }

  Point to_frame(const Frame& frame, Point point) {
    auto offset = point - frame.m_origin;
    return Point(dot(offset, frame.m_across), dot(offset, frame.m_down));
  }

  Point from_frame(const Frame& frame, Point point) {
    return frame.m_origin + frame.m_across * point.m_x +
      frame.m_down * point.m_y;
  }

  const Node* find_displayed(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node || !node->get_reference()) {
      return node;
    }
    auto resolution = resolve_reference(document.get_nodes(), id);
    return resolution ? document.find(resolution->m_source) : nullptr;
  }

  bool is_point_text(const Document& document, const NodeId& id) {
    auto node = find_displayed(document, id);
    auto text = node ? node->get_text() : nullptr;
    return text && std::holds_alternative<PositionedFrame>(text->m_frame);
  }

  bool is_auto_height_text(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    auto text = node ? node->get_text() : nullptr;
    return text && std::holds_alternative<AutoHeightFrame>(text->m_frame);
  }

  bool is_area_text(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    auto text = node ? node->get_text() : nullptr;
    return text && std::holds_alternative<AreaFrame>(text->m_frame);
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

  Point constrain_drag(Point delta) {
    auto angle = std::atan2(delta.m_y, delta.m_x) * 180 / std::numbers::pi;
    if(angle < 0) {
      angle += 360;
    }
    if(std::abs(std::fmod(angle, 90) - 45) < DIAGONAL_TOLERANCE) {
      auto side = std::sqrt((delta.m_x * delta.m_x + delta.m_y * delta.m_y) /
        2);
      auto x = angle < 90 || angle > 270 ? 1 : -1;
      auto y = angle < 180 ? 1 : -1;
      return Point(x * side, y * side);
    } else if(std::abs(delta.m_x) > std::abs(delta.m_y)) {
      return Point(delta.m_x, 0);
    }
    return Point(0, delta.m_y);
  }

  bool is_half_pixel_aligned(const Shape& shape) {
    auto& stroke = shape.m_style.m_stroke;
    return stroke && stroke->m_is_enabled &&
      stroke->m_alignment == StrokeAlignment::CENTER &&
      !(stroke->m_color == Color(0, 0, 0, 0));
  }

  Point snap_to_pixels(Point point, bool is_half_pixel) {
    if(is_half_pixel) {
      return Point(std::floor(point.m_x) + 0.5, std::floor(point.m_y) + 0.5);
    }
    return Point(std::round(point.m_x), std::round(point.m_y));
  }

  void add_handle(CanvasOverlay& overlay, Point center, double radius,
      Color fill, Color stroke) {
    auto shape = OverlayShape();
    shape.m_center = center;
    shape.m_size = Size(2 * radius, 2 * radius);
    shape.m_radius = radius;
    shape.m_fill = fill;
    if(!(stroke == Color(0, 0, 0, 0))) {
      shape.m_stroke = stroke;
      shape.m_stroke_width = LINE_WIDTH;
    }
    overlay.m_shapes.push_back(shape);
  }

  void add_scroll_handle(CanvasOverlay& overlay, const Frame& frame,
      Handle handle, double zoom) {
    auto rect = get_scroll_handle_rect(handle, frame.m_size, zoom);
    auto shape = OverlayShape();
    shape.m_center = from_frame(frame, rect.get_center());
    shape.m_size = rect.get_size();
    shape.m_angle = std::atan2(frame.m_across.m_y, frame.m_across.m_x);
    shape.m_radius = SelectionHandles::SCROLL_HANDLE_RADIUS;
    shape.m_fill = SELECTION_COLOR;
    overlay.m_shapes.push_back(shape);
    if(zoom < DISTANT_ZOOM) {
      return;
    }
    auto center = rect.get_center();
    auto is_vertical = handle == Handle::LEFT || handle == Handle::RIGHT;
    for(auto side : {-1.0, 1.0}) {
      auto offset = side * GRIP_GAP / 2;
      auto start = Point(rect.get_left() + GRIP_INSET, center.m_y + offset);
      auto end = Point(rect.get_right() - GRIP_INSET, center.m_y + offset);
      if(is_vertical) {
        start = Point(center.m_x + offset, rect.get_top() + GRIP_INSET);
        end = Point(center.m_x + offset, rect.get_bottom() - GRIP_INSET);
      }
      overlay.m_shapes.push_back(make_overlay_line(from_frame(frame, start),
        from_frame(frame, end), LINE_WIDTH, GRIP_COLOR));
    }
  }

  std::shared_ptr<const QImage> make_lock_badge(double scale) {
    auto side = std::max(1, static_cast<int>(
      std::lround(SelectionHandles::LOCK_BADGE_SIZE * scale)));
    auto image = std::make_shared<QImage>(
      side, side, QImage::Format_RGBA8888_Premultiplied);
    image->fill(Qt::transparent);
    auto painter = QPainter(image.get());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(side / 12.0, side / 12.0);
    auto grey = QColor::fromRgbF(static_cast<float>(LOCKED_COLOR.m_red),
      static_cast<float>(LOCKED_COLOR.m_green),
      static_cast<float>(LOCKED_COLOR.m_blue));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255));
    painter.drawRoundedRect(QRectF(0, 0, 12, 12), 2.5, 2.5);
    painter.setBrush(grey);
    painter.drawRoundedRect(QRectF(2.5, 5.5, 7, 5), 1, 1);
    auto shackle = QPainterPath();
    shackle.moveTo(4, 5.5);
    shackle.lineTo(4, 4);
    shackle.arcTo(QRectF(4, 2, 4, 4), 180, -180);
    shackle.lineTo(8, 5.5);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(grey, 1.2));
    painter.drawPath(shackle);
    painter.end();
    return image;
  }
}

SelectionHandles::SelectionHandles()
  : m_gesture(Gesture::NONE),
    m_press_document(0, 0),
    m_handle(Handle::TOP_LEFT),
    m_grab_offset(0, 0),
    m_resized(0, 0, 0, 0),
    m_rule(ResizeRule::RESPONSIVE),
    m_angle(0),
    m_raw_angle(0),
    m_is_applied(false),
    m_corner(0),
    m_radius(0),
    m_inset(0),
    m_endpoint(0),
    m_fixed(0, 0),
    m_dragged(0, 0),
    m_is_coordinate_snapped(false),
    m_badge_scale(0) {}

std::optional<SelectionBox> SelectionHandles::get_box(
    const ToolContext& context) const {
  update_cache(context);
  return m_cached_box;
}

std::optional<ToolCursor> SelectionHandles::hover(
    const ToolContext& context, Point position) const {
  if(find_endpoint(context, position)) {
    return ToolCursor();
  } else if(auto grab = find_grab(context, position)) {
    auto box = *get_box(context);
    auto angle = get_angle(context.to_view(box.get_center()),
      get_view_point(context, box, grab->m_handle));
    if(grab->m_action == HandleAction::RESIZE) {
      return ToolCursor(CursorKind::RESIZE, angle);
    }
    return ToolCursor(CursorKind::ROTATE, angle);
  } else if(find_radius_handle(context, position)) {
    return ToolCursor();
  }
  return std::nullopt;
}

bool SelectionHandles::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return false;
  }
  if(m_gesture != Gesture::NONE) {
    finish(context);
  }
  auto& editor = context.get_editor();
  auto& document = context.get_document();
  auto position = event.m_position;
  m_press_document = context.to_document(position);
  m_is_applied = false;
  if(auto endpoint = find_endpoint(context, position)) {
    begin_endpoint(context, *get_line_handles(context), *endpoint);
    return true;
  }
  if(auto grab = find_grab(context, position)) {
    m_cursor = *hover(context, position);
    m_nodes = get_movable_nodes(context);
    m_box = *get_box(context);
    m_handle = grab->m_handle;
    if(get_layout(context, m_box).m_style == HandleStyle::SCROLL_GROUP) {
      m_scroll_handle = m_handle;
    }
    if(grab->m_action == HandleAction::RESIZE) {
      m_gesture = Gesture::RESIZE;
      m_resized = m_box.m_rect;
      m_rule = ResizeRule::RESPONSIVE;
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
    return true;
  }
  if(auto corner = find_radius_handle(context, position)) {
    m_cursor = ToolCursor();
    m_radius_handles = get_radius_handles(context);
    m_corner = *corner;
    m_radius = get_corner_radius(m_radius_handles->m_radii, m_corner);
    m_inset = get_inset(context, position);
    m_gesture = Gesture::RADIUS;
    editor.get_history().seal();
    return true;
  }
  return false;
}

void SelectionHandles::move(ToolContext& context, const PointerEvent& event) {
  if(m_gesture == Gesture::RESIZE) {
    update_resize(context, event);
  } else if(m_gesture == Gesture::ROTATE) {
    update_rotate(context, event);
  } else if(m_gesture == Gesture::RADIUS) {
    update_radius(context, event);
  } else if(m_gesture == Gesture::ENDPOINT) {
    update_endpoint(context, event);
  }
}

void SelectionHandles::release(ToolContext& context) {
  finish(context);
}

void SelectionHandles::cancel(ToolContext& context) {
  if(m_gesture != Gesture::NONE && m_is_applied) {
    context.get_editor().undo();
  }
  m_is_applied = false;
  finish(context);
}

bool SelectionHandles::is_active() const {
  return m_gesture != Gesture::NONE;
}

bool SelectionHandles::is_transforming() const {
  return m_gesture == Gesture::RESIZE || m_gesture == Gesture::ROTATE ||
    m_gesture == Gesture::ENDPOINT;
}

ToolCursor SelectionHandles::get_cursor() const {
  return m_cursor;
}

void SelectionHandles::draw(const ToolContext& context,
    CanvasOverlay& overlay, bool has_radius_handles) const {
  if(m_gesture == Gesture::RESIZE) {
    draw_box(context, SelectionBox(m_resized, m_box.m_transform), overlay,
      true);
    return;
  } else if(m_gesture == Gesture::ROTATE) {
    draw_box(context, SelectionBox(m_box.m_rect,
      make_rotation(m_angle, m_box.get_center()) * m_box.m_transform),
      overlay, true);
    return;
  }
  update_cache(context);
  auto& view = context.get_view();
  auto is_greyed = is_locked_selection(context);
  if(m_cached_outlines.size() > 1) {
    auto color = is_greyed ? LOCKED_COLOR : SELECTION_COLOR;
    for(auto& outline : m_cached_outlines) {
      add_box_outline(overlay, to_view(view, outline.get_corners()), color);
    }
  }
  if(is_greyed) {
    if(m_cached_box) {
      draw_locked(context, *m_cached_box, overlay);
    }
    return;
  }
  if(auto line = get_line_handles(context)) {
    for(auto& point : line->m_points) {
      add_handle(overlay, point, HandleLayout::HANDLE_RADIUS, HANDLE_FILL,
        SELECTION_COLOR);
    }
    return;
  }
  if(m_cached_box) {
    draw_box(
      context, *m_cached_box, overlay, !get_movable_nodes(context).empty());
  }
  if(!has_radius_handles) {
    return;
  }
  if(auto handles = get_radius_handles(context)) {
    for(auto& point : handles->m_points) {
      add_handle(overlay, point, RADIUS_HANDLE_RADIUS, HANDLE_FILL,
        SELECTION_COLOR);
      add_handle(overlay, point, RADIUS_DOT_RADIUS, SELECTION_COLOR,
        Color(0, 0, 0, 0));
    }
  }
}

void SelectionHandles::draw_outline(const ToolContext& context,
    const SelectionBox& box, CanvasOverlay& overlay) const {
  auto& nodes = context.get_editor().get_selection().get_nodes();
  add_box_outline(overlay, to_view(context.get_view(), box.get_corners()),
    SELECTION_COLOR, nodes.size() == 1 &&
      is_area_text(context.get_document(), nodes.front()));
}

void SelectionHandles::update_cache(const ToolContext& context) const {
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

bool SelectionHandles::is_locked_selection(const ToolContext& context) const {
  auto& nodes = context.get_editor().get_selection().get_nodes();
  return !nodes.empty() && get_movable_nodes(context).empty() &&
    std::any_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
      return is_locked_node(context.get_document(), id);
    });
}

HandleLayout SelectionHandles::get_layout(
    const ToolContext& context, const SelectionBox& box) const {
  return get_handle_layout(context.get_document(), get_movable_nodes(context),
    get_size(to_view(context.get_view(), box.get_corners())),
    context.get_zoom(), m_scroll_handle);
}

std::optional<HandleHit> SelectionHandles::find_grab(
    const ToolContext& context, Point position) const {
  auto box = get_box(context);
  if(!box || get_movable_nodes(context).empty() ||
      get_line_handles(context)) {
    return std::nullopt;
  }
  auto frame = get_frame(to_view(context.get_view(), box->get_corners()));
  return get_layout(context, *box).find(
    frame.m_size, context.get_zoom(), to_frame(frame, position));
}

std::optional<SelectionHandles::RadiusHandles>
    SelectionHandles::get_radius_handles(const ToolContext& context) const {
  auto& document = context.get_document();
  auto& nodes = context.get_editor().get_selection().get_nodes();
  if(nodes.size() != 1 || get_movable_nodes(context).size() != 1) {
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

std::optional<std::size_t> SelectionHandles::find_radius_handle(
    const ToolContext& context, Point position) const {
  auto handles = get_radius_handles(context);
  if(!handles) {
    return std::nullopt;
  }
  for(auto i = std::size_t(0); i != handles->m_points.size(); ++i) {
    if(distance(handles->m_points[i], position) <=
        HandleLayout::HANDLE_REACH) {
      return i;
    }
  }
  return std::nullopt;
}

double SelectionHandles::get_inset(
    const ToolContext& context, Point position) const {
  auto inverse = invert(m_radius_handles->m_transform);
  if(!inverse) {
    return 0;
  }
  auto offset = inverse->map(context.to_document(position)) -
    get_corner_point(m_radius_handles->m_rect, m_corner);
  auto inward = get_inward(m_corner);
  return (offset.m_x * inward.m_x + offset.m_y * inward.m_y) / 2;
}

std::optional<SelectionHandles::LineHandles>
    SelectionHandles::get_line_handles(const ToolContext& context) const {
  auto& document = context.get_document();
  auto& nodes = context.get_editor().get_selection().get_nodes();
  if(nodes.size() != 1 || get_movable_nodes(context).size() != 1) {
    return std::nullopt;
  }
  auto node = document.find(nodes.front());
  auto shape = node ? node->get_shape() : nullptr;
  auto line = shape ? std::get_if<LineShape>(&shape->m_geometry) : nullptr;
  if(!line) {
    return std::nullopt;
  }
  auto transform = get_world_transform(document, node->get_id());
  return LineHandles(node->get_id(), std::array<Point, 2>({
    context.to_view(transform.map(line->m_start)),
    context.to_view(transform.map(line->m_end))}));
}

std::optional<std::size_t> SelectionHandles::find_endpoint(
    const ToolContext& context, Point position) const {
  auto handles = get_line_handles(context);
  if(!handles) {
    return std::nullopt;
  }
  for(auto i = handles->m_points.size(); i-- != 0;) {
    if(distance(handles->m_points[i], position) <=
        HandleLayout::HANDLE_REACH) {
      return i;
    }
  }
  return std::nullopt;
}

void SelectionHandles::begin_endpoint(ToolContext& context,
    const LineHandles& handles, std::size_t endpoint) {
  auto& document = context.get_document();
  auto& node = document.get(handles.m_id);
  auto& line = std::get<LineShape>(node.get_shape()->m_geometry);
  auto& transform = node.m_header.m_transform;
  m_gesture = Gesture::ENDPOINT;
  m_cursor = ToolCursor();
  m_line = handles.m_id;
  m_endpoint = endpoint;
  m_parent_transform = get_world_transform(document, node.m_parent);
  m_fixed = transform.map(endpoint == 0 ? line.m_end : line.m_start);
  m_dragged = transform.map(endpoint == 0 ? line.m_start : line.m_end);
  auto fixed = m_parent_transform.map(m_fixed);
  m_is_coordinate_snapped = fixed.m_x == std::round(fixed.m_x) &&
    fixed.m_y == std::round(fixed.m_y);
  context.get_editor().get_history().seal();
  context.get_snap_service().begin(node.m_parent, std::vector{m_line});
}

void SelectionHandles::update_resize(
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
  auto& document = context.get_document();
  auto is_flippable = std::none_of(m_nodes.begin(), m_nodes.end(),
    [&] (const auto& id) {
      return is_artboard(document, id);
    });
  auto is_shift_held = is_shift(event.m_modifiers);
  auto is_locked = m_nodes.size() == 1 &&
    document.get(m_nodes.front()).m_header.m_aspect_lock;
  auto is_proportional = is_shift_held || is_locked;
  if(is_responsive(document, m_nodes)) {
    is_proportional = (is_shift_held && is_corner(m_handle)) || is_locked;
  }
  if(m_nodes.size() == 1 && is_point_text(document, m_nodes.front())) {
    is_proportional = true;
  }
  auto rule = ResizeRule::RESPONSIVE;
  if(is_shift_held && !std::all_of(m_nodes.begin(), m_nodes.end(),
      [&] (const auto& id) {
        return document.get(id).m_header.m_constraints.m_is_enabled &&
          !find_instance(document, id).is_nil();
      })) {
    rule = ResizeRule::SUPPRESSED;
  }
  auto rect = resize(m_box.m_rect, m_handle, local, is_proportional,
    is_alt(event.m_modifiers), is_flippable, MINIMUM_SIZE);
  if(rect == m_resized && rule == m_rule) {
    return;
  }
  auto& editor = context.get_editor();
  if(m_is_applied) {
    editor.undo();
    m_is_applied = false;
  }
  m_resized = rect;
  m_rule = rule;
  if(rect == m_box.m_rect) {
    return;
  }
  auto& measurer = context.get_text_measurer();
  auto mapping =
    transform * make_rect_mapping(m_box.m_rect, rect) * *inverse;
  auto command = std::unique_ptr<Command>(std::make_unique<TransformCommand>(
    m_nodes, mapping, "Resize", TransformMode::DRAG, rule, measurer));
  if(m_nodes.size() == 1 &&
      (m_handle == Handle::TOP || m_handle == Handle::BOTTOM) &&
      is_auto_height_text(document, m_nodes.front())) {
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(std::make_unique<SetTextFrameCommand>(
      m_nodes, TextFrameKind::FIXED_SIZE, measurer));
    commands.push_back(std::move(command));
    command = std::make_unique<CompositeCommand>("Resize", std::move(commands));
  }
  if(editor.execute(std::move(command))) {
    m_is_applied = true;
  }
}

void SelectionHandles::update_rotate(
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
  auto& document = context.get_document();
  if(m_nodes.size() == 1 && is_repeat_grid(document, m_nodes.front())) {
    context.get_editor().merge(std::make_unique<TransformCommand>(
      m_nodes, make_rotation(step, center), "Rotate Repeat Grid"));
  } else {
    context.get_editor().merge(make_rotate_command(m_nodes, step, center));
  }
  m_angle = angle;
  m_is_applied = true;
}

void SelectionHandles::update_radius(
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

void SelectionHandles::update_endpoint(
    ToolContext& context, const PointerEvent& event) {
  auto& document = context.get_document();
  auto inverse = invert(m_parent_transform);
  auto node = document.find(m_line);
  if(!inverse || !node || !node->get_shape()) {
    return;
  }
  auto is_constrained = is_shift(event.m_modifiers);
  auto is_mirrored = is_alt(event.m_modifiers);
  auto point = inverse->map(context.to_document(event.m_position));
  auto anchor = m_fixed;
  if(is_mirrored) {
    anchor = (m_fixed + m_dragged) / 2;
  }
  auto delta = point - anchor;
  auto end = point;
  if(is_constrained) {
    delta = constrain_drag(delta);
    end = anchor + delta;
  }
  auto start = anchor;
  if(is_mirrored) {
    start = anchor - delta;
  }
  auto is_horizontal = start.m_y == end.m_y;
  auto is_vertical = start.m_x == end.m_x;
  auto global_start = m_parent_transform.map(start);
  auto global_end = m_parent_transform.map(end);
  if(!is_constrained) {
    global_end = context.get_snap_service().snap_point(
      global_end, SnapAxes::BOTH, event.m_modifiers);
    if(is_mirrored) {
      global_start = 2 * m_parent_transform.map(anchor) - global_end;
    }
  }
  auto is_half_pixel = !m_is_coordinate_snapped &&
    is_half_pixel_aligned(*node->get_shape());
  if(is_mirrored) {
    global_start = snap_to_pixels(global_start, is_half_pixel);
  }
  global_end = snap_to_pixels(global_end, is_half_pixel);
  start = inverse->map(global_start);
  end = inverse->map(global_end);
  if(is_horizontal) {
    end.m_y = start.m_y;
  }
  if(is_vertical) {
    end.m_x = start.m_x;
  }
  auto first = m_endpoint == 0 ? end : start;
  auto last = m_endpoint == 0 ? start : end;
  auto& editor = context.get_editor();
  if(m_is_applied) {
    editor.undo();
    m_is_applied = false;
  }
  node = document.find(m_line);
  if(!node || !node->get_shape()) {
    return;
  }
  auto header = node->m_header;
  auto shape = *node->get_shape();
  auto& transform = header.m_transform;
  if(transform.m_a == 1 && transform.m_b == 0 && transform.m_c == 0 &&
      transform.m_d == 1) {
    auto origin = Point(std::min(first.m_x, last.m_x),
      std::min(first.m_y, last.m_y));
    transform = make_translation(origin.m_x, origin.m_y);
    shape.m_geometry = LineShape(first - origin, last - origin);
  } else if(auto local = invert(transform)) {
    shape.m_geometry = LineShape(local->map(first), local->map(last));
  } else {
    return;
  }
  if(editor.execute(std::make_unique<InsertNodeCommand>("Drag Handle",
      node->m_parent, std::move(header), std::move(shape)))) {
    m_is_applied = true;
  }
}

void SelectionHandles::finish(ToolContext& context) {
  auto& editor = context.get_editor();
  if(is_transforming()) {
    if(m_gesture == Gesture::RESIZE && m_is_applied &&
        std::any_of(m_nodes.begin(), m_nodes.end(), [&] (const auto& id) {
          return is_artboard(context.get_document(), id);
        })) {
      editor.merge(std::make_unique<TransformCommand>(
        m_nodes, Matrix(), "Resize", TransformMode::SETTLE));
    }
    editor.get_history().release_layout();
    editor.get_history().seal();
    context.get_snap_service().end();
  } else if(m_gesture == Gesture::RADIUS) {
    editor.get_history().seal();
  }
  m_gesture = Gesture::NONE;
  m_radius_handles = std::nullopt;
  m_line = NodeId();
  m_nodes.clear();
}

void SelectionHandles::draw_box(const ToolContext& context,
    const SelectionBox& box, CanvasOverlay& overlay, bool has_handles) const {
  draw_outline(context, box, overlay);
  if(!has_handles) {
    return;
  }
  auto corners = to_view(context.get_view(), box.get_corners());
  auto layout = get_layout(context, box);
  auto frame = get_frame(corners);
  for(auto handle : HANDLES) {
    if(!layout.is_resizer(handle)) {
      continue;
    }
    if(layout.m_style == HandleStyle::SCROLL_GROUP) {
      if(m_gesture != Gesture::RESIZE || handle == m_handle) {
        add_scroll_handle(overlay, frame, handle, context.get_zoom());
      }
    } else {
      add_handle(overlay, get_view_point(context, box, handle),
        HandleLayout::HANDLE_RADIUS, HANDLE_FILL, SELECTION_COLOR);
    }
  }
}

void SelectionHandles::draw_locked(const ToolContext& context,
    const SelectionBox& box, CanvasOverlay& overlay) const {
  auto corners = to_view(context.get_view(), box.get_corners());
  add_box_outline(overlay, corners, LOCKED_COLOR);
  auto layout = get_locked_handle_layout(get_size(corners));
  for(auto handle : HANDLES) {
    if(layout.is_resizer(handle)) {
      add_handle(overlay, get_view_point(context, box, handle),
        LOCKED_HANDLE_RADIUS, LOCKED_COLOR, Color(0, 0, 0, 0));
    }
  }
  auto scale = context.get_scale();
  if(!(scale > 0)) {
    return;
  }
  if(!m_badge || m_badge_scale != scale) {
    m_badge = make_lock_badge(scale);
    m_badge_scale = scale;
  }
  auto corner = get_view_point(context, box, Handle::TOP_LEFT);
  auto offset = LOCK_BADGE_SIZE + BADGE_OFFSET;
  overlay.m_images.push_back(
    OverlayImage(m_badge, corner - Point(offset, offset)));
}

std::vector<NodeId> Rellu::get_movable_nodes(const ToolContext& context) {
  auto& document = context.get_document();
  auto nodes = get_top_level(
    document, context.get_editor().get_selection().get_nodes());
  std::erase_if(nodes, [&] (const auto& id) {
    return is_locked_node(document, id);
  });
  return nodes;
}

bool Rellu::is_locked_node(const Document& document, const NodeId& id) {
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

void Rellu::add_box_outline(CanvasOverlay& overlay,
    const std::array<Point, 4>& corners, Color color, bool is_bottom_dashed) {
  for(auto i = std::size_t(0); i != corners.size(); ++i) {
    auto start = corners[i];
    auto end = corners[(i + 1) % corners.size()];
    auto length = distance(start, end);
    if(length == 0) {
      continue;
    }
    if(is_bottom_dashed && i == 2) {
      add_dashes(overlay, start, end, color);
      continue;
    }
    auto extension = (end - start) * (LINE_WIDTH / 2 / length);
    overlay.m_shapes.push_back(make_overlay_line(
      start - extension, end + extension, LINE_WIDTH, color));
  }
}

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>
#include <variant>
#include <QPainter>
#include <QPainterPath>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CornerRadiusCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/Layouts.hpp"
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
  constexpr auto CONTEXT_COLOR = make_color(38, 128, 235, 0.45);
  constexpr auto MARQUEE_FILL = make_color(38, 128, 235, 0.08);
  constexpr auto HANDLE_FILL = make_color(255, 255, 255);
  constexpr auto LOCKED_COLOR = make_color(150, 150, 150);
  constexpr auto LINE_WIDTH = 1.0;
  constexpr auto DASH_LENGTH = 2.0;
  constexpr auto RADIUS_DOT_RADIUS = 1.25;
  constexpr auto BADGE_OFFSET = 3.0;
  constexpr auto MINIMUM_SIZE = 1.0;
  constexpr auto ANGLE_TOLERANCE = 1e-12;
  constexpr auto MAXIMUM_OUTLINES = std::size_t(1000);
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

  void add_outline(CanvasOverlay& overlay, const std::array<Point, 4>& corners,
      Color color, bool is_bottom_dashed = false) {
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

  Size get_size(const std::array<Point, 4>& corners) {
    return Size(
      distance(corners[0], corners[1]), distance(corners[1], corners[2]));
  }

  bool is_point_text(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(node && node->get_reference()) {
      auto resolution = resolve_reference(document.get_nodes(), id);
      node = resolution ? document.find(resolution->m_source) : nullptr;
    }
    auto text = node ? node->get_text() : nullptr;
    return text && std::holds_alternative<PositionedFrame>(text->m_frame);
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

  std::shared_ptr<const QImage> make_lock_badge(double scale) {
    auto side = std::max(1, static_cast<int>(
      std::lround(SelectTool::LOCK_BADGE_SIZE * scale)));
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
    m_rule(ResizeRule::RESPONSIVE),
    m_angle(0),
    m_raw_angle(0),
    m_is_applied(false),
    m_is_copied(false),
    m_marquee(0, 0, 0, 0),
    m_nudge_key(0),
    m_corner(0),
    m_radius(0),
    m_inset(0),
    m_badge_scale(0) {}

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
  if(m_guides.press(context, event)) {
    m_gesture = Gesture::GUIDE;
    if(auto cursor = m_guides.hover(context, event.m_position)) {
      m_cursor = *cursor;
    }
    return;
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
  auto hit = find_hit(context, m_press_document);
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
  } else if(m_gesture == Gesture::GUIDE) {
    m_guides.release(context, event);
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
  auto hit = find_hit(context, context.to_document(event.m_position));
  if(hit.is_nil() || is_artboard(document, hit)) {
    return;
  }
  auto& edit = editor.get_context();
  auto target = edit.resolve(hit);
  if(is_locked(document, target)) {
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
  std::erase_if(nodes, [&] (const auto& id) {
    return is_stacked(context.get_document(), id, delta);
  });
  if(nodes.empty()) {
    return true;
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
  } else if(m_gesture == Gesture::GUIDE) {
    m_guides.cancel(context);
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
  m_guides.draw(context, overlay);
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
    draw_hover(context, hover, overlay);
  }
  auto is_greyed = !is_dragging && is_locked_selection(context);
  auto box = std::optional<SelectionBox>();
  if(is_dragging) {
    box = get_gesture_box();
  } else {
    update_cache(context);
    box = m_cached_box;
    if(m_cached_outlines.size() > 1) {
      auto color = is_greyed ? LOCKED_COLOR : SELECTION_COLOR;
      for(auto& outline : m_cached_outlines) {
        add_outline(overlay, to_view(view, outline.get_corners()), color);
      }
    }
  }
  if(box && is_greyed) {
    draw_locked(context, *box, overlay);
  } else if(box) {
    auto& nodes = selection.get_nodes();
    auto node = nodes.size() == 1 ? document.find(nodes.front()) : nullptr;
    auto text = node ? node->get_text() : nullptr;
    draw_box(context, *box, overlay, m_gesture != Gesture::MOVE &&
      !get_movable(context).empty() &&
      m_layout_handles.get_grid(context).is_nil(),
      text && std::holds_alternative<AreaFrame>(text->m_frame));
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
        auto dot = OverlayShape();
        dot.m_center = point;
        dot.m_size = Size(2 * RADIUS_DOT_RADIUS, 2 * RADIUS_DOT_RADIUS);
        dot.m_radius = RADIUS_DOT_RADIUS;
        dot.m_fill = SELECTION_COLOR;
        overlay.m_shapes.push_back(dot);
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
    cache.m_outline = get_outline(context.get_document(), id);
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

bool SelectTool::is_locked_selection(const ToolContext& context) const {
  auto& nodes = context.get_editor().get_selection().get_nodes();
  return !nodes.empty() && get_movable(context).empty() &&
    std::any_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
      return is_locked(context.get_document(), id);
    });
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
  auto layout = get_layout(context, *box);
  auto find_handle = [&] (bool is_corner_wanted) -> std::optional<Grab> {
    for(auto handle : HANDLES) {
      if(is_corner(handle) != is_corner_wanted ||
          !layout.is_resizer(handle)) {
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
  if(!is_rotatable(context)) {
    return std::nullopt;
  }
  auto is_within = is_inside(corners, position);
  for(auto handle : HANDLES) {
    if(!layout.is_rotator(handle)) {
      continue;
    }
    auto reach = distance(get_view_point(context, *box, handle), position);
    if((!layout.is_resizer(handle) && reach <= HANDLE_REACH) ||
        (!is_within && reach <= ROTATION_REACH)) {
      return Grab(Target::ROTATION, handle);
    }
  }
  return std::nullopt;
}

HandleLayout SelectTool::get_layout(
    const ToolContext& context, const SelectionBox& box) const {
  return get_handle_layout(context.get_document(), get_movable(context),
    get_size(to_view(context.get_view(), box.get_corners())),
    context.get_zoom());
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
  auto mapping =
    transform * make_rect_mapping(m_box.m_rect, rect) * *inverse;
  if(editor.execute(std::make_unique<TransformCommand>(m_nodes, mapping,
      "Resize", TransformMode::DRAG, rule, context.get_text_measurer()))) {
    m_is_applied = true;
  }
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
  } else if(auto cursor = m_guides.hover(context, event.m_position);
      cursor && !find_grab(context, event.m_position)) {
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
    auto hit = find_hit(context, context.to_document(event.m_position));
    if(!hit.is_nil() && !is_artboard(context.get_document(), hit)) {
      hover = resolve_target(context, hit, event.m_modifiers, false);
    }
  }
  if(is_locked(context.get_document(), hover)) {
    edit.set_hover(NodeId());
  } else {
    edit.set_hover(hover);
  }
  measure(context, get_box(context), hover, event.m_modifiers);
}

void SelectTool::finish(ToolContext& context) {
  if(m_gesture == Gesture::LAYOUT) {
    m_layout_handles.release(context);
  } else if(m_gesture == Gesture::MOVE || m_gesture == Gesture::RESIZE ||
      m_gesture == Gesture::ROTATE) {
    if(m_gesture == Gesture::RESIZE && m_is_applied &&
        std::any_of(m_nodes.begin(), m_nodes.end(), [&] (const auto& id) {
          return is_artboard(context.get_document(), id);
        })) {
      context.get_editor().merge(std::make_unique<TransformCommand>(
        m_nodes, Matrix(), "Resize", TransformMode::SETTLE));
    }
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
    CanvasOverlay& overlay, bool has_handles, bool is_frame) const {
  auto corners = to_view(context.get_view(), box.get_corners());
  add_outline(overlay, corners, SELECTION_COLOR, is_frame);
  if(!has_handles) {
    return;
  }
  auto layout = get_layout(context, box);
  for(auto handle : HANDLES) {
    if(!layout.is_resizer(handle)) {
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

void SelectTool::draw_locked(const ToolContext& context,
    const SelectionBox& box, CanvasOverlay& overlay) const {
  auto corners = to_view(context.get_view(), box.get_corners());
  add_outline(overlay, corners, LOCKED_COLOR);
  auto layout = get_locked_handle_layout(get_size(corners));
  for(auto handle : HANDLES) {
    if(!layout.is_resizer(handle)) {
      continue;
    }
    auto shape = OverlayShape();
    shape.m_center = get_view_point(context, box, handle);
    shape.m_size = Size(2 * LOCKED_HANDLE_RADIUS, 2 * LOCKED_HANDLE_RADIUS);
    shape.m_radius = LOCKED_HANDLE_RADIUS;
    shape.m_fill = LOCKED_COLOR;
    overlay.m_shapes.push_back(shape);
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

void SelectTool::draw_hover(const ToolContext& context, const NodeId& hover,
    CanvasOverlay& overlay) const {
  auto& box = get_box(context, hover, m_hover_box);
  auto& view = context.get_view();
  if(m_hover_box.m_outline && add_path_outline(
      overlay, *m_hover_box.m_outline, view, SELECTION_COLOR)) {
    return;
  }
  if(box) {
    add_outline(overlay, to_view(view, box->get_corners()), SELECTION_COLOR);
  }
}

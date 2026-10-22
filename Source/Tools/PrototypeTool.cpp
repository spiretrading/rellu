#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QTransform>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Tools/PrototypeTool.hpp"
#include "Rellu/Tools/SelectionBox.hpp"
#include "Rellu/Tools/ToolContext.hpp"

using namespace Rellu;

namespace {
  constexpr auto WIRE_COLOR = make_color(38, 128, 235);
  constexpr auto HANDLE_COLOR = make_color(38, 128, 235);
  constexpr auto WHITE = make_color(255, 255, 255);
  constexpr auto WIRE_WIDTH = 2.0;
  constexpr auto SELECTED_WIRE_WIDTH = 4.0;
  constexpr auto OVERLAY_WIRE_WIDTH = 3.0;
  constexpr auto OVERLAY_DASH = 10.0;
  constexpr auto OUTLINE_WIDTH = 1.0;
  constexpr auto TARGET_WIDTH = 1.0;
  constexpr auto WIRING_DOT_RADIUS = 7.0;
  constexpr auto WIRE_SAMPLES = 64;
  constexpr auto BEZIER_MINIMUM = 70.0;
  constexpr auto BEZIER_SCALE = 0.9;
  constexpr auto TAB_WIDTH = 13.0;
  constexpr auto TAB_HALF_HEIGHT = 7.0;
  constexpr auto MULTIPLE_HANDLE_RADIUS = 10.0;
  constexpr auto ARTBOARD_TRIGGER_OFFSET = 20.0;
  constexpr auto KNOB_RADIUS = 6.0;
  constexpr auto LARGE_KNOB_RADIUS = 10.0;
  constexpr auto LARGE_KNOB_ZOOM = 0.33;
  constexpr auto KNOB_OFFSET = 60.0;
  constexpr auto TAIL_KNOB_DISTANCE = 120.0;
  constexpr auto TAIL_OFFSET = Point(60, -40);
  constexpr auto SMALL_TAIL_OFFSET = Point(30, -20);
  constexpr auto SMALL_TAIL_ZOOM = 0.33;
  constexpr auto KNOB_FONT_SIZE = 11.0;
  constexpr auto IMAGE_LIMIT = 256;
  constexpr auto LABEL_PADDING = 7.0;
  constexpr auto LABEL_HEIGHT = 28.0;
  constexpr auto LABEL_RADIUS = 4.0;
  constexpr auto LABEL_BASELINE = 19.0;
  constexpr auto LABEL_OFFSET = -57.0;
  constexpr auto SMALL_LABEL_OFFSET = -39.0;
  constexpr auto SMALL_LABEL_ZOOM = 0.16;
  constexpr auto PLUS_SIZE = 8.0;
  constexpr auto TIP_OFFSET = Point(15, -30);
  constexpr auto TIP_HEIGHT = 19.0;
  constexpr auto TIP_PADDING = 8.0;
  constexpr auto TIP_BASELINE = 13.0;
  constexpr auto TIP_RADIUS = 4.0;
  constexpr auto TIP_FONT_SIZE = 11.0;
  constexpr auto PLUS_WIDTH = 1.5;
  constexpr auto MARKER_RADIUS = 17.0 / 60;
  constexpr auto MARKER_RIM = 0.28;
  constexpr auto ARTBOARD_STROKE = 1.0;
  constexpr auto HOUSE_SIZE = Size(8, 22.0 / 3);
  constexpr auto LARGE_HOUSE_TOP = 34.0 / 3;
  constexpr auto MEDIUM_HOUSE_TOP = 8.0;
  constexpr auto LABEL_FONT_SIZE = 14.0;
  constexpr auto OFFSET_FILL = make_color(255, 74, 225, 0.4);
  constexpr auto OFFSET_LABEL_SIZE = 10.0;
  constexpr auto OFFSET_LABEL_GAP = 6.0;
  constexpr auto OFFSET_LABEL_ASCENT = 6.0;
  const auto MARKER_GREY = QColor(194, 194, 194);
  const auto MARKER_HOVER = QColor(181, 181, 181);
  const auto LABEL_GREY = QColor(75, 75, 75);
  const auto MARKER_BLUE = QColor(38, 128, 235);
  const auto OFFSET_LABEL_COLOR = QColor(255, 73, 225);
  const auto SELECTED_WIRE = QColor(38, 128, 235);
  const auto TRIGGER_WIRE = QColor(126, 188, 230);
  const auto OTHER_WIRE = QColor(102, 102, 102);
  const auto SELECT_FILL = QColor(38, 128, 235, 77);
  const auto TIP_BACKGROUND = QColor(0, 0, 0, 204);

  enum class Side : unsigned char {
    RIGHT,
    TOP,
    LEFT,
    BOTTOM
  };

  enum class KnobIcon : unsigned char {
    NONE,
    DIRECTION,
    REVERSE,
    PREVIOUS,
    MULTIPLE
  };

  struct HandleLayout {
    Side m_side = Side::RIGHT;
    Point m_anchor = Point(0, 0);
    Point m_tip = Point(0, 0);
    Point m_normal = Point(1, 0);
    Point m_along = Point(0, 1);
    double m_offset = 0;
    double m_width = 0;
    double m_height = 0;
    Rect m_bounds;
  };

  struct WireLayout {
    NodeId m_owner;
    NodeId m_destination;
    std::vector<const Interaction*> m_interactions;
    bool m_is_selected = false;
    bool m_is_trigger_selected = false;
    bool m_is_overlay = false;
    bool m_is_pending = false;
    HandleLayout m_trigger;
    std::optional<HandleLayout> m_target;
    Point m_end = Point(0, 0);
    std::array<Point, 4> m_curve;
    std::vector<Point> m_points;
    std::optional<Point> m_knob;
    double m_knob_radius = 0;
    KnobIcon m_icon = KnobIcon::NONE;
  };

  struct WireHit {
    std::size_t m_index = 0;
    Point m_point = Point(0, 0);
    double m_distance = 0;
  };

  struct WireHover {
    bool m_is_shown = false;
    NodeId m_owner;
    NodeId m_destination;
    Point m_point = Point(0, 0);
  };

  struct WireTab {
    NodeId m_node;
    HandleLayout m_layout;
    bool m_is_destination = false;
    bool m_is_selected = false;
    bool m_is_trigger_selected = false;
  };

  using ImageCache =
    std::unordered_map<std::string, std::shared_ptr<const QImage>>;

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

  NodeId find_top_level(const Document& document, const NodeId& id) {
    auto current = id;
    for(auto depth = 0; depth != 512 && !current.is_nil(); ++depth) {
      auto node = document.find(current);
      if(!node) {
        return NodeId();
      }
      if(node->m_parent.is_nil()) {
        return current;
      }
      current = node->m_parent;
    }
    return NodeId();
  }

  bool is_shown(const Document& document, const NodeId& id) {
    auto current = id;
    for(auto depth = 0; depth != 512 && !current.is_nil(); ++depth) {
      auto node = document.find(current);
      if(!node || !node->m_header.m_is_visible) {
        return false;
      }
      current = node->m_parent;
    }
    return true;
  }

  double get_length(Point point) {
    return std::hypot(point.m_x, point.m_y);
  }

  Point normalize(Point point) {
    auto length = get_length(point);
    if(length == 0) {
      return Point(0, 0);
    }
    return point / length;
  }

  double get_width(const WireLayout& wire) {
    if(wire.m_is_selected) {
      return SELECTED_WIRE_WIDTH;
    } else if(wire.m_is_overlay) {
      return OVERLAY_WIRE_WIDTH;
    }
    return WIRE_WIDTH;
  }

  QColor get_wire_color(bool is_selected, bool is_trigger_selected) {
    if(is_selected) {
      return SELECTED_WIRE;
    } else if(is_trigger_selected) {
      return TRIGGER_WIRE;
    }
    return OTHER_WIRE;
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

  std::vector<const Interaction*> find_selected(const ToolContext& context) {
    auto selected = std::vector<const Interaction*>();
    auto& editor = context.get_editor();
    auto& selection = editor.get_selection();
    if(selection.is_empty()) {
      return selected;
    }
    auto& edit = editor.get_context();
    if(edit.get_interaction_owner() == selection.get_primary()) {
      for(auto& id : edit.get_interactions()) {
        for(auto& interaction :
            context.get_document().get_prototype().m_interactions) {
          if(interaction.m_id == id) {
            selected.push_back(&interaction);
          }
        }
        for(auto& interaction : edit.get_pending()) {
          if(interaction.m_id == id && std::ranges::none_of(selected,
              [&] (auto stored) {
                return stored->m_id == id;
              })) {
            selected.push_back(&interaction);
          }
        }
      }
      if(!selected.empty() || edit.get_interactions().empty()) {
        return selected;
      }
    }
    if(selection.get_size() != 1) {
      return selected;
    }
    auto interactions =
      get_interactions(context.get_document(), selection.get_primary());
    auto id = find_default_interaction(
      context.get_document(), selection.get_primary());
    for(auto interaction : interactions) {
      if(interaction->m_id == id) {
        selected.push_back(interaction);
      }
    }
    return selected;
  }

  const Interaction* find_current(const ToolContext& context) {
    auto selected = find_selected(context);
    if(selected.size() != 1) {
      return nullptr;
    }
    return selected.front();
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

  std::optional<SelectionBox> get_frame(
      const ToolContext& context, const NodeId& id) {
    auto& document = context.get_document();
    auto node = document.find(id);
    if(!node) {
      return std::nullopt;
    }
    if(auto artboard = node->get_artboard()) {
      auto box = SelectionBox();
      box.m_rect = artboard->m_frame;
      return box;
    }
    return get_selection_box(
      document, std::vector{id}, context.get_text_measurer());
  }

  std::optional<Rect> get_global_bounds(
      const ToolContext& context, const NodeId& id) {
    auto& document = context.get_document();
    if(auto node = document.find(id); node && node->get_artboard()) {
      return node->get_artboard()->m_frame;
    }
    return get_world_bounds(
      document, id, BoundsType::GEOMETRY, context.get_text_measurer());
  }

  HandleLayout layout_handle(const ToolContext& context,
      const SelectionBox& frame, Side side, bool is_artboard_trigger,
      bool is_multiple) {
    auto rotation = get_rotation(frame.m_transform);
    auto turn = static_cast<int>(std::floor((rotation + 45) / 90)) + 4;
    auto local = static_cast<Side>((static_cast<int>(side) + turn) % 4);
    auto& rect = frame.m_rect;
    auto anchor = Point(0, 0);
    auto along = Point(0, 0);
    if(local == Side::LEFT) {
      anchor = Point(rect.m_x, rect.m_y + rect.m_height / 2);
      along = Point(0, -1);
    } else if(local == Side::TOP) {
      anchor = Point(rect.m_x + rect.m_width / 2, rect.m_y);
      along = Point(1, 0);
    } else if(local == Side::RIGHT) {
      anchor = Point(rect.m_x + rect.m_width, rect.m_y + rect.m_height / 2);
      along = Point(0, 1);
    } else {
      anchor = Point(rect.m_x + rect.m_width / 2, rect.m_y + rect.m_height);
      along = Point(-1, 0);
    }
    auto map = [&] (Point point) {
      return context.to_view(frame.m_transform.map(point));
    };
    auto layout = HandleLayout();
    layout.m_side = side;
    layout.m_anchor = map(anchor);
    layout.m_along = normalize(map(anchor + along) - layout.m_anchor);
    layout.m_normal = Point(layout.m_along.m_y, -layout.m_along.m_x);
    auto tip = Point(0, 0);
    if(is_multiple) {
      layout.m_width = MULTIPLE_HANDLE_RADIUS;
      layout.m_height = 2 * MULTIPLE_HANDLE_RADIUS;
      layout.m_offset = is_artboard_trigger ? ARTBOARD_TRIGGER_OFFSET : 0;
    } else {
      layout.m_width = TAB_WIDTH;
      layout.m_height = 2 * TAB_HALF_HEIGHT;
      tip = Point(TAB_WIDTH, TAB_HALF_HEIGHT);
      layout.m_offset = TAB_HALF_HEIGHT +
        (is_artboard_trigger ? ARTBOARD_TRIGGER_OFFSET : 0);
    }
    auto place = [&] (Point point) {
      return layout.m_anchor + layout.m_normal * point.m_x +
        layout.m_along * (point.m_y - layout.m_offset);
    };
    layout.m_tip = place(tip);
    auto corners = std::array<Point, 4>({place(Point(0, 0)),
      place(Point(layout.m_width, 0)),
      place(Point(layout.m_width, layout.m_height)),
      place(Point(0, layout.m_height))});
    auto left = corners[0].m_x;
    auto top = corners[0].m_y;
    auto right = left;
    auto bottom = top;
    for(auto& corner : corners) {
      left = std::min(left, corner.m_x);
      top = std::min(top, corner.m_y);
      right = std::max(right, corner.m_x);
      bottom = std::max(bottom, corner.m_y);
    }
    layout.m_bounds = Rect(left, top, right - left, bottom - top);
    return layout;
  }

  std::pair<Side, Side> choose_sides(const ToolContext& context,
      const NodeId& owner, const Rect& trigger, const Rect& destination) {
    auto is_right = trigger.get_right() <= destination.get_left();
    auto is_below = trigger.get_bottom() <= destination.get_top();
    auto is_left = trigger.get_left() >= destination.get_right();
    auto is_above = trigger.get_top() >= destination.get_bottom();
    auto side = Side::RIGHT;
    if((is_right || is_left) && (is_below || is_above)) {
      side = is_right ? Side::RIGHT : Side::LEFT;
      auto& document = context.get_document();
      if(auto artboard = document.find(find_artboard(document, owner));
          artboard && artboard->get_artboard()) {
        auto& frame = artboard->get_artboard()->m_frame;
        auto room = is_right ? frame.get_right() - trigger.get_right() :
          trigger.get_left() - frame.get_left();
        if(is_below && frame.get_bottom() - trigger.get_bottom() < room) {
          side = Side::BOTTOM;
        } else if(is_above && trigger.get_top() - frame.get_top() < room) {
          side = Side::TOP;
        }
      }
    } else if(is_right) {
      side = Side::RIGHT;
    } else if(is_left) {
      side = Side::LEFT;
    } else if(is_below) {
      side = Side::BOTTOM;
    } else if(is_above) {
      side = Side::TOP;
    }
    return std::pair(side,
      static_cast<Side>((static_cast<int>(side) + 2) % 4));
  }

  Point evaluate(const std::array<Point, 4>& curve, double t) {
    auto u = 1 - t;
    return curve[0] * (u * u * u) + curve[1] * (3 * u * u * t) +
      curve[2] * (3 * u * t * t) + curve[3] * (t * t * t);
  }

  std::pair<Point, double> find_closest(
      const std::vector<Point>& points, Point position) {
    auto closest = points.front();
    auto distance = get_length(position - closest);
    for(auto i = std::size_t(1); i < points.size(); ++i) {
      auto start = points[i - 1];
      auto segment = points[i] - start;
      auto length = segment.m_x * segment.m_x + segment.m_y * segment.m_y;
      auto t = 0.0;
      if(length != 0) {
        t = std::clamp(((position.m_x - start.m_x) * segment.m_x +
          (position.m_y - start.m_y) * segment.m_y) / length, 0.0, 1.0);
      }
      auto point = start + segment * t;
      if(auto candidate = get_length(position - point);
          candidate < distance) {
        distance = candidate;
        closest = point;
      }
    }
    return std::pair(closest, distance);
  }

  std::optional<std::pair<Point, double>> find_along(
      const std::vector<Point>& points, Point position, double reach) {
    auto best = std::optional<std::pair<Point, double>>();
    auto best_distance = reach;
    auto travelled = 0.0;
    for(auto i = std::size_t(1); i < points.size(); ++i) {
      auto start = points[i - 1];
      auto segment = points[i] - start;
      auto length = get_length(segment);
      auto t = 0.0;
      if(length != 0) {
        t = std::clamp(((position.m_x - start.m_x) * segment.m_x +
          (position.m_y - start.m_y) * segment.m_y) / (length * length), 0.0,
          1.0);
      }
      auto point = start + segment * t;
      if(auto distance = get_length(position - point);
          distance <= best_distance) {
        best_distance = distance;
        best = std::pair(point, travelled + length * t);
      }
      travelled += length;
    }
    return best;
  }

  bool expects_destination(const Interaction& interaction) {
    auto& action = interaction.m_action;
    return std::holds_alternative<ArtboardTransition>(action) ||
      std::holds_alternative<Overlay>(action) ||
      std::holds_alternative<StateTransition>(action) ||
      std::holds_alternative<ScrollTransition>(action);
  }

  bool has_no_destination(const Interaction& interaction) {
    auto& action = interaction.m_action;
    if(auto state = std::get_if<StateTransition>(&action)) {
      return state->m_state.is_nil();
    }
    return get_wire_destination(interaction).is_nil();
  }

  bool is_pending(const Interaction& interaction) {
    auto& action = interaction.m_action;
    if(std::holds_alternative<ArtboardTransition>(action) ||
        std::holds_alternative<Overlay>(action)) {
      return true;
    }
    return std::holds_alternative<StateTransition>(action) &&
      std::holds_alternative<TapTrigger>(interaction.m_trigger);
  }

  void place_knob(WireLayout& wire, double zoom) {
    auto is_destinationless = !wire.m_target;
    if(!is_destinationless && wire.m_interactions.size() < 2) {
      return;
    }
    wire.m_knob_radius = zoom >= LARGE_KNOB_ZOOM && wire.m_is_selected ?
      LARGE_KNOB_RADIUS : KNOB_RADIUS;
    auto delta = wire.m_end - wire.m_trigger.m_anchor;
    auto is_vertical = wire.m_trigger.m_side == Side::TOP ||
      wire.m_trigger.m_side == Side::BOTTOM;
    auto minimum = wire.m_knob_radius + (is_vertical ?
      wire.m_trigger.m_bounds.m_height : wire.m_trigger.m_bounds.m_width);
    auto length = get_length(delta) - wire.m_knob_radius;
    auto side = wire.m_trigger.m_side;
    if(delta.m_x < 0 && side == Side::RIGHT) {
      side = Side::LEFT;
    } else if(delta.m_x > 0 && side == Side::LEFT) {
      side = Side::RIGHT;
    }
    auto reach = std::max(
      std::min(length, KNOB_OFFSET + wire.m_knob_radius), minimum);
    if(is_destinationless) {
      reach = TAIL_KNOB_DISTANCE;
      side = Side::RIGHT;
    }
    auto sign = [] (double value) {
      return static_cast<double>((value > 0) - (value < 0));
    };
    auto first = Point(0, 0);
    auto second = Point(0, 0);
    if(side == Side::TOP) {
      first = Point(0, -reach);
      second = Point(sign(delta.m_x) * reach, 0);
    } else if(side == Side::RIGHT) {
      first = Point(reach, 0);
      second = Point(0, sign(delta.m_y) * reach);
    } else if(side == Side::BOTTOM) {
      first = Point(0, reach);
      second = Point(sign(delta.m_x) * reach, 0);
    } else {
      first = Point(-reach, 0);
      second = Point(0, sign(delta.m_y) * reach);
    }
    auto candidates = std::array<std::optional<std::pair<Point, double>>, 2>(
      {find_along(wire.m_points, wire.m_trigger.m_anchor + first, reach),
        find_along(wire.m_points, wire.m_trigger.m_anchor + second, reach)});
    auto chosen = candidates[0];
    if(!chosen || (candidates[1] && candidates[1]->second > chosen->second)) {
      chosen = candidates[1];
    }
    if(!chosen) {
      return;
    }
    wire.m_knob = Point(std::round(chosen->first.m_x),
      std::round(chosen->first.m_y));
    if(wire.m_knob_radius >= LARGE_KNOB_RADIUS && is_destinationless &&
        wire.m_interactions.size() == 1) {
      auto& interaction = *wire.m_interactions.front();
      if(expects_destination(interaction) &&
          has_no_destination(interaction)) {
        wire.m_icon = KnobIcon::DIRECTION;
      } else if(std::holds_alternative<PreviousArtboard>(
          interaction.m_action)) {
        wire.m_icon = KnobIcon::PREVIOUS;
      } else {
        wire.m_icon = KnobIcon::MULTIPLE;
      }
    }
  }

  void finish_wire(WireLayout& wire, double zoom) {
    auto start = wire.m_trigger.m_tip;
    auto end = wire.m_end;
    auto is_vertical = wire.m_trigger.m_side == Side::TOP ||
      wire.m_trigger.m_side == Side::BOTTOM;
    auto span = is_vertical ? end.m_y - start.m_y : end.m_x - start.m_x;
    auto softening = 0.3 *
      std::min(100.0, std::max(0.0, std::abs(span) - BEZIER_MINIMUM)) / 100;
    auto reach = span * (BEZIER_SCALE - softening);
    if(is_vertical) {
      wire.m_curve = {start, Point(start.m_x, start.m_y + reach),
        Point(end.m_x, end.m_y - reach), end};
    } else {
      wire.m_curve = {start, Point(start.m_x + reach, start.m_y),
        Point(end.m_x - reach, end.m_y), end};
    }
    wire.m_points.clear();
    for(auto i = 0; i <= WIRE_SAMPLES; ++i) {
      wire.m_points.push_back(
        evaluate(wire.m_curve, static_cast<double>(i) / WIRE_SAMPLES));
    }
    wire.m_is_pending = wire.m_is_selected && !wire.m_target &&
      wire.m_interactions.size() == 1 &&
      is_pending(*wire.m_interactions.front());
    place_knob(wire, zoom);
  }

  std::vector<WireLayout> layout_wires(const ToolContext& context) {
    auto wires = std::vector<WireLayout>();
    auto& document = context.get_document();
    auto& selection = context.get_editor().get_selection();
    auto& nodes = selection.get_nodes();
    if(nodes.empty()) {
      return wires;
    }
    auto shown = std::vector<const Interaction*>();
    auto seen = std::unordered_set<const Interaction*>();
    auto add = [&] (const Interaction* interaction) {
      if(seen.insert(interaction).second) {
        shown.push_back(interaction);
      }
    };
    auto containers = std::vector<NodeId>();
    for(auto& id : nodes) {
      auto container = find_top_level(document, id);
      if(!container.is_nil() &&
          std::ranges::find(containers, container) == containers.end()) {
        containers.push_back(container);
      }
    }
    for(auto& container : containers) {
      auto pending = std::vector<NodeId>({container});
      while(!pending.empty()) {
        auto id = pending.back();
        pending.pop_back();
        auto node = document.find(id);
        if(!node || !node->m_header.m_is_visible) {
          continue;
        }
        for(auto interaction : get_interactions(document, id)) {
          add(interaction);
        }
        auto& children = document.get_children(id);
        pending.insert(pending.end(), children.rbegin(), children.rend());
      }
    }
    if(is_artboard(document, nodes.front())) {
      for(auto& interaction : document.get_prototype().m_interactions) {
        auto destination = get_wire_destination(interaction);
        if(!destination.is_nil() &&
            std::ranges::find(nodes, destination) != nodes.end() &&
            is_shown(document, interaction.m_owner)) {
          add(&interaction);
        }
      }
    }
    auto selected = find_selected(context);
    auto batches = std::vector<std::pair<const Interaction*, bool>>();
    for(auto interaction : selected) {
      batches.emplace_back(interaction, true);
    }
    for(auto interaction : shown) {
      if(std::ranges::find(selected, interaction) == selected.end()) {
        batches.emplace_back(interaction, false);
      }
    }
    auto sole = nodes.size() == 1 ? nodes.front() : NodeId();
    for(auto& [interaction, is_selected] : batches) {
      auto& owner = interaction->m_owner;
      auto destination = get_wire_destination(*interaction);
      if(!destination.is_nil() && !document.find(destination)) {
        destination = NodeId();
      }
      auto existing = std::ranges::find_if(wires, [&] (const auto& wire) {
        return wire.m_owner == owner && wire.m_destination == destination;
      });
      if(existing != wires.end()) {
        existing->m_is_selected = existing->m_is_selected || is_selected;
        existing->m_is_overlay = existing->m_is_overlay &&
          std::holds_alternative<Overlay>(interaction->m_action);
        existing->m_interactions.push_back(interaction);
        continue;
      }
      auto trigger_frame = get_frame(context, owner);
      if(!trigger_frame) {
        continue;
      }
      auto is_trigger_selected = !is_selected && selection.contains(owner);
      auto is_multiple =
        (is_selected || is_trigger_selected) && sole == owner;
      auto is_artboard_trigger = is_artboard(document, owner);
      auto wire = WireLayout();
      wire.m_owner = owner;
      wire.m_destination = destination;
      wire.m_interactions.push_back(interaction);
      wire.m_is_selected = is_selected;
      wire.m_is_trigger_selected = is_trigger_selected;
      wire.m_is_overlay =
        std::holds_alternative<Overlay>(interaction->m_action);
      auto trigger_bounds = get_global_bounds(context, owner);
      auto destination_bounds = std::optional<Rect>();
      auto destination_frame = std::optional<SelectionBox>();
      if(!destination.is_nil()) {
        destination_bounds = get_global_bounds(context, destination);
        destination_frame = get_frame(context, destination);
      }
      if(trigger_bounds && destination_bounds && destination_frame) {
        auto sides = choose_sides(
          context, owner, *trigger_bounds, *destination_bounds);
        wire.m_trigger = layout_handle(context, *trigger_frame, sides.first,
          is_artboard_trigger, is_multiple && sides.first == Side::RIGHT);
        wire.m_target = layout_handle(
          context, *destination_frame, sides.second, false, false);
        wire.m_end = wire.m_target->m_tip;
      } else {
        wire.m_destination = NodeId();
        wire.m_trigger = layout_handle(context, *trigger_frame, Side::RIGHT,
          is_artboard_trigger, is_multiple);
        wire.m_end = wire.m_trigger.m_anchor + (context.get_zoom() <
          SMALL_TAIL_ZOOM ? SMALL_TAIL_OFFSET : TAIL_OFFSET);
        existing = std::ranges::find_if(wires, [&] (const auto& other) {
          return other.m_owner == owner && other.m_destination.is_nil();
        });
        if(existing != wires.end()) {
          existing->m_is_selected = existing->m_is_selected || is_selected;
          existing->m_is_overlay = existing->m_is_overlay &&
            std::holds_alternative<Overlay>(interaction->m_action);
          existing->m_interactions.push_back(interaction);
          continue;
        }
      }
      wires.push_back(std::move(wire));
    }
    std::ranges::stable_sort(wires, [] (const auto& left, const auto& right) {
      if(left.m_is_selected != right.m_is_selected) {
        return !left.m_is_selected;
      }
      return !left.m_is_trigger_selected && right.m_is_trigger_selected;
    });
    for(auto& wire : wires) {
      finish_wire(wire, context.get_zoom());
    }
    return wires;
  }

  std::optional<WireHit> find_wire(
      const std::vector<WireLayout>& wires, Point position) {
    for(auto i = wires.size(); i-- != 0;) {
      auto& wire = wires[i];
      auto closest = find_closest(wire.m_points, position);
      if(closest.second <= get_width(wire) / 2 + PrototypeTool::WIRE_SLOP ||
          wire.m_knob && get_length(position - *wire.m_knob) <=
            wire.m_knob_radius + PrototypeTool::WIRE_SLOP) {
        return WireHit(i, closest.first, closest.second);
      }
    }
    return std::nullopt;
  }

  std::string make_key(const char* kind, std::initializer_list<double> values) {
    auto key = std::string(kind);
    for(auto value : values) {
      key += '|';
      key += std::to_string(std::round(value * 64) / 64);
    }
    return key;
  }

  std::shared_ptr<const QImage> cache_image(ImageCache& images,
      const std::string& key, const std::function<QImage ()>& paint) {
    if(auto image = images.find(key); image != images.end()) {
      return image->second;
    }
    if(images.size() > IMAGE_LIMIT) {
      images.clear();
    }
    auto image = std::make_shared<const QImage>(paint());
    images.emplace(key, image);
    return image;
  }

  std::pair<std::shared_ptr<const QImage>, Point> get_wire_image(
      ImageCache& images, const std::array<Point, 4>& curve, double width,
      const QColor& color, bool is_dashed, double scale) {
    auto left = curve[0].m_x;
    auto top = curve[0].m_y;
    auto right = left;
    auto bottom = top;
    for(auto& point : curve) {
      left = std::min(left, point.m_x);
      top = std::min(top, point.m_y);
      right = std::max(right, point.m_x);
      bottom = std::max(bottom, point.m_y);
    }
    auto margin = width / 2 + 1;
    auto origin = Point(std::floor((left - margin) * scale) / scale,
      std::floor((top - margin) * scale) / scale);
    auto local = curve;
    for(auto& point : local) {
      point = point - origin;
    }
    auto key = make_key(is_dashed ? "dashed_wire" : "wire", {local[0].m_x,
      local[0].m_y, local[1].m_x, local[1].m_y, local[2].m_x, local[2].m_y,
      local[3].m_x, local[3].m_y, width, static_cast<double>(color.rgba()),
      scale});
    auto image = cache_image(images, key, [&] {
      auto image = QImage(
        std::max(1, static_cast<int>(std::ceil((right - origin.m_x + margin) *
          scale))),
        std::max(1, static_cast<int>(std::ceil((bottom - origin.m_y + margin) *
          scale))), QImage::Format_RGBA8888_Premultiplied);
      image.fill(Qt::transparent);
      auto painter = QPainter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.scale(scale, scale);
      auto path = QPainterPath(QPointF(local[0].m_x, local[0].m_y));
      path.cubicTo(QPointF(local[1].m_x, local[1].m_y),
        QPointF(local[2].m_x, local[2].m_y),
        QPointF(local[3].m_x, local[3].m_y));
      auto pen = QPen(color, width);
      if(is_dashed) {
        pen.setCapStyle(Qt::RoundCap);
        pen.setDashPattern(
          {OVERLAY_DASH / width, OVERLAY_DASH / width});
      } else {
        pen.setCapStyle(Qt::FlatCap);
      }
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(path);
      painter.end();
      return image;
    });
    return std::pair(image, origin);
  }

  std::shared_ptr<const QImage> get_knob_image(ImageCache& images,
      double radius, const QColor& color, const std::string& content,
      KnobIcon icon, double scale) {
    auto key = "knob|" + std::to_string(radius) + "|" +
      std::to_string(color.rgba()) + "|" + content + "|" +
      std::to_string(static_cast<int>(icon)) + "|" + std::to_string(scale);
    return cache_image(images, key, [&] {
      auto size = std::max(1, static_cast<int>(std::ceil(2 * radius * scale)));
      auto image = QImage(size, size, QImage::Format_RGBA8888_Premultiplied);
      image.fill(Qt::transparent);
      auto painter = QPainter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.scale(scale, scale);
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawEllipse(QRectF(0, 0, 2 * radius, 2 * radius));
      if(!content.empty()) {
        auto font = QFont();
        font.setPixelSize(static_cast<int>(KNOB_FONT_SIZE));
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(0, 0, 2 * radius, 2 * radius), Qt::AlignCenter,
          QString::fromStdString(content));
      } else if(icon != KnobIcon::NONE) {
        auto center = QPointF(radius, radius);
        auto pen = QPen(Qt::white, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        if(icon == KnobIcon::MULTIPLE) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(Qt::white);
          for(auto offset : {-3.5, 0.0, 3.5}) {
            painter.drawEllipse(center + QPointF(offset, 0), 1.2, 1.2);
          }
        } else if(icon == KnobIcon::PREVIOUS) {
          auto arrow = QPainterPath(center + QPointF(-3, -2.5));
          arrow.lineTo(center + QPointF(0.5, -2.5));
          arrow.arcTo(QRectF(center + QPointF(-2, -2.5), QSizeF(5, 5)), 90,
            -180);
          arrow.lineTo(center + QPointF(-2, 2.5));
          painter.drawPath(arrow);
          auto head = QPainterPath(center + QPointF(-1.2, -4.3));
          head.lineTo(center + QPointF(-3, -2.5));
          head.lineTo(center + QPointF(-1.2, -0.7));
          painter.drawPath(head);
        } else {
          auto direction = icon == KnobIcon::REVERSE ? -1.0 : 1.0;
          auto chevron =
            QPainterPath(center + QPointF(-1.5 * direction, -4));
          chevron.lineTo(center + QPointF(2.5 * direction, 0));
          chevron.lineTo(center + QPointF(-1.5 * direction, 4));
          painter.drawPath(chevron);
        }
      }
      painter.end();
      return image;
    });
  }

  std::pair<std::shared_ptr<const QImage>, Point> get_tab_image(
      ImageCache& images, const HandleLayout& layout, const QColor& color,
      bool is_destination, double scale) {
    auto origin = Point(std::floor(layout.m_bounds.m_x * scale) / scale,
      std::floor(layout.m_bounds.m_y * scale) / scale);
    auto anchor = layout.m_anchor - origin;
    auto key = make_key(is_destination ? "target_tab" : "trigger_tab",
      {anchor.m_x, anchor.m_y, layout.m_normal.m_x, layout.m_normal.m_y,
        layout.m_offset, layout.m_width, layout.m_height,
        static_cast<double>(color.rgba()), scale});
    auto image = cache_image(images, key, [&] {
      auto image = QImage(
        std::max(1, static_cast<int>(std::ceil(
          (layout.m_bounds.get_right() - origin.m_x) * scale))),
        std::max(1, static_cast<int>(std::ceil(
          (layout.m_bounds.get_bottom() - origin.m_y) * scale))),
        QImage::Format_RGBA8888_Premultiplied);
      image.fill(Qt::transparent);
      auto painter = QPainter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.scale(scale, scale);
      painter.setTransform(QTransform(layout.m_normal.m_x, layout.m_normal.m_y,
        layout.m_along.m_x, layout.m_along.m_y,
        anchor.m_x - layout.m_along.m_x * layout.m_offset,
        anchor.m_y - layout.m_along.m_y * layout.m_offset), true);
      auto radius = layout.m_height / 2;
      auto shape = QPainterPath();
      shape.moveTo(0, 0);
      shape.lineTo(layout.m_width - radius, 0);
      shape.arcTo(QRectF(layout.m_width - 2 * radius, 0, 2 * radius,
        2 * radius), 90, -180);
      shape.lineTo(0, layout.m_height);
      shape.closeSubpath();
      painter.fillPath(shape, color);
      if(is_destination) {
        auto pen = QPen(Qt::white, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        auto middle = layout.m_height / 2;
        auto chevron = QPainterPath(QPointF(8.5, middle - 3.5));
        chevron.lineTo(QPointF(5, middle));
        chevron.lineTo(QPointF(8.5, middle + 3.5));
        painter.drawPath(chevron);
      }
      painter.end();
      return image;
    });
    return std::pair(image, origin);
  }

  double get_label_width(const std::string& name) {
    auto font = QFont();
    font.setPixelSize(static_cast<int>(LABEL_FONT_SIZE));
    return QFontMetricsF(font).horizontalAdvance(
      QString::fromStdString(name)) + 2 * LABEL_PADDING;
  }

  std::vector<NodeId> find_reachable(
      const Document& document, const NodeId& artboard) {
    auto reached = std::vector<NodeId>();
    auto pending = std::vector<NodeId>({artboard});
    while(!pending.empty()) {
      auto current = pending.front();
      pending.erase(pending.begin());
      if(std::ranges::find(reached, current) != reached.end()) {
        continue;
      }
      reached.push_back(current);
      for(auto& interaction : document.get_prototype().m_interactions) {
        if(find_artboard(document, interaction.m_owner) != current ||
            !is_shown(document, interaction.m_owner)) {
          continue;
        }
        auto destination = NodeId();
        if(auto transition =
            std::get_if<ArtboardTransition>(&interaction.m_action)) {
          destination = transition->m_destination;
        } else if(auto overlay =
            std::get_if<Overlay>(&interaction.m_action)) {
          destination = overlay->m_destination;
        }
        if(is_artboard(document, destination)) {
          pending.push_back(destination);
        }
      }
    }
    std::ranges::reverse(reached);
    return reached;
  }

  std::shared_ptr<const QImage> get_tip_image(
      ImageCache& images, const std::string& text, double scale) {
    auto key = "tip|" + text + "|" + std::to_string(scale);
    return cache_image(images, key, [&] {
      auto font = QFont();
      font.setPixelSize(static_cast<int>(TIP_FONT_SIZE));
      auto label = QString::fromStdString(text);
      auto width =
        QFontMetricsF(font).horizontalAdvance(label) + 2 * TIP_PADDING;
      auto image = QImage(
        std::max(1, static_cast<int>(std::ceil(width * scale))),
        std::max(1, static_cast<int>(std::ceil(TIP_HEIGHT * scale))),
        QImage::Format_RGBA8888_Premultiplied);
      image.fill(Qt::transparent);
      auto painter = QPainter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.scale(scale, scale);
      painter.setPen(Qt::NoPen);
      painter.setBrush(TIP_BACKGROUND);
      painter.drawRoundedRect(
        QRectF(0, 0, width, TIP_HEIGHT), TIP_RADIUS, TIP_RADIUS);
      painter.setFont(font);
      painter.setPen(Qt::white);
      painter.drawText(QPointF(TIP_PADDING, TIP_BASELINE), label);
      painter.end();
      return image;
    });
  }

  void add_tint(const ToolContext& context, const SelectionBox& frame,
      ImageCache& images, CanvasOverlay& overlay) {
    auto scale = context.get_scale();
    auto corners = std::array<Point, 4>();
    auto rect = frame.m_rect;
    auto points = std::array<Point, 4>({rect.get_top_left(),
      Point(rect.get_right(), rect.get_top()), rect.get_bottom_right(),
      Point(rect.get_left(), rect.get_bottom())});
    for(auto i = std::size_t(0); i != points.size(); ++i) {
      corners[i] = context.to_view(frame.m_transform.map(points[i]));
    }
    auto left = corners[0].m_x;
    auto top = corners[0].m_y;
    auto right = left;
    auto bottom = top;
    for(auto& corner : corners) {
      left = std::min(left, corner.m_x);
      top = std::min(top, corner.m_y);
      right = std::max(right, corner.m_x);
      bottom = std::max(bottom, corner.m_y);
    }
    if(get_rotation(frame.m_transform) == 0) {
      auto image = cache_image(images, "tint", [] {
        auto image = QImage(1, 1, QImage::Format_RGBA8888_Premultiplied);
        image.fill(SELECT_FILL);
        return image;
      });
      overlay.m_images.push_back(OverlayImage(
        image, Point(left, top), Size(right - left, bottom - top)));
      return;
    }
    auto origin = Point(std::floor(left * scale) / scale,
      std::floor(top * scale) / scale);
    auto key = make_key("rotated_tint", {corners[0].m_x - origin.m_x,
      corners[0].m_y - origin.m_y, corners[1].m_x - origin.m_x,
      corners[1].m_y - origin.m_y, corners[2].m_x - origin.m_x,
      corners[2].m_y - origin.m_y, corners[3].m_x - origin.m_x,
      corners[3].m_y - origin.m_y, scale});
    auto image = cache_image(images, key, [&] {
      auto image = QImage(
        std::max(1, static_cast<int>(std::ceil((right - origin.m_x) * scale))),
        std::max(1, static_cast<int>(std::ceil((bottom - origin.m_y) * scale))),
        QImage::Format_RGBA8888_Premultiplied);
      image.fill(Qt::transparent);
      auto painter = QPainter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.scale(scale, scale);
      auto polygon = QPolygonF();
      for(auto& corner : corners) {
        polygon << QPointF(corner.m_x - origin.m_x, corner.m_y - origin.m_y);
      }
      painter.setPen(Qt::NoPen);
      painter.setBrush(SELECT_FILL);
      painter.drawPolygon(polygon);
      painter.end();
      return image;
    });
    overlay.m_images.push_back(OverlayImage(image, origin));
  }

  void draw_wires(const ToolContext& context,
      const std::vector<WireLayout>& wires, const WireHover& hover,
      ImageCache& images, CanvasOverlay& overlay) {
    auto scale = context.get_scale();
    auto& selection = context.get_editor().get_selection();
    auto sole = selection.get_size() == 1 ? selection.get_primary() : NodeId();
    auto tabs = std::vector<WireTab>();
    auto add_tab = [&] (const NodeId& node, const HandleLayout& layout,
        bool is_destination, bool is_selected, bool is_trigger_selected) {
      auto existing = std::ranges::find_if(tabs, [&] (const auto& tab) {
        return tab.m_node == node && tab.m_layout.m_side == layout.m_side &&
          tab.m_is_destination == is_destination;
      });
      if(existing != tabs.end()) {
        existing->m_is_selected = existing->m_is_selected || is_selected;
        existing->m_is_trigger_selected =
          existing->m_is_trigger_selected || is_trigger_selected;
        return;
      }
      tabs.push_back(WireTab(
        node, layout, is_destination, is_selected, is_trigger_selected));
    };
    for(auto& wire : wires) {
      add_tab(wire.m_owner, wire.m_trigger, false, wire.m_is_selected,
        wire.m_is_trigger_selected);
      if(wire.m_target) {
        add_tab(wire.m_destination, *wire.m_target, true, wire.m_is_selected,
          wire.m_is_trigger_selected);
      }
    }
    auto draw_tabs = [&] (bool is_selected) {
      for(auto& tab : tabs) {
        if(tab.m_is_selected != is_selected || !tab.m_is_destination &&
            tab.m_node == sole && tab.m_layout.m_side == Side::RIGHT) {
          continue;
        }
        auto color =
          get_wire_color(tab.m_is_selected, tab.m_is_trigger_selected);
        auto image = get_tab_image(
          images, tab.m_layout, color, tab.m_is_destination, scale);
        overlay.m_images.push_back(OverlayImage(image.first, image.second));
      }
    };
    auto draw_group = [&] (bool is_selected) {
      for(auto& wire : wires) {
        if(wire.m_is_selected != is_selected) {
          continue;
        }
        auto color =
          get_wire_color(wire.m_is_selected, wire.m_is_trigger_selected);
        auto image = get_wire_image(images, wire.m_curve, get_width(wire),
          color, wire.m_is_overlay, scale);
        overlay.m_images.push_back(OverlayImage(image.first, image.second));
        if(hover.m_is_shown && wire.m_owner == hover.m_owner &&
            wire.m_destination == hover.m_destination) {
          auto knob = get_knob_image(
            images, KNOB_RADIUS, color, std::string(), KnobIcon::NONE, scale);
          overlay.m_images.push_back(OverlayImage(knob,
            hover.m_point - Point(KNOB_RADIUS, KNOB_RADIUS)));
        }
        if(wire.m_knob) {
          auto content = std::string();
          if(wire.m_knob_radius >= LARGE_KNOB_RADIUS &&
              (wire.m_target || wire.m_interactions.size() > 1)) {
            content = std::to_string(wire.m_interactions.size());
          }
          auto knob = get_knob_image(
            images, wire.m_knob_radius, color, content, wire.m_icon, scale);
          overlay.m_images.push_back(OverlayImage(knob,
            *wire.m_knob - Point(wire.m_knob_radius, wire.m_knob_radius)));
        }
      }
    };
    draw_tabs(false);
    draw_group(false);
    draw_tabs(true);
    draw_group(true);
  }
}

PrototypeTool::PrototypeTool()
  : m_press(0, 0),
    m_is_dragged(false),
    m_initial_offset(0),
    m_offset_start(0),
    m_is_offset_applied(false),
    m_is_over_offset(false),
    m_hover_point(0, 0),
    m_pointer(0, 0),
    m_is_wire_hovered(false),
    m_is_knob_shown(false),
    m_is_wire_pressed(false) {}

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
  auto zoom = context.get_zoom();
  auto size = SMALL_MARKER_SIZE;
  if(zoom >= LARGE_MARKER_ZOOM) {
    size = LARGE_MARKER_SIZE;
  } else if(zoom >= MEDIUM_MARKER_ZOOM) {
    size = MEDIUM_MARKER_SIZE;
  }
  return Rect(frame.m_x - size.m_width - ARTBOARD_STROKE,
    frame.m_y - ARTBOARD_STROKE, size.m_width, size.m_height);
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

std::vector<WireShape> PrototypeTool::get_wires(
    const ToolContext& context) const {
  auto shapes = std::vector<WireShape>();
  for(auto& wire : layout_wires(context)) {
    auto shape = WireShape();
    shape.m_owner = wire.m_owner;
    shape.m_destination = wire.m_destination;
    for(auto interaction : wire.m_interactions) {
      shape.m_interactions.push_back(interaction->m_id);
    }
    shape.m_is_selected = wire.m_is_selected;
    shape.m_start = wire.m_curve[0];
    shape.m_end = wire.m_curve[3];
    shape.m_middle = evaluate(wire.m_curve, 0.5);
    shape.m_knob = wire.m_knob;
    shapes.push_back(std::move(shape));
  }
  return shapes;
}

std::optional<Point> PrototypeTool::get_hover_knob() const {
  if(!m_is_knob_shown) {
    return std::nullopt;
  }
  return Point(std::round(m_hover_point.m_x), std::round(m_hover_point.m_y));
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
      m_press = event.m_position;
      m_is_dragged = false;
      m_target = NodeId();
      return;
    }
  }
  auto wires = std::vector<WireLayout>();
  if(!is_near_handle(context, event.m_position)) {
    wires = layout_wires(context);
  }
  if(auto hit = find_wire(wires, event.m_position)) {
    auto& wire = wires[hit->m_index];
    m_is_knob_shown = false;
    m_is_wire_pressed = true;
    m_is_wire_hovered = true;
    m_hover_owner = wire.m_owner;
    m_hover_destination = wire.m_destination;
    auto interactions = wire.m_interactions;
    sort_by_trigger(interactions);
    if(is_shift(event.m_modifiers) && !wire.m_is_pending) {
      if(selection.is_empty()) {
        return;
      }
      auto ids = std::vector<InteractionId>();
      for(auto interaction : find_selected(context)) {
        if(std::ranges::find(wire.m_interactions, interaction) ==
            wire.m_interactions.end()) {
          ids.push_back(interaction->m_id);
        }
      }
      if(ids.size() == find_selected(context).size()) {
        ids.push_back(interactions.front()->m_id);
      }
      edit.set_interactions(selection.get_primary(), std::move(ids));
      return;
    }
    if(wire.m_is_pending ||
        (wire.m_is_selected && !wire.m_destination.is_nil())) {
      m_dragged.clear();
      for(auto interaction : interactions) {
        if(wire.m_is_pending || std::ranges::contains(
            find_selected(context), interaction)) {
          m_dragged.push_back(interaction->m_id);
        }
      }
      m_press = event.m_position;
      m_is_dragged = false;
      m_target = NodeId();
      return;
    }
    if(wire.m_is_selected) {
      return;
    }
    auto owner = document.find(wire.m_owner);
    if(owner && (owner->m_parent.is_nil() ||
        is_artboard(document, owner->m_parent) ||
        !edit.enter(owner->m_parent))) {
      while(!edit.get_container().is_nil()) {
        edit.exit();
      }
    }
    selection.select(wire.m_owner);
    edit.set_interaction(interactions.front()->m_id);
    return;
  }
  for(auto& flow : document.get_prototype().m_flows) {
    if(auto label = get_flow_label(context, flow.m_artboard);
        label && label->contains(event.m_position)) {
      focus_flow(context, flow.m_artboard);
      return;
    }
  }
  if(auto artboard = find_marker(context, event.m_position);
      !artboard.is_nil()) {
    if(find_flow(document, artboard)) {
      editor.execute(std::make_unique<RemoveFlowCommand>(artboard));
    } else {
      editor.execute(
        std::make_unique<AddFlowCommand>(artboard, std::string()));
      if(find_flow(document, artboard)) {
        focus_flow(context, artboard);
      }
    }
    return;
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
    edit.set_interactions(NodeId(), {});
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
  if(!m_source.is_nil() || !m_dragged.empty()) {
    if(!m_is_dragged && std::hypot(event.m_position.m_x - m_press.m_x,
        event.m_position.m_y - m_press.m_y) < DRAG_THRESHOLD) {
      return;
    }
    m_is_dragged = true;
    m_wire_end = event.m_position;
    m_target = find_wire_target(context, event.m_position);
    return;
  }
  m_pointer = event.m_position;
  m_hover_marker = find_marker(context, event.m_position);
  auto& edit = context.get_editor().get_context();
  auto hit = std::optional<WireHit>();
  auto wires = std::vector<WireLayout>();
  if(!is_near_handle(context, event.m_position)) {
    wires = layout_wires(context);
    hit = find_wire(wires, event.m_position);
  }
  auto is_changed = !hit != !m_is_wire_hovered || (hit &&
    (wires[hit->m_index].m_owner != m_hover_owner ||
      wires[hit->m_index].m_destination != m_hover_destination));
  auto is_visible =
    hit && !m_is_wire_pressed && hit->m_distance <= WIRE_SLOP;
  if(m_is_wire_hovered && is_changed) {
    m_is_knob_shown = false;
    m_is_wire_pressed = false;
  }
  m_is_wire_hovered = static_cast<bool>(hit);
  if(hit) {
    m_hover_owner = wires[hit->m_index].m_owner;
    m_hover_destination = wires[hit->m_index].m_destination;
    if(is_visible) {
      m_hover_point = hit->m_point;
      m_is_knob_shown = true;
      m_tool_tip = summarize_wire(wires[hit->m_index].m_interactions);
    }
    edit.set_hover(NodeId());
    return;
  }
  m_hover_owner = NodeId();
  m_hover_destination = NodeId();
  m_tool_tip.clear();
  auto hover = context.get_titles().find(event.m_position);
  if(hover.is_nil()) {
    auto node = context.get_hit_tester().hit(context.to_document(
      event.m_position), HIT_TOLERANCE / context.get_zoom());
    if(!node.is_nil() && !is_artboard(context.get_document(), node)) {
      hover = edit.resolve(node);
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
  if(!m_source.is_nil()) {
    if(context.get_document().find(m_source)) {
      if(!m_is_dragged) {
        create_wire(context, is_shift(event.m_modifiers));
      } else if(auto destination =
          find_wire_target(context, event.m_position); !destination.is_nil()) {
        connect(context, destination, is_shift(event.m_modifiers));
      }
    }
    cancel(context);
    return;
  }
  if(!m_dragged.empty()) {
    if(m_is_dragged) {
      rewire(context, find_wire_target(context, event.m_position),
        is_shift(event.m_modifiers));
    } else {
      select_wire(context, m_dragged, is_shift(event.m_modifiers));
    }
    cancel(context);
  }
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
  m_hover_marker = NodeId();
  m_is_wire_hovered = false;
  m_is_knob_shown = false;
  m_is_wire_pressed = false;
  m_hover_owner = NodeId();
  m_hover_destination = NodeId();
  m_tool_tip.clear();
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
  m_dragged.clear();
  m_is_dragged = false;
  m_wire_end.reset();
  m_target = NodeId();
}

bool PrototypeTool::is_busy() const {
  return !m_source.is_nil() || !m_dragged.empty() ||
    !m_offset_interaction.is_nil();
}

ToolCursor PrototypeTool::get_cursor() const {
  if(!m_offset_interaction.is_nil() || m_is_over_offset) {
    return ToolCursor(CursorKind::RESIZE, std::numbers::pi / 2);
  }
  return ToolCursor();
}

std::string PrototypeTool::get_tool_tip() const {
  if(!m_is_knob_shown) {
    return std::string();
  }
  return m_tool_tip;
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
  auto marked = get_marked_artboards(context);
  for(auto& title : context.get_titles().get_titles()) {
    auto flow = find_flow(document, title.m_artboard);
    if(!flow && std::ranges::find(marked, title.m_artboard) == marked.end()) {
      continue;
    }
    auto marker = get_marker(context, title.m_artboard);
    if(!marker) {
      continue;
    }
    overlay.m_images.push_back(OverlayImage(get_marker_image(
      marker->get_size(), static_cast<bool>(flow),
      title.m_artboard == m_hover_marker, scale), marker->get_top_left()));
    if(auto label = get_flow_label(context, title.m_artboard)) {
      overlay.m_images.push_back(OverlayImage(get_label_image(flow->m_name,
        is_focused_flow(context, title.m_artboard), scale),
        label->get_top_left()));
    }
  }
  if(find_selected(context).empty()) {
    for(auto& id : selection.get_nodes()) {
      if(auto frame = get_frame(context, id)) {
        add_tint(context, *frame, m_images, overlay);
      }
    }
  }
  auto wires = layout_wires(context);
  if(m_is_dragged) {
    std::erase_if(wires, [&] (const auto& wire) {
      return std::ranges::any_of(wire.m_interactions, [&] (auto interaction) {
        return std::ranges::contains(m_dragged, interaction->m_id);
      });
    });
  }
  auto wire_hover = WireHover();
  if(auto knob = get_hover_knob()) {
    wire_hover = WireHover(true, m_hover_owner, m_hover_destination, *knob);
  }
  draw_wires(context, wires, wire_hover, m_images, overlay);
  if(m_is_dragged && m_wire_end) {
    draw_drag(context, overlay);
  }
  if(auto tip = get_tool_tip(); !tip.empty()) {
    overlay.m_images.push_back(OverlayImage(
      get_tip_image(m_images, tip, scale), m_pointer + TIP_OFFSET));
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
    auto circle = OverlayShape();
    circle.m_center = *handle;
    circle.m_size = Size(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS);
    circle.m_radius = HANDLE_RADIUS;
    circle.m_fill = HANDLE_COLOR;
    overlay.m_shapes.push_back(circle);
    auto is_drawing = !m_source.is_nil() && m_is_dragged;
    if(!is_drawing && get_interactions(document, get_wired(context)).empty()) {
      auto tip = Point(handle->m_x + 2.5, handle->m_y);
      add_line(overlay, Point(handle->m_x - 1.5, handle->m_y - 4), tip, 1.5,
        WHITE);
      add_line(overlay, Point(handle->m_x - 1.5, handle->m_y + 4), tip, 1.5,
        WHITE);
    } else if(!is_drawing) {
      overlay.m_shapes.push_back(make_overlay_line(
        *handle - Point(PLUS_SIZE / 2, 0), *handle + Point(PLUS_SIZE / 2, 0),
        PLUS_WIDTH, WHITE));
      overlay.m_shapes.push_back(make_overlay_line(
        *handle - Point(0, PLUS_SIZE / 2), *handle + Point(0, PLUS_SIZE / 2),
        PLUS_WIDTH, WHITE));
    }
  }
}

void PrototypeTool::draw_drag(
    const ToolContext& context, CanvasOverlay& overlay) const {
  auto& document = context.get_document();
  auto scale = context.get_scale();
  auto owner = m_source;
  auto destination = NodeId();
  auto is_overlay = false;
  auto is_multiple = true;
  if(owner.is_nil()) {
    is_overlay = true;
    for(auto& id : m_dragged) {
      auto interaction = find_edited(context, id);
      if(!interaction) {
        continue;
      }
      if(owner.is_nil()) {
        owner = interaction->m_owner;
        destination = get_wire_destination(*interaction);
      }
      is_overlay = is_overlay &&
        std::holds_alternative<Overlay>(interaction->m_action);
    }
    is_multiple = m_dragged.size() <= 1;
  }
  auto frame = get_frame(context, owner);
  if(!frame) {
    return;
  }
  auto side = Side::RIGHT;
  if(!destination.is_nil()) {
    auto trigger_bounds = get_global_bounds(context, owner);
    auto destination_bounds = get_global_bounds(context, destination);
    if(trigger_bounds && destination_bounds) {
      side = choose_sides(
        context, owner, *trigger_bounds, *destination_bounds).first;
    }
  }
  auto wire = WireLayout();
  wire.m_owner = owner;
  wire.m_trigger = layout_handle(context, *frame, side,
    is_artboard(document, owner), is_multiple && side == Side::RIGHT);
  wire.m_end = *m_wire_end;
  wire.m_is_selected = true;
  finish_wire(wire, context.get_zoom());
  auto image = get_wire_image(m_images, wire.m_curve, SELECTED_WIRE_WIDTH,
    SELECTED_WIRE, is_overlay, scale);
  overlay.m_images.push_back(OverlayImage(image.first, image.second));
  auto icon = KnobIcon::DIRECTION;
  if(m_wire_end->m_x < wire.m_trigger.m_bounds.m_x) {
    icon = KnobIcon::REVERSE;
  }
  overlay.m_images.push_back(OverlayImage(get_knob_image(m_images,
    WIRING_DOT_RADIUS, SELECTED_WIRE, std::string(), icon, scale),
    *m_wire_end - Point(WIRING_DOT_RADIUS, WIRING_DOT_RADIUS)));
  if(auto bounds = get_global_bounds(context, m_target)) {
    auto view = context.get_view().map(*bounds);
    add_outline(overlay, std::array<Point, 4>({view.get_top_left(),
      Point(view.get_right(), view.get_top()), view.get_bottom_right(),
      Point(view.get_left(), view.get_bottom())}), TARGET_WIDTH, WIRE_COLOR);
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

bool PrototypeTool::is_near_handle(
    const ToolContext& context, Point position) const {
  if(auto handle = get_handle(context); handle && std::hypot(
      position.m_x - handle->m_x, position.m_y - handle->m_y) <=
        HANDLE_RADIUS + WIRE_SLOP) {
    return true;
  }
  if(auto layout = layout_offset(context)) {
    return inflate(layout->m_handle, WIRE_SLOP, WIRE_SLOP).contains(position);
  }
  return false;
}

std::optional<Rect> PrototypeTool::get_flow_label(
    const ToolContext& context, const NodeId& artboard) const {
  auto& document = context.get_document();
  auto flow = find_flow(document, artboard);
  auto node = document.find(artboard);
  if(!flow || !node || !node->get_artboard() ||
      !node->m_header.m_is_visible) {
    return std::nullopt;
  }
  auto origin =
    context.to_view(node->get_artboard()->m_frame.get_top_left());
  auto offset = context.get_zoom() < SMALL_LABEL_ZOOM ? SMALL_LABEL_OFFSET :
    LABEL_OFFSET;
  return Rect(origin.m_x, origin.m_y + offset, get_label_width(flow->m_name),
    LABEL_HEIGHT);
}

bool PrototypeTool::is_focused_flow(
    const ToolContext& context, const NodeId& artboard) const {
  return !artboard.is_nil() && artboard == m_focused_flow &&
    context.get_editor().get_selection().get_nodes() == m_flow_selection;
}

void PrototypeTool::focus_flow(ToolContext& context, const NodeId& artboard) {
  auto& selection = context.get_editor().get_selection();
  auto& edit = context.get_editor().get_context();
  while(!edit.get_container().is_nil()) {
    edit.exit();
  }
  selection.select(find_reachable(context.get_document(), artboard));
  m_focused_flow = artboard;
  m_flow_selection = selection.get_nodes();
}

std::vector<NodeId> PrototypeTool::get_marked_artboards(
    const ToolContext& context) const {
  auto& document = context.get_document();
  auto artboards = std::vector<NodeId>();
  for(auto& id : context.get_editor().get_selection().get_nodes()) {
    if(auto artboard = find_artboard(document, id); !artboard.is_nil() &&
        std::ranges::find(artboards, artboard) == artboards.end()) {
      artboards.push_back(artboard);
    }
  }
  return artboards;
}

NodeId PrototypeTool::find_marker(
    const ToolContext& context, Point position) const {
  auto& document = context.get_document();
  auto markers = get_marked_artboards(context);
  for(auto& flow : document.get_prototype().m_flows) {
    markers.push_back(flow.m_artboard);
  }
  for(auto& artboard : markers) {
    if(auto marker = get_marker(context, artboard);
        marker && marker->contains(position)) {
      return artboard;
    }
  }
  return NodeId();
}

NodeId PrototypeTool::find_wire_target(
    const ToolContext& context, Point position) const {
  auto& document = context.get_document();
  auto triggers = std::vector<NodeId>();
  if(!m_source.is_nil()) {
    triggers.push_back(m_source);
  }
  for(auto& id : m_dragged) {
    if(auto interaction = find_edited(context, id)) {
      triggers.push_back(interaction->m_owner);
    }
  }
  auto artboard = find_destination(context, position);
  auto is_valid = [&] (const NodeId& destination) {
    return std::ranges::any_of(triggers, [&] (const auto& trigger) {
      if(is_artboard(document, destination)) {
        return destination != find_artboard(document, trigger);
      }
      return destination != trigger && !is_artboard(document, trigger) &&
        find_artboard(document, destination) ==
          find_artboard(document, trigger);
    });
  };
  if(!artboard.is_nil() && std::ranges::any_of(triggers,
      [&] (const auto& trigger) {
        return find_artboard(document, trigger) == artboard;
      })) {
    auto hit = context.get_hit_tester().hit(
      context.to_document(position), HIT_TOLERANCE / context.get_zoom());
    for(auto depth = 0; depth != 512 && !hit.is_nil(); ++depth) {
      auto node = document.find(hit);
      if(!node || node->get_artboard()) {
        return NodeId();
      }
      if(is_artboard(document, node->m_parent)) {
        break;
      }
      hit = node->m_parent;
    }
    if(!hit.is_nil() && is_valid(hit)) {
      return hit;
    }
    return NodeId();
  }
  if(!artboard.is_nil()) {
    if(is_valid(artboard)) {
      return artboard;
    }
    return NodeId();
  }
  auto title = context.get_titles().find(position);
  if(!title.is_nil() && is_valid(title)) {
    return title;
  }
  return NodeId();
}

const Interaction* PrototypeTool::find_edited(
    const ToolContext& context, const InteractionId& id) const {
  for(auto& interaction :
      context.get_document().get_prototype().m_interactions) {
    if(interaction.m_id == id) {
      return &interaction;
    }
  }
  for(auto& interaction : context.get_editor().get_context().get_pending()) {
    if(interaction.m_id == id) {
      return &interaction;
    }
  }
  return nullptr;
}

void PrototypeTool::select_trigger(ToolContext& context, const NodeId& owner) {
  auto& document = context.get_document();
  auto& edit = context.get_editor().get_context();
  auto node = document.find(owner);
  if(!node) {
    return;
  }
  if(node->m_parent.is_nil() || is_artboard(document, node->m_parent) ||
      !edit.enter(node->m_parent)) {
    while(!edit.get_container().is_nil()) {
      edit.exit();
    }
  }
  context.get_editor().get_selection().select(owner);
}

void PrototypeTool::select_wire(ToolContext& context,
    const std::vector<InteractionId>& interactions, bool is_shift) {
  auto& document = context.get_document();
  auto wire = std::vector<const Interaction*>();
  for(auto& id : interactions) {
    if(auto interaction = find_edited(context, id)) {
      wire.push_back(interaction);
    }
  }
  if(wire.empty()) {
    return;
  }
  auto& edit = context.get_editor().get_context();
  auto selected = find_selected(context);
  auto ids = std::vector<InteractionId>();
  if(is_shift) {
    for(auto interaction : selected) {
      if(std::ranges::find(wire, interaction) == wire.end()) {
        ids.push_back(interaction->m_id);
      }
    }
    if(ids.size() == selected.size()) {
      sort_by_trigger(wire);
      ids.push_back(wire.front()->m_id);
    }
  } else {
    select_trigger(context, wire.front()->m_owner);
    sort_by_trigger(wire);
    ids.push_back(wire.front()->m_id);
  }
  auto& selection = context.get_editor().get_selection();
  if(!selection.is_empty()) {
    edit.set_interactions(selection.get_primary(), std::move(ids));
  }
}

void PrototypeTool::create_wire(ToolContext& context, bool is_shift) {
  auto& document = context.get_document();
  auto& editor = context.get_editor();
  auto& last = editor.get_context().get_last_edited();
  auto addition = std::make_unique<AddInteractionCommand>(
    make_new_interaction(document, m_source,
      find_component_states(document, m_source).m_states.size() > 1,
      last ? &*last : nullptr));
  auto id = addition->get_interaction().m_id;
  if(!is_complete(document, addition->get_interaction())) {
    auto& edit = editor.get_context();
    auto before = edit.get_interaction_selection();
    auto pending = edit.get_pending();
    auto ids = std::vector<InteractionId>();
    if(is_shift) {
      for(auto interaction : find_selected(context)) {
        ids.push_back(interaction->m_id);
      }
    } else {
      select_trigger(context, m_source);
      pending.clear();
    }
    ids.push_back(id);
    pending.push_back(addition->get_interaction());
    edit.set_interactions(
      editor.get_selection().get_primary(), std::move(ids));
    edit.set_pending(std::move(pending));
    editor.record("Create Wire", std::move(before));
    return;
  }
  auto commands = std::vector<std::unique_ptr<Command>>();
  commands.push_back(std::move(addition));
  if(editor.execute(std::make_unique<CompositeCommand>(
      "Create Wire", std::move(commands)))) {
    select_wire(context, {id}, is_shift);
    editor.update_selection();
  }
}

void PrototypeTool::connect(
    ToolContext& context, const NodeId& destination, bool is_shift) {
  auto& document = context.get_document();
  auto& editor = context.get_editor();
  auto& last = editor.get_context().get_last_edited();
  auto commands = std::vector<std::unique_ptr<Command>>();
  auto addition = std::make_unique<AddInteractionCommand>(
    make_wired_interaction(document, m_source, destination,
      find_component_states(document, m_source).m_states.size() > 1,
      last ? &*last : nullptr));
  auto id = addition->get_interaction().m_id;
  commands.push_back(std::move(addition));
  auto artboard = find_artboard(document, m_source);
  if(document.get_prototype().m_flows.empty() && !artboard.is_nil()) {
    commands.push_back(
      std::make_unique<AddFlowCommand>(artboard, std::string()));
  }
  editor.execute(
    std::make_unique<CompositeCommand>("Draw Wire", std::move(commands)));
  if(std::ranges::none_of(document.get_prototype().m_interactions,
      [&] (const auto& interaction) {
        return interaction.m_id == id;
      })) {
    return;
  }
  auto owner = m_source;
  if(!is_shift) {
    select_trigger(context, owner);
  }
  auto& selection = editor.get_selection();
  if(!selection.is_empty()) {
    editor.get_context().set_interactions(selection.get_primary(), {id});
  }
  editor.update_selection();
}

void PrototypeTool::rewire(
    ToolContext& context, const NodeId& destination, bool is_shift) {
  auto& document = context.get_document();
  auto& editor = context.get_editor();
  auto commands = std::vector<std::unique_ptr<Command>>();
  auto owner = NodeId();
  auto kept = std::vector<InteractionId>();
  auto& edit = editor.get_context();
  auto pending = edit.get_pending();
  for(auto& id : m_dragged) {
    auto entry = std::ranges::find(pending, id, &Interaction::m_id);
    if(entry == pending.end()) {
      continue;
    }
    owner = entry->m_owner;
    if(!destination.is_nil()) {
      auto changed = retarget_interaction(document, *entry, destination);
      if(is_complete(document, changed)) {
        kept.push_back(id);
        commands.push_back(
          std::make_unique<AddInteractionCommand>(std::move(changed)));
      }
    }
    pending.erase(entry);
  }
  edit.set_pending(std::move(pending));
  for(auto& id : m_dragged) {
    for(auto& interaction : document.get_prototype().m_interactions) {
      if(interaction.m_id != id) {
        continue;
      }
      owner = interaction.m_owner;
      if(destination.is_nil()) {
        commands.push_back(std::make_unique<RemoveInteractionCommand>(id));
        continue;
      }
      kept.push_back(id);
      auto changed =
        retarget_interaction(document, interaction, destination);
      if(changed != interaction) {
        commands.push_back(
          std::make_unique<SetInteractionCommand>(std::move(changed)));
      }
    }
  }
  auto is_recorded = false;
  if(!commands.empty()) {
    is_recorded = editor.execute(std::make_unique<CompositeCommand>(
      "Draw Wire", std::move(commands))) != nullptr;
  }
  if(owner.is_nil()) {
    return;
  }
  if(!is_shift) {
    select_trigger(context, owner);
  }
  auto& selection = editor.get_selection();
  if(!selection.is_empty()) {
    editor.get_context().set_interactions(
      selection.get_primary(), std::move(kept));
  }
  if(is_recorded) {
    editor.update_selection();
  }
}

std::shared_ptr<const QImage> PrototypeTool::get_marker_image(
    Size size, bool is_flow, bool is_hovered, double scale) const {
  auto key = make_key(is_flow ? "flow_marker" : "marker",
    {size.m_width, size.m_height, static_cast<double>(is_hovered), scale});
  return cache_image(m_images, key, [&] {
    auto image = QImage(
      std::max(1, static_cast<int>(std::ceil(size.m_width * scale))),
      std::max(1, static_cast<int>(std::ceil(size.m_height * scale))),
      QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    auto painter = QPainter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    auto radius = size.m_width * MARKER_RADIUS;
    auto shape = QPainterPath();
    shape.moveTo(0, 0);
    shape.lineTo(size.m_width, 0);
    shape.lineTo(size.m_width, size.m_height);
    shape.lineTo(radius, size.m_height);
    shape.arcTo(QRectF(0, size.m_height - 2 * radius, 2 * radius, 2 * radius),
      270, -90);
    shape.closeSubpath();
    auto fill = MARKER_GREY;
    if(is_flow) {
      fill = MARKER_BLUE;
    } else if(is_hovered) {
      fill = MARKER_HOVER;
    }
    painter.fillPath(shape, fill);
    auto rim = QColor::fromRgbF(
      fill.redF() + (1 - fill.redF()) * MARKER_RIM,
      fill.greenF() + (1 - fill.greenF()) * MARKER_RIM,
      fill.blueF() + (1 - fill.blueF()) * MARKER_RIM);
    painter.fillRect(QRectF(0, 0, size.m_width, 1 / scale), rim);
    painter.fillRect(
      QRectF(0, 0, 1 / scale, size.m_height - radius), rim);
    if(size.m_width > SMALL_MARKER_SIZE.m_width) {
      auto top = size.m_width == LARGE_MARKER_SIZE.m_width ?
        LARGE_HOUSE_TOP : MEDIUM_HOUSE_TOP;
      auto left = (size.m_width - HOUSE_SIZE.m_width) / 2;
      auto w = HOUSE_SIZE.m_width;
      auto h = HOUSE_SIZE.m_height;
      auto eave = 0.45 * h;
      auto house = QPainterPath(QPointF(left + w / 2, top));
      for(auto point : {QPointF(w, eave), QPointF(0.85 * w, eave),
          QPointF(0.85 * w, h), QPointF(0.6 * w, h),
          QPointF(0.6 * w, 0.7 * h), QPointF(0.4 * w, 0.7 * h),
          QPointF(0.4 * w, h), QPointF(0.15 * w, h),
          QPointF(0.15 * w, eave), QPointF(0, eave)}) {
        house.lineTo(QPointF(left, top) + point);
      }
      house.closeSubpath();
      painter.fillPath(house, Qt::white);
    }
    painter.end();
    return image;
  });
}

std::shared_ptr<const QImage> PrototypeTool::get_label_image(
    const std::string& name, bool is_focused, double scale) const {
  auto key = "label|" + name + "|" + std::to_string(is_focused) + "|" +
    std::to_string(scale);
  return cache_image(m_images, key, [&] {
    auto font = QFont();
    font.setPixelSize(static_cast<int>(LABEL_FONT_SIZE));
    auto text = QString::fromStdString(name);
    auto width = get_label_width(name);
    auto image = QImage(
      std::max(1, static_cast<int>(std::ceil(width * scale))),
      std::max(1, static_cast<int>(std::ceil(LABEL_HEIGHT * scale))),
      QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    auto painter = QPainter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    painter.setPen(Qt::NoPen);
    painter.setBrush(is_focused ? MARKER_BLUE : LABEL_GREY);
    painter.drawRoundedRect(
      QRectF(0, 0, width, LABEL_HEIGHT), LABEL_RADIUS, LABEL_RADIUS);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QPointF(LABEL_PADDING, LABEL_BASELINE), text);
    painter.end();
    return image;
  });
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

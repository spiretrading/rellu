#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <variant>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Tools/SelectionBox.hpp"

using namespace Rellu;

namespace {
  constexpr auto HANDLE_RADIUS = 4.0;
  constexpr auto SCROLL_HANDLE_LENGTH = 18.0;
  constexpr auto SCROLL_HANDLE_THICKNESS = 10.0;
  constexpr auto DISTANT_SCROLL_HANDLE_LENGTH = 14.0;
  constexpr auto DISTANT_SCROLL_HANDLE_THICKNESS = 8.0;
  constexpr auto DISTANT_ZOOM = 0.33;
  constexpr auto HANDLE_INSET = 0.5;
  constexpr auto CORNERS = std::array<Handle, 4>({Handle::TOP_LEFT,
    Handle::TOP_RIGHT, Handle::BOTTOM_RIGHT, Handle::BOTTOM_LEFT});

  std::size_t to_index(Handle handle) {
    return static_cast<std::size_t>(handle);
  }

  void set(HandleLayout& layout, Handle handle, bool is_shown) {
    layout.m_resizers[to_index(handle)] = is_shown;
    layout.m_rotators[to_index(handle)] = is_shown;
  }

  bool is_artboard(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->get_artboard();
  }

  const Node* find_displayed(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node || !node->get_reference()) {
      return node;
    }
    auto resolution = resolve_reference(document.get_nodes(), id);
    return resolution ? document.find(resolution->m_source) : nullptr;
  }

  void hide_crowded(HandleLayout& layout, Size size) {
    if(size.m_width < 2 * HANDLE_RADIUS || size.m_height < 2 * HANDLE_RADIUS) {
      layout = HandleLayout();
      if(size.m_width >= 2 * HANDLE_RADIUS) {
        for(auto handle : {Handle::TOP, Handle::RIGHT, Handle::LEFT}) {
          set(layout, handle, true);
        }
      } else if(size.m_height >= 2 * HANDLE_RADIUS) {
        for(auto handle : {Handle::TOP, Handle::BOTTOM, Handle::LEFT}) {
          set(layout, handle, true);
        }
      } else {
        set(layout, Handle::BOTTOM_RIGHT, true);
      }
      return;
    }
    if(size.m_height < 4 * HANDLE_RADIUS) {
      set(layout, Handle::RIGHT, false);
      set(layout, Handle::LEFT, false);
    }
    if(size.m_width < 4 * HANDLE_RADIUS) {
      set(layout, Handle::TOP, false);
      set(layout, Handle::BOTTOM, false);
    }
  }

  Rect get_scroll_handle_rect(Handle handle, Size size, double zoom) {
    auto length = SCROLL_HANDLE_LENGTH;
    auto thickness = SCROLL_HANDLE_THICKNESS;
    if(zoom < DISTANT_ZOOM) {
      length = DISTANT_SCROLL_HANDLE_LENGTH;
      thickness = DISTANT_SCROLL_HANDLE_THICKNESS;
    }
    auto center = Point(size.m_width / 2, HANDLE_INSET);
    if(handle == Handle::BOTTOM) {
      center.m_y = size.m_height - HANDLE_INSET;
    } else if(handle == Handle::RIGHT) {
      center = Point(size.m_width - HANDLE_INSET, size.m_height / 2);
    } else if(handle == Handle::LEFT) {
      center = Point(HANDLE_INSET, size.m_height / 2);
    }
    if(handle == Handle::LEFT || handle == Handle::RIGHT) {
      std::swap(length, thickness);
    }
    return Rect(center.m_x - length / 2, center.m_y - thickness / 2, length,
      thickness);
  }

  bool overlaps(const Rect& left, const Rect& right) {
    return left.m_x < right.m_x + right.m_width &&
      left.m_x + left.m_width > right.m_x &&
      left.m_y < right.m_y + right.m_height &&
      left.m_y + left.m_height > right.m_y;
  }

  HandleLayout get_scroll_layout(
      ScrollDirection direction, Size size, double zoom) {
    auto layout = HandleLayout();
    if(direction != ScrollDirection::HORIZONTAL) {
      layout.m_resizers[to_index(Handle::TOP)] = true;
      layout.m_resizers[to_index(Handle::BOTTOM)] = true;
    }
    if(direction != ScrollDirection::VERTICAL) {
      layout.m_resizers[to_index(Handle::LEFT)] = true;
      layout.m_resizers[to_index(Handle::RIGHT)] = true;
    }
    auto priority = std::array<Handle, 4>(
      {Handle::RIGHT, Handle::BOTTOM, Handle::LEFT, Handle::TOP});
    for(auto i = std::size_t(0); i != priority.size(); ++i) {
      if(!layout.is_resizer(priority[i])) {
        continue;
      }
      auto rect = get_scroll_handle_rect(priority[i], size, zoom);
      for(auto j = i + 1; j != priority.size(); ++j) {
        if(layout.is_resizer(priority[j]) && overlaps(
            rect, get_scroll_handle_rect(priority[j], size, zoom))) {
          layout.m_resizers[to_index(priority[j])] = false;
        }
      }
    }
    return layout;
  }

  Point get_fraction(Handle handle) {
    switch(handle) {
      case Handle::TOP_LEFT:
        return Point(0, 0);
      case Handle::TOP:
        return Point(0.5, 0);
      case Handle::TOP_RIGHT:
        return Point(1, 0);
      case Handle::RIGHT:
        return Point(1, 0.5);
      case Handle::BOTTOM_RIGHT:
        return Point(1, 1);
      case Handle::BOTTOM:
        return Point(0.5, 1);
      case Handle::BOTTOM_LEFT:
        return Point(0, 1);
      default:
        return Point(0, 0.5);
    }
  }

  struct Span {
    double m_start;
    double m_length;
  };

  Span place(double start, double length, double length_after,
      double fraction, bool is_centered) {
    if(fraction == 0.5 || is_centered) {
      return Span(start + (length - length_after) / 2, length_after);
    } else if(fraction == 1) {
      return Span(start, length_after);
    }
    return Span(start + length - length_after, length_after);
  }

  double get_sign(double value) {
    if(value < 0) {
      return -1;
    }
    return 1;
  }

  double stretch(double start, double length, double position,
      double fraction, bool is_centered, bool is_flippable, double minimum) {
    if(fraction == 0.5 || length == 0) {
      return length;
    }
    auto result = 0.0;
    if(is_centered) {
      auto center = start + length / 2;
      result = 2 * (fraction == 1 ? position - center : center - position);
    } else if(fraction == 1) {
      result = position - start;
    } else {
      result = start + length - position;
    }
    auto smallest = std::min(minimum, length);
    if(is_flippable && result < 0) {
      return std::min(result, -smallest);
    }
    return std::max(result, smallest);
  }
}

Point SelectionBox::get_point(Handle handle) const {
  auto fraction = get_fraction(handle);
  return Point(m_rect.m_x + fraction.m_x * m_rect.m_width,
    m_rect.m_y + fraction.m_y * m_rect.m_height);
}

std::array<Point, 4> SelectionBox::get_corners() const {
  return std::array<Point, 4>({m_transform.map(get_point(Handle::TOP_LEFT)),
    m_transform.map(get_point(Handle::TOP_RIGHT)),
    m_transform.map(get_point(Handle::BOTTOM_RIGHT)),
    m_transform.map(get_point(Handle::BOTTOM_LEFT))});
}

Point SelectionBox::get_center() const {
  return m_transform.map(m_rect.get_center());
}

bool HandleLayout::is_resizer(Handle handle) const {
  return m_resizers[to_index(handle)];
}

bool HandleLayout::is_rotator(Handle handle) const {
  return m_rotators[to_index(handle)];
}

bool Rellu::is_corner(Handle handle) {
  return handle == Handle::TOP_LEFT || handle == Handle::TOP_RIGHT ||
    handle == Handle::BOTTOM_RIGHT || handle == Handle::BOTTOM_LEFT;
}

Handle Rellu::get_opposite(Handle handle) {
  return HANDLES[(static_cast<std::size_t>(handle) + 4) % HANDLES.size()];
}

std::optional<SelectionBox> Rellu::get_selection_box(const Document& document,
    const std::vector<NodeId>& nodes, const TextMeasurer& measurer) {
  auto top = get_top_level(document, nodes);
  if(top.empty()) {
    return std::nullopt;
  }
  if(top.size() == 1) {
    auto transform = get_world_transform(document, top.front());
    if(invert(transform)) {
      if(auto local = get_local_bounds(
          document, top.front(), BoundsType::GEOMETRY, measurer)) {
        return SelectionBox(*local, transform);
      }
      return std::nullopt;
    }
  }
  if(auto bounds =
      get_world_bounds(document, top, BoundsType::GEOMETRY, measurer)) {
    return SelectionBox(*bounds, Matrix());
  }
  return std::nullopt;
}

HandleLayout Rellu::get_handle_layout(const Document& document,
    const std::vector<NodeId>& nodes, Size size, double zoom) {
  if(nodes.empty()) {
    return HandleLayout();
  }
  auto node = nodes.size() == 1 ?
    find_displayed(document, nodes.front()) : nullptr;
  auto text = node ? node->get_text() : nullptr;
  auto group = node ? node->get_group() : nullptr;
  if(text && std::holds_alternative<PositionedFrame>(text->m_frame)) {
    auto layout = HandleLayout();
    layout.m_resizers[to_index(Handle::BOTTOM)] = true;
    layout.m_rotators[to_index(Handle::BOTTOM)] = true;
    return layout;
  } else if(group && group->m_scroll_group) {
    return get_scroll_layout(group->m_scroll_group->m_direction, size, zoom);
  }
  auto layout = HandleLayout();
  for(auto handle : HANDLES) {
    set(layout, handle, true);
  }
  auto limits = get_stack_limits(document, nodes);
  if(limits.m_is_width_fixed || limits.m_is_height_fixed) {
    for(auto handle : CORNERS) {
      layout.m_resizers[to_index(handle)] = false;
    }
  }
  if(limits.m_is_width_fixed) {
    layout.m_resizers[to_index(Handle::LEFT)] = false;
    layout.m_resizers[to_index(Handle::RIGHT)] = false;
  }
  if(limits.m_is_height_fixed) {
    layout.m_resizers[to_index(Handle::TOP)] = false;
    layout.m_resizers[to_index(Handle::BOTTOM)] = false;
  }
  if(limits.m_is_rotation_fixed) {
    layout.m_rotators = {};
  }
  hide_crowded(layout, size);
  if(std::any_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
      return is_artboard(document, id);
    })) {
    layout.m_rotators = {};
  }
  if(text && std::holds_alternative<AutoHeightFrame>(text->m_frame)) {
    for(auto handle : CORNERS) {
      layout.m_resizers[to_index(handle)] = false;
    }
  }
  for(auto handle : HANDLES) {
    if(!is_corner(handle) && layout.is_resizer(handle)) {
      layout.m_rotators[to_index(handle)] = false;
    }
  }
  return layout;
}

HandleLayout Rellu::get_locked_handle_layout(Size size) {
  auto layout = HandleLayout();
  layout.m_resizers.fill(true);
  hide_crowded(layout, size);
  layout.m_rotators = {};
  return layout;
}

Rect Rellu::resize(const Rect& rect, Handle handle, Point point,
    bool is_proportional, bool is_centered, bool is_flippable,
    double minimum) {
  auto fraction = get_fraction(handle);
  auto width = stretch(rect.m_x, rect.m_width, point.m_x, fraction.m_x,
    is_centered, is_flippable, minimum);
  auto height = stretch(rect.m_y, rect.m_height, point.m_y, fraction.m_y,
    is_centered, is_flippable, minimum);
  if(is_proportional && rect.m_width > 0 && rect.m_height > 0) {
    auto x_factor = width / rect.m_width;
    auto y_factor = height / rect.m_height;
    if(fraction.m_x == 0.5) {
      x_factor = std::abs(y_factor);
    } else if(fraction.m_y == 0.5) {
      y_factor = std::abs(x_factor);
    } else {
      auto factor = std::max(std::abs(x_factor), std::abs(y_factor));
      x_factor = get_sign(x_factor) * factor;
      y_factor = get_sign(y_factor) * factor;
    }
    width = rect.m_width * x_factor;
    height = rect.m_height * y_factor;
  }
  auto horizontal =
    place(rect.m_x, rect.m_width, width, fraction.m_x, is_centered);
  auto vertical =
    place(rect.m_y, rect.m_height, height, fraction.m_y, is_centered);
  return Rect(horizontal.m_start, vertical.m_start, horizontal.m_length,
    vertical.m_length);
}

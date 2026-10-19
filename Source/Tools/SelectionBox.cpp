#include <algorithm>
#include <cmath>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Tools/SelectionBox.hpp"

using namespace Rellu;

namespace {
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

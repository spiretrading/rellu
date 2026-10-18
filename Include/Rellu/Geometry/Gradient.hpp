#ifndef RELLU_GEOMETRY_GRADIENT_HPP
#define RELLU_GEOMETRY_GRADIENT_HPP
#include <variant>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Point.hpp"

namespace Rellu {

  /** A color at a position along a gradient. */
  struct GradientStop {

    /** The position along the gradient in the range [0, 1]. */
    double m_offset = 0;

    /** The color at the position. */
    Color m_color = Color(0, 0, 0, 1);

    constexpr bool operator ==(const GradientStop& stop) const;
  };

  /** The geometry of a gradient varying along a line. */
  struct LinearGradient {

    /** The point where the offset is 0. */
    Point m_start = Point(0, 0);

    /** The point where the offset is 1. */
    Point m_end = Point(1, 0);

    bool operator ==(const LinearGradient&) const = default;
  };

  /** The geometry of a gradient varying outward from a focal point. */
  struct RadialGradient {

    /** The center of the circle where the offset is 1. */
    Point m_center = Point(0.5, 0.5);

    /** The radius of the circle where the offset is 1. */
    double m_radius = 0.5;

    /** The point where the offset is 0. */
    Point m_focus = Point(0.5, 0.5);

    constexpr bool operator ==(const RadialGradient& gradient) const;
  };

  /** The geometry of a gradient, whose alternative gives its kind. */
  using GradientGeometry = std::variant<LinearGradient, RadialGradient>;

  constexpr bool GradientStop::operator ==(const GradientStop& stop) const {
    return equals(m_offset, stop.m_offset) && m_color == stop.m_color;
  }

  constexpr bool RadialGradient::operator ==(
      const RadialGradient& gradient) const {
    return m_center == gradient.m_center &&
      equals(m_radius, gradient.m_radius) && m_focus == gradient.m_focus;
  }
}

#endif

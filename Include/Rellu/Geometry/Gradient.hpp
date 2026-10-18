#ifndef RELLU_GEOMETRY_GRADIENT_HPP
#define RELLU_GEOMETRY_GRADIENT_HPP
#include <variant>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Point.hpp"

namespace Rellu {

  /** A color at a position along a gradient. */
  struct GradientStop {

    /** The position along the gradient in the range [0, 1]. */
    double m_offset = 0;

    /** The color at the position. */
    Color m_color = Color(0, 0, 0, 1);

    bool operator ==(const GradientStop&) const = default;
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

    bool operator ==(const RadialGradient&) const = default;
  };

  /** The geometry of a gradient, whose alternative gives its kind. */
  using GradientGeometry = std::variant<LinearGradient, RadialGradient>;
}

#endif

#ifndef RELLU_MODEL_LAYOUT_HPP
#define RELLU_MODEL_LAYOUT_HPP
#include "Rellu/Geometry/Equality.hpp"

namespace Rellu {

  /** Lists the axes along which content is arranged. */
  enum class Orientation : unsigned char {

    /** Along the x axis. */
    HORIZONTAL,

    /** Along the y axis. */
    VERTICAL
  };

  /** The space reserved on each side of a rectangle. */
  struct Margins {

    /** The space above. */
    double m_top = 0;

    /** The space to the right. */
    double m_right = 0;

    /** The space below. */
    double m_bottom = 0;

    /** The space to the left. */
    double m_left = 0;

    constexpr bool operator ==(const Margins& margins) const;
  };

  constexpr bool Margins::operator ==(const Margins& margins) const {
    return equals(m_top, margins.m_top) &&
      equals(m_right, margins.m_right) &&
      equals(m_bottom, margins.m_bottom) && equals(m_left, margins.m_left);
  }
}

#endif

#ifndef RELLU_GEOMETRY_SIZE_HPP
#define RELLU_GEOMETRY_SIZE_HPP
#include <ostream>

namespace Rellu {

  /** A width and height in document pixels. */
  struct Size {

    /** The width. */
    double m_width;

    /** The height. */
    double m_height;

    /** Returns <code>true</code> iff either dimension is not positive. */
    constexpr bool is_empty() const;

    bool operator ==(const Size&) const = default;
  };

  constexpr bool Size::is_empty() const {
    return !(m_width > 0) || !(m_height > 0);
  }

  inline std::ostream& operator <<(std::ostream& out, Size size) {
    return out << '(' << size.m_width << ", " << size.m_height << ')';
  }
}

#endif

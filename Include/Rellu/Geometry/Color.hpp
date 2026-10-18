#ifndef RELLU_GEOMETRY_COLOR_HPP
#define RELLU_GEOMETRY_COLOR_HPP
#include <cstdint>
#include <ostream>
#include "Rellu/Geometry/Equality.hpp"

namespace Rellu {

  /**
   * A straight (non-premultiplied) sRGB color with alpha, each component in
   * the range [0, 1].
   */
  struct Color {

    /** The red component. */
    double m_red;

    /** The green component. */
    double m_green;

    /** The blue component. */
    double m_blue;

    /** The opacity, where 0 is fully transparent and 1 is fully opaque. */
    double m_alpha;

    constexpr bool operator ==(const Color& color) const;
  };

  /**
   * Makes a Color from 8-bit components.
   * @param red The red component in the range [0, 255].
   * @param green The green component in the range [0, 255].
   * @param blue The blue component in the range [0, 255].
   * @param alpha The opacity in the range [0, 1].
   */
  constexpr Color make_color(int red, int green, int blue, double alpha = 1) {
    return Color(red / 255.0, green / 255.0, blue / 255.0, alpha);
  }

  /**
   * Makes a Color from a packed 32-bit value holding alpha, red, green and
   * blue in that order from the most significant byte, such as 0xFF000000
   * for opaque black.
   * @param argb The packed color.
   */
  constexpr Color make_argb_color(std::uint32_t argb) {
    return make_color(static_cast<int>((argb >> 16) & 0xFF),
      static_cast<int>((argb >> 8) & 0xFF), static_cast<int>(argb & 0xFF),
      ((argb >> 24) & 0xFF) / 255.0);
  }

  inline std::ostream& operator <<(std::ostream& out, const Color& color) {
    return out << '(' << color.m_red << ", " << color.m_green << ", " <<
      color.m_blue << ", " << color.m_alpha << ')';
  }

  constexpr bool Color::operator ==(const Color& color) const {
    return equals(m_red, color.m_red) && equals(m_green, color.m_green) &&
      equals(m_blue, color.m_blue) && equals(m_alpha, color.m_alpha);
  }
}

#endif

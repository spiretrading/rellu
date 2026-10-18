#ifndef RELLU_GEOMETRY_BLEND_MODE_HPP
#define RELLU_GEOMETRY_BLEND_MODE_HPP

namespace Rellu {

  /**
   * Lists the ways a node's pixels combine with what lies beneath them, as
   * defined by the W3C Compositing and Blending specification.
   */
  enum class BlendMode : unsigned char {
    NORMAL,
    MULTIPLY,
    SCREEN,
    OVERLAY,
    DARKEN,
    LIGHTEN,
    COLOR_DODGE,
    COLOR_BURN,
    HARD_LIGHT,
    SOFT_LIGHT,
    DIFFERENCE,
    EXCLUSION,
    HUE,
    SATURATION,
    COLOR,
    LUMINOSITY
  };
}

#endif

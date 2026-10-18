#ifndef RELLU_GEOMETRY_SVG_PATH_HPP
#define RELLU_GEOMETRY_SVG_PATH_HPP
#include <optional>
#include <string_view>
#include "Rellu/Geometry/Path.hpp"

namespace Rellu {

  /**
   * Parses SVG path data, converting elliptical arcs to cubic Bezier curves.
   * @param source The path data, such as the value of an SVG
   *        <code>d</code> attribute.
   * @return The parsed Path using the non-zero fill rule, or
   *         <code>std::nullopt</code> if the <i>source</i> is malformed.
   */
  std::optional<Path> parse_svg_path(std::string_view source);
}

#endif

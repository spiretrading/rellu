#ifndef RELLU_TOOLS_CANVAS_BOUNDS_HPP
#define RELLU_TOOLS_CANVAS_BOUNDS_HPP
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"

namespace Rellu {

  /** The extent of Adobe XD's canvas in document coordinates. */
  constexpr auto CANVAS_BOUNDS = Rect(-25000, -25000, 50000, 50000);

  /** The color Adobe XD draws beyond the canvas's extent. */
  constexpr auto OUTSIDE_CANVAS_COLOR = make_color(0xA0, 0xA0, 0xA0);

  /**
   * Returns the parts of a view beyond CANVAS_BOUNDS, in logical units.
   * @param view The transform from document coordinates to logical units,
   *        scaling both axes alike without rotating.
   * @param size The size of the view in logical units.
   */
  std::vector<Rect> get_outside_regions(const Matrix& view, Size size);
}

#endif

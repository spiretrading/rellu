#ifndef RELLU_TOOLS_ARTBOARD_GRIDS_HPP
#define RELLU_TOOLS_ARTBOARD_GRIDS_HPP
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"

namespace Rellu {

  /**
   * Draws the design grids the visible artboards show, as Adobe XD draws
   * them over the canvas: the columns of a layout grid filled in its column
   * color, or the lines of a square grid in its colors, omitted while its
   * cells are too small on screen to tell apart.
   * @param document The document.
   * @param view The transform from document space to the frame.
   * @param size The size of the frame.
   * @param overlay The overlay to draw on.
   */
  void draw_artboard_grids(const Document& document, const Matrix& view,
    Size size, CanvasOverlay& overlay);
}

#endif

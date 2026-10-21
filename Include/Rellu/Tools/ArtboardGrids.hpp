#ifndef RELLU_TOOLS_ARTBOARD_GRIDS_HPP
#define RELLU_TOOLS_ARTBOARD_GRIDS_HPP
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"
#include "Rellu/Tools/GridSettings.hpp"

namespace Rellu {

  /**
   * Draws the kinds of design grid shown on every visible artboard, as
   * Adobe XD draws them over the canvas: the columns of a layout grid filled
   * in its column color, darkened by a thin band of it just inside each
   * edge, with the tops and bottoms of its gutters capped in it one logical
   * pixel high, or the lines of a square grid in its colors, omitted while
   * its cells are too small on screen to tell apart. An artboard without a
   * grid shows the grid of make_initial_grid.
   * @param document The document.
   * @param settings The kinds of grid shown.
   * @param view The transform from document space to the frame.
   * @param size The size of the frame.
   * @param overlay The overlay to draw on.
   */
  void draw_artboard_grids(const Document& document,
    const GridSettings& settings, const Matrix& view, Size size,
    CanvasOverlay& overlay);
}

#endif

#ifndef RELLU_TOOLS_ARTBOARD_OUTLINES_HPP
#define RELLU_TOOLS_ARTBOARD_OUTLINES_HPP
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"

namespace Rellu {

  /** The color of the outline of an artboard holding no selected node. */
  inline constexpr auto ARTBOARD_OUTLINE_COLOR = make_color(205, 205, 205);

  /** The color of the outline of an artboard holding a selected node. */
  inline constexpr auto FOCUSED_ARTBOARD_OUTLINE_COLOR =
    make_color(160, 160, 160);

  /** The width in logical pixels of an artboard's outline. */
  inline constexpr auto ARTBOARD_OUTLINE_WIDTH = 1.0;

  /**
   * Draws the outlines Adobe XD draws on the canvas just outside the frames
   * of the visible artboards, ARTBOARD_OUTLINE_WIDTH wide and darker around
   * the artboards holding a selected node, leaving out the device pixels an
   * artboard partly covers.
   * @param document The document.
   * @param view The transform from document coordinates to logical pixels.
   * @param ratio The number of device pixels per logical pixel.
   * @param selection The selected nodes.
   * @param overlay The overlay receiving the outlines.
   */
  void draw_artboard_outlines(const Document& document, const Matrix& view,
    double ratio, const std::vector<NodeId>& selection,
    CanvasOverlay& overlay);
}

#endif

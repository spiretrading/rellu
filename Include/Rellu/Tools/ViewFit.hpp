#ifndef RELLU_TOOLS_VIEW_FIT_HPP
#define RELLU_TOOLS_VIEW_FIT_HPP
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"

namespace Rellu {

  /** The smallest zoom of the canvas. */
  constexpr auto MINIMUM_ZOOM = 1.0 / 64;

  /** The largest zoom of the canvas. */
  constexpr auto MAXIMUM_ZOOM = 256.0;

  /**
   * The space in logical pixels Adobe XD leaves to the left and right of a
   * region it zooms to fit.
   */
  constexpr auto FIT_PADDING_X = 62.0;

  /**
   * The space in logical pixels Adobe XD leaves above and below a region it
   * zooms to fit.
   */
  constexpr auto FIT_PADDING_Y = 73.5;

  /** The zoom and offset of the canvas. */
  struct ViewFit {

    /** The number of logical pixels per document pixel. */
    double m_zoom = 1;

    /** The position on the canvas of the document's origin. */
    Point m_offset = Point(0, 0);
  };

  /**
   * Returns the view Adobe XD zooms to so that a region fits the canvas, as
   * Zoom to Fit All, Zoom to Selection, the zoom tool's marquee and a new
   * document show it: the largest zoom leaving FIT_PADDING_X to its sides
   * and FIT_PADDING_Y above and below it, with the region centred.
   * @param region The region in document coordinates.
   * @param size The size of the canvas in logical pixels.
   */
  ViewFit fit_view(const Rect& region, Size size);

  /**
   * Returns the region fit_view shows at a zoom, centred on a point, so
   * that fitting it pans the canvas without zooming.
   * @param center The point in document coordinates.
   * @param zoom The zoom.
   * @param size The size of the canvas in logical pixels.
   */
  Rect get_fitted_region(Point center, double zoom, Size size);

  /**
   * Returns <code>true</code> iff a window first shown has the state and
   * size it was restored to, so that a view drawn in it is drawn at its
   * final size.
   * @param is_restored_expanded Whether the window was restored maximized or
   *        full screen.
   * @param is_expanded Whether the window is maximized or full screen.
   * @param size The size of the window's frame in logical pixels.
   * @param area The size in logical pixels of the area an expanded window
   *        fills.
   */
  bool is_settled(
    bool is_restored_expanded, bool is_expanded, Size size, Size area);

  /**
   * Returns the geometry of a window first shown without a saved geometry:
   * a size shrunk to fit an area and centred in it.
   * @param size The window's size in logical pixels.
   * @param area The area in logical pixels.
   */
  Rect center_window(Size size, const Rect& area);

  /**
   * Returns a window's bounds moved, and shrunk where larger than an area,
   * to lie within the area.
   * @param bounds The window's bounds.
   * @param area The area.
   */
  Rect fit_window(const Rect& bounds, const Rect& area);
}

#endif

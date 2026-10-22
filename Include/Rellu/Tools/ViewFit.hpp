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
  constexpr auto FIT_PADDING_X = 60.0;

  /**
   * The space in logical pixels Adobe XD leaves above and below a region it
   * zooms to fit.
   */
  constexpr auto FIT_PADDING_Y = 72.5;

  /** The narrowest client area of Adobe XD's window, in logical pixels. */
  constexpr auto MINIMUM_CLIENT_WIDTH = 500.0;

  /**
   * The shortest Adobe XD's window may be, its resize border below the
   * client area included, in logical pixels.
   */
  constexpr auto MINIMUM_WINDOW_HEIGHT = 500.0;

  /** The narrowest the canvas becomes, in logical pixels. */
  constexpr auto MINIMUM_CANVAS_WIDTH = 40.0;

  /**
   * The width in logical pixels of the canvas that the left pane leaves
   * while it is no narrower than MINIMUM_PANE_WIDTH.
   */
  constexpr auto PANE_CANVAS_WIDTH = 160.0;

  /**
   * The narrowest the left pane is dragged or fitted to while the canvas
   * keeps MINIMUM_CANVAS_WIDTH, in logical pixels.
   */
  constexpr auto MINIMUM_PANE_WIDTH = 244.0;

  /**
   * Returns the width Adobe XD gives its left pane once its window changes
   * size: the width stored, no wider than leaves the canvas
   * PANE_CANVAS_WIDTH and no narrower than MINIMUM_PANE_WIDTH, then
   * narrower where the canvas would otherwise be narrower than
   * MINIMUM_CANVAS_WIDTH.
   * @param stored The pane's width stored, in logical pixels.
   * @param available The width in logical pixels shared by the pane and the
   *        canvas.
   * @return The pane's width, which becomes the width stored.
   */
  double fit_pane_width(double stored, double available);

  /**
   * Returns the smallest client area of a window in device pixels, as
   * Adobe XD limits its window.
   * @param ratio The number of device pixels per logical pixel.
   * @param border The height in device pixels of the window's resize border
   *        below its client area.
   */
  Size get_minimum_client_size(double ratio, double border);

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
   * Places a view's offset as Adobe XD places it, the distance in device
   * pixels from the corner of the canvas to the document's origin truncated
   * toward zero.
   * @param offset The position of the document's origin in logical pixels.
   * @param origin The position of the canvas's corner in logical pixels.
   * @param ratio The number of device pixels per logical pixel.
   * @return The offset placed.
   */
  Point snap_view_offset(Point offset, Point origin, double ratio);

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

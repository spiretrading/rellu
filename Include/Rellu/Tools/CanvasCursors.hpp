#ifndef RELLU_TOOLS_CANVAS_CURSORS_HPP
#define RELLU_TOOLS_CANVAS_CURSORS_HPP
#include <QImage>
#include <QPoint>
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /** The width and height in logical pixels of a pointer's image. */
  inline constexpr auto CANVAS_CURSOR_SIZE = 32;

  /**
   * Returns which of the turned images of a pointer shows it, as Adobe XD
   * picks it from the pointer's angle.
   * @param cursor The pointer.
   * @return The eighth of a turn nearest the angle, from 0 through 7, of a
   *         ROTATE pointer, the same taken modulo 4 for a RESIZE pointer,
   *         and 0 for any other pointer.
   */
  int get_orientation(const ToolCursor& cursor);

  /**
   * Returns the point of a pointer's image that lies at the pointer's
   * position, as Adobe XD places it.
   * @param cursor The pointer.
   * @return The point in logical pixels from the top left of the image.
   */
  QPoint get_hot_spot(const ToolCursor& cursor);

  /**
   * Draws the image of a pointer shown over the canvas, looking like Adobe
   * XD's.
   * @param cursor The pointer.
   * @param ratio The device pixel ratio drawn at.
   * @return An image CANVAS_CURSOR_SIZE logical pixels square, of the
   *         <i>ratio</i> given.
   */
  QImage draw_cursor(const ToolCursor& cursor, double ratio);
}

#endif

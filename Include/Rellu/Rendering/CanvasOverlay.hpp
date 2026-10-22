#ifndef RELLU_RENDERING_CANVAS_OVERLAY_HPP
#define RELLU_RENDERING_CANVAS_OVERLAY_HPP
#include <memory>
#include <optional>
#include <vector>
#include <QImage>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Size.hpp"

namespace Rellu {

  /**
   * A rectangle drawn over the canvas in the frame's logical units, turned
   * and rounded, filled within a stroke along the inside of its edges. The
   * edges of a snapped rectangle turned by a multiple of a right angle lie
   * on whole pixels.
   */
  struct OverlayShape {

    /** The center. */
    Point m_center = Point(0, 0);

    /** The width and height before turning. */
    Size m_size = Size(0, 0);

    /** The angle in radians the rectangle is turned by, clockwise on screen. */
    double m_angle = 0;

    /** The radius rounding each corner. */
    double m_radius = 0;

    /** The straight alpha color filling the inside of the stroke. */
    Color m_fill = Color(0, 0, 0, 0);

    /** The straight alpha color of the stroke. */
    Color m_stroke = Color(0, 0, 0, 0);

    /**
     * The width of the stroke, drawn as a whole number of pixels, at least
     * one when positive.
     */
    double m_stroke_width = 0;

    /**
     * Whether the edges lie on whole pixels when turned by a multiple of a
     * right angle, rather than covering fractions of pixels.
     */
    bool m_is_snapped = true;
  };

  /**
   * An image drawn over the canvas with one of its pixels per pixel, or
   * stretched over a size.
   */
  struct OverlayImage {

    /** The non-null image. */
    std::shared_ptr<const QImage> m_image;

    /**
     * The position of the image's top left corner in logical units, drawn
     * at the nearest whole pixel.
     */
    Point m_position = Point(0, 0);

    /**
     * The size in logical units the image is stretched over, rounded to
     * whole pixels, or none to draw it one of its pixels per pixel.
     */
    std::optional<Size> m_size;
  };

  /** What is drawn over the canvas in screen space, images below shapes. */
  struct CanvasOverlay {

    /** The images in drawing order. */
    std::vector<OverlayImage> m_images;

    /** The shapes in drawing order. */
    std::vector<OverlayShape> m_shapes;

    /** Returns <code>true</code> iff there is nothing to draw. */
    bool is_empty() const;
  };

  /**
   * Makes an OverlayShape drawing a straight line.
   * @param start The start of the line in logical units.
   * @param end The end of the line in logical units.
   * @param width The width of the line.
   * @param color The straight alpha color of the line.
   */
  OverlayShape make_overlay_line(
    Point start, Point end, double width, Color color);

  inline bool CanvasOverlay::is_empty() const {
    return m_images.empty() && m_shapes.empty();
  }
}

#endif

#ifndef RELLU_GEOMETRY_SHAPES_HPP
#define RELLU_GEOMETRY_SHAPES_HPP
#include <span>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"

namespace Rellu {

  /**
   * The radius of each corner of a rectangle, in the order Adobe XD stores
   * them.
   */
  struct CornerRadii {

    /** The radius of the top left corner. */
    double m_top_left;

    /** The radius of the top right corner. */
    double m_top_right;

    /** The radius of the bottom right corner. */
    double m_bottom_right;

    /** The radius of the bottom left corner. */
    double m_bottom_left;

    constexpr bool operator ==(const CornerRadii& radii) const;
  };

  /**
   * Returns the corner radii Adobe XD renders for a rectangle: negative
   * radii become zero and, where the radii at the ends of a side add up to
   * more than its length, every radius is scaled down by the same factor so
   * that they fit.
   * @param radii The requested corner radii.
   * @param size The size of the rectangle.
   * @return The effective corner radii.
   */
  CornerRadii clamp_corner_radii(const CornerRadii& radii, Size size);

  /**
   * Makes a closed rectangle running clockwise on screen from its top left
   * corner.
   * @param bounds The rectangle.
   */
  Path make_rectangle(const Rect& bounds);

  /**
   * Makes a closed rectangle with rounded corners, running clockwise on
   * screen. Without rounding it starts from its top left corner; otherwise,
   * as Adobe XD orders it, it starts where its left side meets its bottom
   * left corner and draws a line along every side, even one of zero length,
   * unless it is a circle.
   * @param bounds The rectangle.
   * @param radii The requested corner radii, clamped by clamp_corner_radii.
   */
  Path make_rectangle(const Rect& bounds, const CornerRadii& radii);

  /**
   * Makes a closed ellipse running clockwise on screen from its rightmost
   * point.
   * @param center The center of the ellipse.
   * @param x_radius The horizontal radius.
   * @param y_radius The vertical radius.
   */
  Path make_ellipse(Point center, double x_radius, double y_radius);

  /**
   * Makes a closed circle running clockwise on screen from its rightmost
   * point.
   * @param center The center of the circle.
   * @param radius The radius.
   */
  Path make_circle(Point center, double radius);

  /**
   * Makes an open straight line.
   * @param start The start of the line.
   * @param end The end of the line.
   */
  Path make_line(Point start, Point end);

  /**
   * Makes a closed polygon through a sequence of vertices, rounding each
   * vertex with a circular arc tangent to both of its edges. A radius too
   * large for an edge is reduced so that no rounding takes up more than half
   * of either adjacent edge.
   * @param vertices The vertices in order.
   * @param corner_radius The radius rounding each vertex.
   */
  Path make_polygon(std::span<const Point> vertices, double corner_radius);

  /**
   * Makes the regular polygon Adobe XD draws for a corner count, starting
   * from a vertex at the top and running clockwise on screen, stretched so
   * its vertices span the bounds.
   * @param corner_count The number of vertices, at least 3; fewer produce an
   *        empty Path.
   * @param bounds The rect the vertices span.
   * @param corner_radius The radius rounding each vertex.
   */
  Path make_polygon(int corner_count, const Rect& bounds, double corner_radius);

  constexpr bool CornerRadii::operator ==(const CornerRadii& radii) const {
    return equals(m_top_left, radii.m_top_left) &&
      equals(m_top_right, radii.m_top_right) &&
      equals(m_bottom_right, radii.m_bottom_right) &&
      equals(m_bottom_left, radii.m_bottom_left);
  }
}

#endif

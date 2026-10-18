#ifndef RELLU_GEOMETRY_POINT_HPP
#define RELLU_GEOMETRY_POINT_HPP
#include <cmath>
#include <ostream>
#include "Rellu/Geometry/Equality.hpp"

namespace Rellu {

  /** A point or displacement in document pixels, with y growing downward. */
  struct Point {

    /** The x coordinate. */
    double m_x;

    /** The y coordinate. */
    double m_y;

    constexpr bool operator ==(const Point& point) const;
  };

  constexpr Point operator +(Point left, Point right) {
    return Point(left.m_x + right.m_x, left.m_y + right.m_y);
  }

  constexpr Point operator -(Point left, Point right) {
    return Point(left.m_x - right.m_x, left.m_y - right.m_y);
  }

  constexpr Point operator -(Point point) {
    return Point(-point.m_x, -point.m_y);
  }

  constexpr Point operator *(Point point, double scalar) {
    return Point(point.m_x * scalar, point.m_y * scalar);
  }

  constexpr Point operator *(double scalar, Point point) {
    return point * scalar;
  }

  constexpr Point operator /(Point point, double scalar) {
    return Point(point.m_x / scalar, point.m_y / scalar);
  }

  /** Returns the dot product of two vectors. */
  constexpr double dot(Point left, Point right) {
    return left.m_x * right.m_x + left.m_y * right.m_y;
  }

  /**
   * Returns the z component of the cross product of two vectors, which is
   * positive when <i>right</i> lies clockwise from <i>left</i> on screen.
   */
  constexpr double cross(Point left, Point right) {
    return left.m_x * right.m_y - left.m_y * right.m_x;
  }

  /** Returns the length of a vector. */
  inline double length(Point point) {
    return std::hypot(point.m_x, point.m_y);
  }

  /** Returns the distance between two points. */
  inline double distance(Point left, Point right) {
    return length(right - left);
  }

  /**
   * Linearly interpolates between two points.
   * @param start The point at <i>t</i> = 0.
   * @param end The point at <i>t</i> = 1.
   * @param t The interpolation parameter.
   * @return The point a fraction <i>t</i> of the way from <i>start</i> to
   *         <i>end</i>.
   */
  constexpr Point interpolate(Point start, Point end, double t) {
    return start + (end - start) * t;
  }

  inline std::ostream& operator <<(std::ostream& out, Point point) {
    return out << '(' << point.m_x << ", " << point.m_y << ')';
  }

  constexpr bool Point::operator ==(const Point& point) const {
    return equals(m_x, point.m_x) && equals(m_y, point.m_y);
  }
}

#endif

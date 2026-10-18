#ifndef RELLU_GEOMETRY_RECT_HPP
#define RELLU_GEOMETRY_RECT_HPP
#include <algorithm>
#include <ostream>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Size.hpp"

namespace Rellu {

  /** An axis-aligned rectangle in document pixels, with y growing downward. */
  struct Rect {

    /** The x coordinate of the left edge. */
    double m_x;

    /** The y coordinate of the top edge. */
    double m_y;

    /** The width. */
    double m_width;

    /** The height. */
    double m_height;

    constexpr double get_left() const;
    constexpr double get_top() const;
    constexpr double get_right() const;
    constexpr double get_bottom() const;
    constexpr Point get_top_left() const;
    constexpr Point get_bottom_right() const;
    constexpr Point get_center() const;
    constexpr Size get_size() const;

    /** Returns <code>true</code> iff either dimension is not positive. */
    constexpr bool is_empty() const;

    /**
     * Tests whether a point lies within this rect, including its left and top
     * edges but excluding its right and bottom edges.
     * @param point The point to test.
     * @return <code>true</code> iff this rect contains the <i>point</i>.
     */
    constexpr bool contains(Point point) const;

    /**
     * Tests whether a rect lies entirely within this rect, edges included.
     * @param rect The rect to test.
     * @return <code>true</code> iff neither rect is empty and this rect
     *         contains the <i>rect</i>.
     */
    constexpr bool contains(const Rect& rect) const;

    /**
     * Tests whether two rects overlap with a positive area.
     * @param rect The rect to test.
     * @return <code>true</code> iff this rect and the <i>rect</i> share a
     *         region of positive area.
     */
    constexpr bool intersects(const Rect& rect) const;

    constexpr bool operator ==(const Rect& rect) const;
  };

  /**
   * Returns the overlap of two rects.
   * @param left The first rect.
   * @param right The second rect.
   * @return The region common to both rects, or an all-zero Rect if they do
   *         not overlap with a positive area.
   */
  constexpr Rect intersect(const Rect& left, const Rect& right) {
    if(!left.intersects(right)) {
      return Rect();
    }
    auto x = std::max(left.get_left(), right.get_left());
    auto y = std::max(left.get_top(), right.get_top());
    return Rect(x, y, std::min(left.get_right(), right.get_right()) - x,
      std::min(left.get_bottom(), right.get_bottom()) - y);
  }

  /**
   * Returns the smallest rect containing two rects, where an empty rect
   * contributes nothing.
   * @param left The first rect.
   * @param right The second rect.
   * @return The union of both rects' bounds.
   */
  constexpr Rect unite(const Rect& left, const Rect& right) {
    if(left.is_empty()) {
      return right;
    } else if(right.is_empty()) {
      return left;
    }
    auto x = std::min(left.get_left(), right.get_left());
    auto y = std::min(left.get_top(), right.get_top());
    return Rect(x, y, std::max(left.get_right(), right.get_right()) - x,
      std::max(left.get_bottom(), right.get_bottom()) - y);
  }

  /**
   * Grows a rect outward on every side, shrinking it for negative amounts.
   * @param rect The rect to grow.
   * @param x The amount to move the left and right edges outward.
   * @param y The amount to move the top and bottom edges outward.
   * @return The grown rect.
   */
  constexpr Rect inflate(const Rect& rect, double x, double y) {
    return Rect(rect.m_x - x, rect.m_y - y, rect.m_width + 2 * x,
      rect.m_height + 2 * y);
  }

  /**
   * Grows a rect outward by the same amount on every side.
   * @param rect The rect to grow.
   * @param amount The amount to move each edge outward.
   * @return The grown rect.
   */
  constexpr Rect inflate(const Rect& rect, double amount) {
    return inflate(rect, amount, amount);
  }

  constexpr double Rect::get_left() const {
    return m_x;
  }

  constexpr double Rect::get_top() const {
    return m_y;
  }

  constexpr double Rect::get_right() const {
    return m_x + m_width;
  }

  constexpr double Rect::get_bottom() const {
    return m_y + m_height;
  }

  constexpr Point Rect::get_top_left() const {
    return Point(m_x, m_y);
  }

  constexpr Point Rect::get_bottom_right() const {
    return Point(get_right(), get_bottom());
  }

  constexpr Point Rect::get_center() const {
    return Point(m_x + m_width / 2, m_y + m_height / 2);
  }

  constexpr Size Rect::get_size() const {
    return Size(m_width, m_height);
  }

  constexpr bool Rect::is_empty() const {
    return get_size().is_empty();
  }

  constexpr bool Rect::contains(Point point) const {
    return point.m_x >= m_x && point.m_x < get_right() && point.m_y >= m_y &&
      point.m_y < get_bottom();
  }

  constexpr bool Rect::contains(const Rect& rect) const {
    return !is_empty() && !rect.is_empty() && rect.m_x >= m_x &&
      rect.m_y >= m_y && rect.get_right() <= get_right() &&
      rect.get_bottom() <= get_bottom();
  }

  constexpr bool Rect::intersects(const Rect& rect) const {
    return !is_empty() && !rect.is_empty() && rect.m_x < get_right() &&
      m_x < rect.get_right() && rect.m_y < get_bottom() &&
      m_y < rect.get_bottom();
  }

  inline std::ostream& operator <<(std::ostream& out, const Rect& rect) {
    return out << '(' << rect.m_x << ", " << rect.m_y << ", " << rect.m_width <<
      ", " << rect.m_height << ')';
  }

  constexpr bool Rect::operator ==(const Rect& rect) const {
    return equals(m_x, rect.m_x) && equals(m_y, rect.m_y) &&
      equals(m_width, rect.m_width) && equals(m_height, rect.m_height);
  }
}

#endif

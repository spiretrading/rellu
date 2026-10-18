#ifndef RELLU_GEOMETRY_PROJECTION_HPP
#define RELLU_GEOMETRY_PROJECTION_HPP
#include <algorithm>
#include <cmath>
#include <optional>
#include <ostream>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"

namespace Rellu {

  /**
   * A 2D projective transform extending Matrix's convention with a divisor,
   * mapping a point (x, y) to ((a * x + c * y + tx) / w,
   * (b * x + d * y + ty) / w) where w = wx * x + wy * y + w0. A default
   * constructed Projection is the identity.
   */
  struct Projection {

    /** The x coordinate's contribution to the mapped x coordinate. */
    double m_a = 1;

    /** The x coordinate's contribution to the mapped y coordinate. */
    double m_b = 0;

    /** The y coordinate's contribution to the mapped x coordinate. */
    double m_c = 0;

    /** The y coordinate's contribution to the mapped y coordinate. */
    double m_d = 1;

    /** The horizontal translation. */
    double m_tx = 0;

    /** The vertical translation. */
    double m_ty = 0;

    /** The x coordinate's contribution to the divisor. */
    double m_wx = 0;

    /** The y coordinate's contribution to the divisor. */
    double m_wy = 0;

    /** The constant term of the divisor. */
    double m_w0 = 1;

    constexpr bool is_affine() const;

    /**
     * Returns the divisor of a point.
     * @param point The point.
     * @return The divisor, positive where the point lies in front of the
     *         viewer.
     */
    constexpr double get_divisor(Point point) const;

    constexpr Point map(Point point) const;

    /**
     * Maps a rect, returning the bounds of its four mapped corners.
     * @param rect The rect to map, which must lie in front of the viewer.
     * @return The smallest axis-aligned rect containing the mapped <i>rect</i>.
     */
    constexpr Rect map(const Rect& rect) const;

    constexpr bool operator ==(const Projection& projection) const;
  };

  /**
   * Converts an affine transform to the Projection mapping points alike.
   * @param matrix The affine transform.
   */
  constexpr Projection to_projection(const Matrix& matrix) {
    return Projection(matrix.m_a, matrix.m_b, matrix.m_c, matrix.m_d,
      matrix.m_tx, matrix.m_ty, 0, 0, 1);
  }

  /**
   * Composes two transforms.
   * @param left The transform to apply second.
   * @param right The transform to apply first.
   * @return The transform that applies <i>right</i> and then <i>left</i>.
   */
  constexpr Projection operator *(
      const Projection& left, const Projection& right) {
    return Projection(
      left.m_a * right.m_a + left.m_c * right.m_b + left.m_tx * right.m_wx,
      left.m_b * right.m_a + left.m_d * right.m_b + left.m_ty * right.m_wx,
      left.m_a * right.m_c + left.m_c * right.m_d + left.m_tx * right.m_wy,
      left.m_b * right.m_c + left.m_d * right.m_d + left.m_ty * right.m_wy,
      left.m_a * right.m_tx + left.m_c * right.m_ty + left.m_tx * right.m_w0,
      left.m_b * right.m_tx + left.m_d * right.m_ty + left.m_ty * right.m_w0,
      left.m_wx * right.m_a + left.m_wy * right.m_b + left.m_w0 * right.m_wx,
      left.m_wx * right.m_c + left.m_wy * right.m_d + left.m_w0 * right.m_wy,
      left.m_wx * right.m_tx + left.m_wy * right.m_ty +
        left.m_w0 * right.m_w0);
  }

  /**
   * Composes a projective transform after an affine one.
   * @param left The transform to apply second.
   * @param right The transform to apply first.
   * @return The transform that applies <i>right</i> and then <i>left</i>.
   */
  constexpr Projection operator *(
      const Projection& left, const Matrix& right) {
    return left * to_projection(right);
  }

  /**
   * Composes an affine transform after a projective one.
   * @param left The transform to apply second.
   * @param right The transform to apply first.
   * @return The transform that applies <i>right</i> and then <i>left</i>.
   */
  constexpr Projection operator *(
      const Matrix& left, const Projection& right) {
    return to_projection(left) * right;
  }

  /**
   * Inverts a transform.
   * @param projection The transform to invert.
   * @return The transform undoing <i>projection</i>, or
   *         <code>std::nullopt</code> if <i>projection</i> is singular.
   */
  inline std::optional<Projection> invert(const Projection& projection) {
    auto& p = projection;
    auto a = p.m_d * p.m_w0 - p.m_ty * p.m_wy;
    auto b = p.m_ty * p.m_wx - p.m_b * p.m_w0;
    auto wx = p.m_b * p.m_wy - p.m_d * p.m_wx;
    auto determinant = p.m_a * a + p.m_c * b + p.m_tx * wx;
    if(determinant == 0) {
      return std::nullopt;
    }
    auto inverse = Projection(a / determinant, b / determinant,
      (p.m_tx * p.m_wy - p.m_c * p.m_w0) / determinant,
      (p.m_a * p.m_w0 - p.m_tx * p.m_wx) / determinant,
      (p.m_c * p.m_ty - p.m_tx * p.m_d) / determinant,
      (p.m_tx * p.m_b - p.m_a * p.m_ty) / determinant, wx / determinant,
      (p.m_c * p.m_wx - p.m_a * p.m_wy) / determinant,
      (p.m_a * p.m_d - p.m_c * p.m_b) / determinant);
    for(auto value : {inverse.m_a, inverse.m_b, inverse.m_c, inverse.m_d,
        inverse.m_tx, inverse.m_ty, inverse.m_wx, inverse.m_wy,
        inverse.m_w0}) {
      if(!std::isfinite(value)) {
        return std::nullopt;
      }
    }
    return inverse;
  }

  /**
   * Returns the affine transform agreeing with a projective one to first
   * order about a point.
   * @param projection The projective transform.
   * @param point The point, which must lie in front of the viewer.
   * @return The affine transform mapping the <i>point</i> and the directions
   *         through it as the <i>projection</i> does.
   */
  constexpr Matrix linearize(const Projection& projection, Point point) {
    auto& p = projection;
    auto w = p.get_divisor(point);
    auto mapped = p.map(point);
    auto a = (p.m_a - mapped.m_x * p.m_wx) / w;
    auto b = (p.m_b - mapped.m_y * p.m_wx) / w;
    auto c = (p.m_c - mapped.m_x * p.m_wy) / w;
    auto d = (p.m_d - mapped.m_y * p.m_wy) / w;
    return Matrix(a, b, c, d, mapped.m_x - a * point.m_x - c * point.m_y,
      mapped.m_y - b * point.m_x - d * point.m_y);
  }

  /**
   * Applies a projective transform to a path the way Skia does: lines and the
   * control points of quadratic curves and arcs map exactly, and a cubic
   * curve is split into four before its control points are mapped.
   * @param path The path to transform, which must lie in front of the viewer.
   * @param projection The transform to apply.
   * @return The transformed <i>path</i>, keeping its fill rule.
   */
  Path transform(const Path& path, const Projection& projection);

  constexpr bool Projection::is_affine() const {
    return m_wx == 0 && m_wy == 0 && m_w0 == 1;
  }

  constexpr double Projection::get_divisor(Point point) const {
    return m_wx * point.m_x + m_wy * point.m_y + m_w0;
  }

  constexpr Point Projection::map(Point point) const {
    auto w = get_divisor(point);
    return Point((m_a * point.m_x + m_c * point.m_y + m_tx) / w,
      (m_b * point.m_x + m_d * point.m_y + m_ty) / w);
  }

  constexpr Rect Projection::map(const Rect& rect) const {
    auto top_left = map(rect.get_top_left());
    auto top_right = map(Point(rect.get_right(), rect.get_top()));
    auto bottom_left = map(Point(rect.get_left(), rect.get_bottom()));
    auto bottom_right = map(rect.get_bottom_right());
    auto left = std::min(
      {top_left.m_x, top_right.m_x, bottom_left.m_x, bottom_right.m_x});
    auto top = std::min(
      {top_left.m_y, top_right.m_y, bottom_left.m_y, bottom_right.m_y});
    auto right = std::max(
      {top_left.m_x, top_right.m_x, bottom_left.m_x, bottom_right.m_x});
    auto bottom = std::max(
      {top_left.m_y, top_right.m_y, bottom_left.m_y, bottom_right.m_y});
    return Rect(left, top, right - left, bottom - top);
  }

  inline std::ostream& operator <<(
      std::ostream& out, const Projection& projection) {
    return out << '(' << projection.m_a << ", " << projection.m_b << ", " <<
      projection.m_c << ", " << projection.m_d << ", " << projection.m_tx <<
      ", " << projection.m_ty << ", " << projection.m_wx << ", " <<
      projection.m_wy << ", " << projection.m_w0 << ')';
  }

  constexpr bool Projection::operator ==(const Projection& projection) const {
    return equals(m_a, projection.m_a) && equals(m_b, projection.m_b) &&
      equals(m_c, projection.m_c) && equals(m_d, projection.m_d) &&
      equals(m_tx, projection.m_tx) && equals(m_ty, projection.m_ty) &&
      equals(m_wx, projection.m_wx) && equals(m_wy, projection.m_wy) &&
      equals(m_w0, projection.m_w0);
  }
}

#endif

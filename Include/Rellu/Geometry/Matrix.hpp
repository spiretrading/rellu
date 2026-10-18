#ifndef RELLU_GEOMETRY_MATRIX_HPP
#define RELLU_GEOMETRY_MATRIX_HPP
#include <algorithm>
#include <cmath>
#include <optional>
#include <ostream>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"

namespace Rellu {

  /**
   * A 2D affine transform using Adobe XD's convention, mapping a point (x, y)
   * to (a * x + c * y + tx, b * x + d * y + ty). A default constructed Matrix
   * is the identity.
   */
  struct Matrix {

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

    constexpr double get_determinant() const;
    constexpr bool is_identity() const;
    constexpr Point map(Point point) const;

    /**
     * Maps a rect, returning the bounds of its four mapped corners.
     * @param rect The rect to map.
     * @return The smallest axis-aligned rect containing the mapped <i>rect</i>.
     */
    constexpr Rect map(const Rect& rect) const;

    bool operator ==(const Matrix&) const = default;
  };

  /**
   * Composes two transforms.
   * @param left The transform to apply second.
   * @param right The transform to apply first.
   * @return The transform that applies <i>right</i> and then <i>left</i>, so
   *         that <code>(left * right).map(p)</code> equals
   *         <code>left.map(right.map(p))</code>.
   */
  constexpr Matrix operator *(const Matrix& left, const Matrix& right) {
    return Matrix(left.m_a * right.m_a + left.m_c * right.m_b,
      left.m_b * right.m_a + left.m_d * right.m_b,
      left.m_a * right.m_c + left.m_c * right.m_d,
      left.m_b * right.m_c + left.m_d * right.m_d,
      left.m_a * right.m_tx + left.m_c * right.m_ty + left.m_tx,
      left.m_b * right.m_tx + left.m_d * right.m_ty + left.m_ty);
  }

  /**
   * Composes a transform in place, equivalent to
   * <code>left = left * right</code>.
   * @param left The transform to apply second, receiving the result.
   * @param right The transform to apply first.
   * @return <i>left</i>.
   */
  constexpr Matrix& operator *=(Matrix& left, const Matrix& right) {
    left = left * right;
    return left;
  }

  /**
   * Makes a translation.
   * @param x The horizontal offset.
   * @param y The vertical offset.
   */
  constexpr Matrix make_translation(double x, double y) {
    return Matrix(1, 0, 0, 1, x, y);
  }

  /**
   * Makes a scale about the origin.
   * @param x The horizontal scale factor.
   * @param y The vertical scale factor.
   */
  constexpr Matrix make_scale(double x, double y) {
    return Matrix(x, 0, 0, y, 0, 0);
  }

  /**
   * Makes a rotation about the origin. A positive angle turns the positive x
   * axis toward the positive y axis, which is clockwise on screen.
   * @param angle The angle in radians.
   */
  inline Matrix make_rotation(double angle) {
    auto cosine = std::cos(angle);
    auto sine = std::sin(angle);
    return Matrix(cosine, sine, -sine, cosine, 0, 0);
  }

  /**
   * Makes a rotation about a point. A positive angle turns the positive x
   * axis toward the positive y axis, which is clockwise on screen.
   * @param angle The angle in radians.
   * @param center The point that remains fixed.
   */
  inline Matrix make_rotation(double angle, Point center) {
    return make_translation(center.m_x, center.m_y) * make_rotation(angle) *
      make_translation(-center.m_x, -center.m_y);
  }

  /**
   * Inverts a transform.
   * @param matrix The transform to invert.
   * @return The transform undoing <i>matrix</i>, or <code>std::nullopt</code>
   *         if <i>matrix</i> is singular.
   */
  inline std::optional<Matrix> invert(const Matrix& matrix) {
    auto determinant = matrix.get_determinant();
    if(determinant == 0) {
      return std::nullopt;
    }
    auto inverse = Matrix(matrix.m_d / determinant, -matrix.m_b / determinant,
      -matrix.m_c / determinant, matrix.m_a / determinant,
      (matrix.m_c * matrix.m_ty - matrix.m_d * matrix.m_tx) / determinant,
      (matrix.m_b * matrix.m_tx - matrix.m_a * matrix.m_ty) / determinant);
    if(!std::isfinite(inverse.m_a) || !std::isfinite(inverse.m_b) ||
        !std::isfinite(inverse.m_c) || !std::isfinite(inverse.m_d) ||
        !std::isfinite(inverse.m_tx) || !std::isfinite(inverse.m_ty)) {
      return std::nullopt;
    }
    return inverse;
  }

  constexpr double Matrix::get_determinant() const {
    return m_a * m_d - m_b * m_c;
  }

  constexpr bool Matrix::is_identity() const {
    return *this == Matrix();
  }

  constexpr Point Matrix::map(Point point) const {
    return Point(m_a * point.m_x + m_c * point.m_y + m_tx,
      m_b * point.m_x + m_d * point.m_y + m_ty);
  }

  constexpr Rect Matrix::map(const Rect& rect) const {
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

  inline std::ostream& operator <<(std::ostream& out, const Matrix& matrix) {
    return out << '(' << matrix.m_a << ", " << matrix.m_b << ", " <<
      matrix.m_c << ", " << matrix.m_d << ", " << matrix.m_tx << ", " <<
      matrix.m_ty << ')';
  }
}

#endif

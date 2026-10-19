#ifndef RELLU_GEOMETRY_CONTOUR_HPP
#define RELLU_GEOMETRY_CONTOUR_HPP
#include <cstddef>
#include <optional>
#include <span>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"

namespace Rellu {

  /**
   * A point a contour passes through, with the control points of the curves
   * meeting at it. A control point equal to the point itself is no handle,
   * so that the segment on its side is straight unless the other end of the
   * segment has a handle.
   */
  struct Anchor {

    /** The point the contour passes through. */
    Point m_point;

    /** The control point of the segment arriving at the point. */
    Point m_in;

    /** The control point of the segment leaving the point. */
    Point m_out;

    bool operator ==(const Anchor&) const = default;
  };

  /**
   * A subpath described the way a pen tool edits it: a sequence of anchors
   * joined in order by straight lines or cubic Bezier curves, and by one
   * more segment from the last anchor back to the first when closed.
   */
  struct Contour {

    /** The anchors in order. */
    std::vector<Anchor> m_anchors;

    /** Whether a segment joins the last anchor back to the first. */
    bool m_is_closed = false;

    bool operator ==(const Contour&) const = default;
  };

  /** A point on a segment of a contour. */
  struct ContourPosition {

    /**
     * The index of the segment, which starts at the anchor of the same
     * index.
     */
    std::size_t m_segment;

    /** The curve parameter, from 0 at the segment's start to 1 at its end. */
    double m_t;

    /** The point. */
    Point m_point;
  };

  /**
   * Makes an anchor without handles.
   * @param point The point the contour passes through.
   */
  constexpr Anchor make_anchor(Point point) {
    return Anchor(point, point, point);
  }

  /**
   * Tests whether an anchor joins its segments smoothly: it has both handles
   * and they point in opposite directions.
   * @param anchor The anchor to test.
   */
  bool is_smooth(const Anchor& anchor);

  /**
   * Describes a path as contours, one per subpath that draws anything.
   * Quadratic curves become the equivalent cubic curves, and a closed
   * subpath whose last point returns to its start merges the two anchors.
   * @param path The path to describe.
   */
  std::vector<Contour> to_contours(const Path& path);

  /**
   * Builds the path that contours describe, drawing straight lines between
   * anchors without handles on the sides they join.
   * @param contours The contours, each becoming one subpath.
   * @param rule The fill rule of the path.
   */
  Path to_path(std::span<const Contour> contours, FillRule rule);

  /**
   * Applies an affine transform to every point of a contour.
   * @param contour The contour to transform.
   * @param matrix The transform to apply.
   */
  Contour transform(const Contour& contour, const Matrix& matrix);

  /**
   * Returns the number of segments of a contour: one less than its anchors
   * when open, as many as its anchors when closed.
   * @param contour The contour.
   */
  std::size_t get_segment_count(const Contour& contour);

  /**
   * Evaluates a point on a segment of a contour.
   * @param contour The contour.
   * @param segment The index of the segment.
   * @param t The curve parameter, from 0 to 1.
   */
  Point evaluate(const Contour& contour, std::size_t segment, double t);

  /**
   * Finds the point of a contour's segments nearest a point.
   * @param contour The contour to search.
   * @param point The point to approach.
   * @return The nearest point, or <code>std::nullopt</code> if the
   *         <i>contour</i> has no segment.
   */
  std::optional<ContourPosition> find_nearest(
    const Contour& contour, Point point);

  /**
   * Splits a segment of a contour in two at a curve parameter, inserting an
   * anchor there without changing the contour's shape.
   * @param contour The contour to change.
   * @param segment The index of the segment to split.
   * @param t The curve parameter, strictly between 0 and 1.
   * @return The index of the anchor inserted.
   */
  std::size_t split_segment(
    Contour& contour, std::size_t segment, double t);

  /**
   * Removes an anchor, joining its neighbours by a segment keeping their
   * outer handles, and leaving a closed contour closed.
   * @param contour The contour to change.
   * @param index The index of the anchor to remove.
   */
  void remove_anchor(Contour& contour, std::size_t index);

  /**
   * Makes an anchor smooth, giving it opposite handles along the direction
   * from its previous anchor to its next one, each a third of the distance
   * to the neighbour on its side, or makes it a corner by removing its
   * handles.
   * @param contour The contour to change.
   * @param index The index of the anchor.
   * @param is_smooth Whether the anchor becomes smooth rather than a corner.
   */
  void set_smooth(Contour& contour, std::size_t index, bool is_smooth);
}

#endif

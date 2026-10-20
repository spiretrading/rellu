#ifndef RELLU_GEOMETRY_POLYLINE_HPP
#define RELLU_GEOMETRY_POLYLINE_HPP
#include <vector>
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"

namespace Rellu {

  /** A sequence of points joined by straight lines. */
  struct Polyline {

    /**
     * The points in order; a closed polyline does not repeat its first point
     * at the end.
     */
    std::vector<Point> m_points;

    /** Whether a line joins the last point back to the first. */
    bool m_is_closed;

    bool operator ==(const Polyline&) const = default;
  };

  /**
   * Approximates a path with polylines, one per subpath, skipping subpaths
   * consisting only of a MOVE.
   * @param path The path to flatten.
   * @param tolerance The maximum distance, which must be positive, between
   *        the <i>path</i> and its approximation.
   * @return The polylines approximating each subpath of the <i>path</i>.
   */
  std::vector<Polyline> flatten(const Path& path, double tolerance);

  /**
   * Fits straight lines and cubic Bezier curves to polylines approximating
   * curves, keeping their ends and the corners where they turn more sharply
   * than the curves they approximate can, as Adobe XD's outlined strokes
   * are made of curves rather than polygons.
   * @param polylines The polylines.
   * @param tolerance The maximum distance, which must be positive, between
   *        the polylines and the curves they approximate, and between the
   *        polylines and the curves fitted to them.
   * @return The path with a subpath fitted to each polyline, closed if the
   *         polyline is.
   */
  Path fit_curves(const std::vector<Polyline>& polylines, double tolerance);
}

#endif

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
}

#endif

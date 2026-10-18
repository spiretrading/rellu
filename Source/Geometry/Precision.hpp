#ifndef RELLU_GEOMETRY_PRECISION_HPP
#define RELLU_GEOMETRY_PRECISION_HPP
#include "Rellu/Geometry/Path.hpp"

namespace Rellu::Details {

  /** The largest magnitude of a coordinate Clipper2 can operate on. */
  inline constexpr auto MAXIMUM_COORDINATE = 1e20;

  /** Specifies how finely Clipper2 approximates a region. */
  struct Precision {

    /** The number of decimal places coordinates are rounded to. */
    int m_digits;

    /**
     * The maximum distance between curves and their approximation, no finer
     * than the rounding of coordinates.
     */
    double m_tolerance;
  };

  /**
   * Returns the largest magnitude of a path's coordinates.
   * @param path The path to measure.
   * @return The largest magnitude of the <i>path</i>'s coordinates, infinity
   *         if any is not finite, or zero if it has none.
   */
  double get_extent(const Path& path);

  /**
   * Returns a path without the subpaths having a coordinate that is not
   * finite or whose magnitude exceeds MAXIMUM_COORDINATE.
   * @param path The path to filter.
   * @return The <i>path</i>'s remaining subpaths, keeping its fill rule.
   */
  Path drop_unrepresentable(const Path& path);

  /**
   * Returns the precision at which Clipper2 approximates a region,
   * coarsened as needed to represent large coordinates.
   * @param tolerance The maximum distance, which must be positive, between
   *        the region and its approximation.
   * @param extent The largest magnitude of a coordinate, which must not
   *        exceed three times MAXIMUM_COORDINATE.
   * @return The precision satisfying the <i>tolerance</i> where the
   *         <i>extent</i> permits.
   */
  Precision get_precision(double tolerance, double extent);
}

#endif

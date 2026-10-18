#ifndef RELLU_GEOMETRY_STROKE_HPP
#define RELLU_GEOMETRY_STROKE_HPP
#include <vector>
#include "Rellu/Geometry/Path.hpp"

namespace Rellu {

  /** Lists where a stroke lies relative to the outline it follows. */
  enum class StrokeAlignment : unsigned char {

    /** Centered on the outline. */
    CENTER,

    /**
     * Within the region a closed subpath fills, centered on an open
     * subpath.
     */
    INSIDE,

    /**
     * Outside the region a closed subpath fills, centered on an open
     * subpath.
     */
    OUTSIDE
  };

  /** Lists the shapes ending an open subpath or a dash. */
  enum class StrokeCap : unsigned char {

    /** Ends flush with the end point. */
    BUTT,

    /** Ends with a semicircle centered on the end point. */
    ROUND,

    /** Ends with a half square centered on the end point. */
    SQUARE
  };

  /** Lists the shapes joining two segments at a corner. */
  enum class StrokeJoin : unsigned char {

    /**
     * Extends both edges to meet at a point, falling back to BEVEL when
     * the miter limit is exceeded.
     */
    MITER,

    /** Joins with a circular arc centered on the corner. */
    ROUND,

    /** Joins with a straight line across the corner. */
    BEVEL
  };

  /** Describes how to stroke a path, using Adobe XD's conventions. */
  struct StrokeGeometry {

    /** The width of the stroke. */
    double m_width;

    /** Where the stroke lies relative to the path. */
    StrokeAlignment m_alignment;

    /** The shape ending open subpaths and dashes. */
    StrokeCap m_cap;

    /** The shape joining segments at corners. */
    StrokeJoin m_join;

    /**
     * The largest ratio of a miter's length to the width of the stroke,
     * as in SVG, beyond which a MITER join is drawn as a BEVEL.
     */
    double m_miter_limit;

    /**
     * The alternating lengths of dashes and gaps, repeated twice if odd in
     * count; empty, negative or all zero lengths draw a solid stroke.
     */
    std::vector<double> m_dashes;

    /** The distance into the dash pattern at which each subpath starts. */
    double m_dash_offset;

    bool operator ==(const StrokeGeometry&) const = default;
  };

  /**
   * Returns the region a stroke covers, so that it can be drawn by filling.
   * Subpaths consisting of a single point are drawn as their caps, oriented
   * along the x axis.
   * @param path The path to stroke, whose fill rule decides the region
   *        INSIDE and OUTSIDE strokes are aligned to.
   * @param stroke The stroke to outline.
   * @param tolerance The maximum distance, which must be positive, between
   *        the curves of the <i>path</i> and the arcs of the <i>stroke</i>
   *        and their approximation.
   * @return A Path using the non-zero fill rule whose closed polygonal
   *         subpaths do not overlap, with outer boundaries running clockwise
   *         on screen and holes running counterclockwise, or an empty Path
   *         if the <i>stroke</i>'s width is not positive.
   */
  Path outline_stroke(
    const Path& path, const StrokeGeometry& stroke, double tolerance);
}

#endif

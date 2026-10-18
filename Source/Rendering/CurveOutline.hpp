#ifndef RELLU_RENDERING_CURVE_OUTLINE_HPP
#define RELLU_RENDERING_CURVE_OUTLINE_HPP
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Stroke.hpp"

namespace Rellu {

  /**
   * An outline as Adobe XD's renderer represents it, in single precision
   * with conic sections, filled under the non-zero rule.
   */
  struct CurveOutline {

    /** Lists the operations building an outline. */
    enum class Verb : unsigned char {

      /** Starts a subpath at the next point. */
      MOVE,

      /** Draws a line to the next point. */
      LINE,

      /** Draws a quadratic curve through the next two points. */
      QUADRATIC,

      /**
       * Draws a conic section through the next two points, weighted by the
       * next weight.
       */
      CONIC,

      /** Draws a cubic curve through the next three points. */
      CUBIC,

      /** Closes the subpath. */
      CLOSE
    };

    /** A point in single precision. */
    struct Vertex {

      /** The x coordinate. */
      float m_x;

      /** The y coordinate. */
      float m_y;
    };

    /** The operations in order. */
    std::vector<Verb> m_verbs;

    /** The points the operations consume, in order. */
    std::vector<Vertex> m_points;

    /** The weights of the conic sections, in order. */
    std::vector<float> m_weights;
  };

  /**
   * Returns the outline in pixels that Adobe XD's renderer fills to draw a
   * stroke along a line, stroking the line in its own coordinates to a
   * precision suited to the transform.
   * @param line The line.
   * @param matrix The transform from the line's coordinates to pixels.
   * @param width The width of the stroke in the line's coordinates.
   * @param cap The shape ending open subpaths.
   * @param join The shape joining segments at corners.
   * @param miter_limit The largest ratio of a miter's length to the width.
   */
  CurveOutline stroke_outline(const Path& line, const Matrix& matrix,
    double width, StrokeCap cap, StrokeJoin join, double miter_limit);
}

#endif

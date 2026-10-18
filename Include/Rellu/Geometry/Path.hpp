#ifndef RELLU_GEOMETRY_PATH_HPP
#define RELLU_GEOMETRY_PATH_HPP
#include <array>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"

namespace Rellu {

  /** Lists the rules deciding which regions a path's outline fills. */
  enum class FillRule : unsigned char {

    /** A point is inside when the outline winds around it a non-zero net. */
    NON_ZERO,

    /** A point is inside when the outline crosses around it an odd count. */
    EVEN_ODD
  };

  /** Lists the kinds of segments making up a Path. */
  enum class SegmentType : unsigned char {

    /** Starts a new subpath at the first point. */
    MOVE,

    /** A straight line to the first point. */
    LINE,

    /**
     * A quadratic Bezier curve with the first point as its control point,
     * ending at the second point.
     */
    QUADRATIC,

    /**
     * A cubic Bezier curve with the first two points as its control points,
     * ending at the third point.
     */
    CUBIC,

    /**
     * A straight line back to the first point, which is the start of the
     * subpath, closing it.
     */
    CLOSE
  };

  /** A single segment of a Path. */
  struct Segment {

    /** The kind of segment. */
    SegmentType m_type;

    /** The segment's points, as given by its type; unused points are zero. */
    std::array<Point, 3> m_points;

    bool operator ==(const Segment&) const = default;
  };

  /**
   * An outline made of a sequence of subpaths, each starting with a MOVE
   * segment and continuing with lines and curves, optionally ending with a
   * CLOSE segment.
   */
  class Path {
    public:

      /** Constructs an empty Path using the non-zero fill rule. */
      Path() noexcept;

      /**
       * Constructs an empty Path.
       * @param rule The fill rule to use.
       */
      explicit Path(FillRule rule) noexcept;

      FillRule get_fill_rule() const;
      void set_fill_rule(FillRule rule);

      /** Returns the segments, where every subpath begins with a MOVE. */
      const std::vector<Segment>& get_segments() const;

      /** Returns <code>true</code> iff there are no segments. */
      bool is_empty() const;

      /**
       * Starts a new subpath, replacing the previous subpath if it consists
       * only of a MOVE.
       * @param point The start of the new subpath.
       */
      void move_to(Point point);

      /**
       * Adds a straight line, starting a new subpath at the current point if
       * none is open. The current point is the origin for an empty Path and
       * the start of the most recently closed subpath after a close.
       * @param point The end of the line.
       */
      void line_to(Point point);

      /**
       * Adds a quadratic Bezier curve, starting a new subpath at the current
       * point if none is open.
       * @param control The control point.
       * @param end The end of the curve.
       */
      void quadratic_to(Point control, Point end);

      /**
       * Adds a cubic Bezier curve, starting a new subpath at the current
       * point if none is open.
       * @param first The control point nearest the start.
       * @param second The control point nearest the end.
       * @param end The end of the curve.
       */
      void cubic_to(Point first, Point second, Point end);

      /** Closes the open subpath, doing nothing if no subpath is open. */
      void close();

      bool operator ==(const Path&) const = default;

    private:
      FillRule m_fill_rule;
      std::vector<Segment> m_segments;
      Point m_start;
      Point m_current;
      bool m_is_open;

      void open();
  };

  /**
   * Applies an affine transform to every point of a path.
   * @param path The path to transform.
   * @param matrix The transform to apply.
   * @return The transformed <i>path</i>, keeping its fill rule.
   */
  Path transform(const Path& path, const Matrix& matrix);

  /**
   * Returns the tight bounds of a path, using the extrema of its curves
   * rather than their control points. A subpath consisting only of a MOVE
   * does not contribute.
   * @param path The path to measure.
   * @return The bounds of the <i>path</i>, or an all-zero Rect if it draws
   *         nothing.
   */
  Rect get_bounds(const Path& path);

  /**
   * Returns the tight bounds of a path after an affine transform.
   * @param path The path to measure.
   * @param matrix The transform to apply to the <i>path</i>.
   * @return The bounds of the transformed <i>path</i>.
   */
  Rect get_bounds(const Path& path, const Matrix& matrix);

  /**
   * Tests whether a point lies inside the region a path fills, treating
   * every subpath as closed.
   * @param path The path to test.
   * @param point The point to test.
   * @param rule The fill rule deciding which regions are inside.
   * @return <code>true</code> iff the <i>point</i> is inside.
   */
  bool contains(const Path& path, Point point, FillRule rule);

  /**
   * Tests whether a point lies inside the region a path fills under the
   * path's own fill rule, treating every subpath as closed.
   * @param path The path to test.
   * @param point The point to test.
   * @return <code>true</code> iff the <i>point</i> is inside.
   */
  bool contains(const Path& path, Point point);
}

#endif

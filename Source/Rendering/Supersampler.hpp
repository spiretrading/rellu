#ifndef RELLU_RENDERING_SUPERSAMPLER_HPP
#define RELLU_RENDERING_SUPERSAMPLER_HPP
#include <cstdint>
#include <memory>
#include <vector>
#include <QRect>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "CurveOutline.hpp"

namespace Rellu {

  /** Lists the ways Adobe XD's renderer antialiases a path. */
  enum class Antialiasing : unsigned char {

    /** The exact area covered, as for axis aligned rectangles. */
    EXACT,

    /** The exact area of the lines Supersampler::trace approximates it with. */
    ANALYTIC,

    /** The 4x4 sample points per pixel Supersampler::rasterize counts. */
    SUPERSAMPLED
  };

  /**
   * Rasterizes paths into 8-bit coverage the way Adobe XD's renderer does
   * for paths it doesn't cover analytically: curves are stepped into lines
   * in fixed point, quarter arcs are drawn as conics, and each pixel counts
   * which of its 4x4 sample points lie inside.
   */
  class Supersampler {
    public:

      /** A line of an outline traced in pixels. */
      struct Line {

        /** The start of the line. */
        Point m_start;

        /** The end of the line. */
        Point m_end;
      };

      Supersampler();

      ~Supersampler();

      /**
       * Returns how XD's renderer antialiases a path: an axis aligned
       * rectangle exactly, a path estimated to cross each row of pixels often
       * for its size by supersampling, and any other path analytically.
       * @param path The path.
       * @param matrix The transform from the path's coordinates to pixels.
       */
      static Antialiasing get_antialiasing(
        const Path& path, const Matrix& matrix);

      /**
       * Returns how XD's renderer antialiases an outline in pixels.
       * @param outline The outline.
       */
      static Antialiasing get_antialiasing(const CurveOutline& outline);

      /**
       * Returns whether XD's renderer draws a stroke along a line as a frame
       * whose exact area it covers, which is when the line is a closed axis
       * aligned rectangle with mitered corners.
       * @param line The line the stroke follows.
       * @param join The shape joining the line's segments.
       * @param miter_limit The largest ratio of a miter's length to the width.
       * @param matrix The transform from the line's coordinates to pixels.
       */
      static bool is_frame(const Path& line, StrokeJoin join,
        double miter_limit, const Matrix& matrix);

      /**
       * Returns whether XD's renderer draws a stroke as a hairline, which is
       * when it is at most a pixel wide.
       * @param width The width of the stroke.
       * @param matrix The transform from the stroke's coordinates to pixels.
       * @param coverage Receives the fraction of a pixel the stroke spans,
       *        which scales the hairline's coverage.
       */
      static bool is_hairline(
        double width, const Matrix& matrix, double& coverage);

      /**
       * Rasterizes a path, replacing the coverage of a previous path.
       * @param path The path.
       * @param matrix The transform from the path's coordinates to pixels.
       * @param clip The pixels coverage is computed for.
       * @param is_clip Whether the coverage clips other content, which XD
       *        accumulates in runs rather than a mask.
       */
      void rasterize(const Path& path, const Matrix& matrix, const QRect& clip,
        bool is_clip);

      /**
       * Rasterizes an outline in pixels, replacing the coverage of a previous
       * path.
       * @param outline The outline.
       * @param clip The pixels coverage is computed for.
       * @param is_clip Whether the coverage clips other content.
       */
      void rasterize(
        const CurveOutline& outline, const QRect& clip, bool is_clip);

      /**
       * Rasterizes a hairline along a line, replacing the coverage of a
       * previous path: one pixel wide, its coverage split between the two
       * pixels nearest the line along each row or column it crosses.
       * @param line The line.
       * @param matrix The transform from the line's coordinates to pixels.
       * @param cap The shape ending the line's open subpaths.
       * @param clip The pixels coverage is computed for.
       */
      void rasterize_hairline(const Path& line, const Matrix& matrix,
        StrokeCap cap, const QRect& clip);

      /**
       * Traces the lines XD's renderer approximates a path with when it
       * computes the path's exact area: curves are stepped into lines in
       * fixed point and the ends of the lines are rounded vertically to a
       * quarter of a pixel.
       * @param path The path.
       * @param matrix The transform from the path's coordinates to pixels.
       * @param lines Receives the lines, replacing its contents.
       */
      void trace(const Path& path, const Matrix& matrix,
        std::vector<Line>& lines);

      /**
       * Traces the lines XD's renderer approximates an outline in pixels with
       * when it computes the outline's exact area.
       * @param outline The outline.
       * @param lines Receives the lines, replacing its contents.
       */
      void trace(const CurveOutline& outline, std::vector<Line>& lines);

      /** Returns the pixels the coverage spans. */
      const QRect& get_bounds() const;

      /**
       * Returns the coverage of each pixel of the bounds in rows from the
       * top, 255 being full coverage.
       */
      const std::vector<std::uint8_t>& get_coverage() const;

    private:
      struct Buffers;
      std::unique_ptr<Buffers> m_buffers;

      Supersampler(const Supersampler&) = delete;
      Supersampler& operator =(const Supersampler&) = delete;
  };
}

#endif

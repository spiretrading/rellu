#ifndef RELLU_TEXT_TEXT_RASTERIZER_HPP
#define RELLU_TEXT_TEXT_RASTERIZER_HPP
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Text/TextLayout.hpp"

namespace Rellu {

  /** The coverage of text over a rectangle of device pixels. */
  struct CoverageMask {

    /** The column of the leftmost pixel. */
    int m_left = 0;

    /** The row of the topmost pixel. */
    int m_top = 0;

    /** The number of columns. */
    int m_width = 0;

    /** The number of rows. */
    int m_height = 0;

    /** The coverage of each pixel row by row, in sixteenths. */
    std::vector<std::uint8_t> m_coverage;
  };

  /** Text of one color rasterized into device pixels. */
  struct TextRaster {

    /** The coverage of the text. */
    CoverageMask m_mask;

    /** The color of the text. */
    Color m_color = Color(0, 0, 0, 1);
  };

  /** Lists the methods of rasterizing glyphs. */
  enum class GlyphRasterization : unsigned char {

    /**
     * DirectWrite's natural rendering in grayscale, which is how
     * Adobe XD draws text on Windows. Available only on Windows.
     */
    DIRECT_WRITE,

    /**
     * FreeType's TrueType hinting sampled at 4x4 points per pixel, which
     * approximates DIRECT_WRITE on every platform.
     */
    FREE_TYPE
  };

  /**
   * Rasterizes text into device pixels the way Adobe XD does on Windows:
   * every glyph positioned to a quarter of a pixel along a horizontal
   * baseline and to a whole pixel across it, to whole pixels along and
   * across a vertical baseline, hinted vertically and sampled at 4x4 points
   * per pixel, and composited over the glyphs before it. A TextRasterizer is
   * safe to use from multiple threads.
   */
  class TextRasterizer {
    public:

      /** The default number of bytes of glyph coverage kept for reuse. */
      static constexpr auto DEFAULT_CAPACITY = std::size_t(32) << 20;

      /**
       * Constructs a TextRasterizer using DIRECT_WRITE on Windows and
       * FREE_TYPE elsewhere.
       */
      TextRasterizer();

      /**
       * Constructs a TextRasterizer.
       * @param rasterization The method of rasterizing glyphs, replaced by
       *        FREE_TYPE where it is unavailable.
       */
      explicit TextRasterizer(GlyphRasterization rasterization);

      /**
       * Constructs a TextRasterizer.
       * @param rasterization The method of rasterizing glyphs, replaced by
       *        FREE_TYPE where it is unavailable.
       * @param capacity The number of bytes of glyph coverage kept for
       *        reuse, beyond which the glyphs least recently used are
       *        discarded.
       */
      TextRasterizer(GlyphRasterization rasterization, std::size_t capacity);

      ~TextRasterizer();

      /** Returns the method of rasterizing glyphs. */
      GlyphRasterization get_rasterization() const;

      /** Returns the number of bytes of glyph coverage kept for reuse. */
      std::size_t get_capacity() const;

      /**
       * Returns the number of bytes of glyph coverage kept, which is at most
       * the capacity between calls to rasterize.
       */
      std::size_t get_cache_size() const;

      /**
       * Tests whether text can be rasterized through a transform, which must
       * be invertible.
       * @param transform The transform from the text's space to device
       *        pixels.
       */
      static bool is_supported(const Matrix& transform);

      /**
       * Tests whether a glyph is too large to rasterize, in which case Adobe
       * XD fills its outline like any other path.
       * @param glyph The glyph to test.
       * @param transform The transform from the glyph's space to device
       *        pixels.
       */
      static bool is_outlined(
        const PlacedGlyph& glyph, const Matrix& transform);

      /**
       * Returns the position in device pixels a glyph's origin is drawn at.
       * @param glyph The glyph.
       * @param transform The supported transform from the glyph's space to
       *        device pixels.
       */
      static Point get_origin(const PlacedGlyph& glyph, const Matrix& transform);

      /**
       * Rasterizes the glyphs of a layout that rasterize would rasterize and
       * keep for reuse but has not, so that rasterizing the layout takes
       * less time, stopping once a deadline passes.
       * @param layout The layout whose glyphs are rasterized.
       * @param transform The transform from the layout's space to device
       *        pixels.
       * @param deadline The time past which no further glyph is rasterized,
       *        after at least one.
       * @return <code>true</code> iff every such glyph is kept for reuse.
       */
      bool prepare(const TextLayout& layout, const Matrix& transform,
        std::chrono::steady_clock::time_point deadline) const;

      /**
       * Rasterizes a layout's decorations and the glyphs that are neither
       * outlined nor color glyphs.
       * @param layout The layout to rasterize.
       * @param transform The supported transform from the layout's space to
       *        device pixels.
       * @return The rasters in the order they are drawn: those of each color
       *         in order of the color's first appearance, glyphs before
       *         decorations, with glyphs overlapping one another in separate
       *         rasters.
       */
      std::vector<TextRaster> rasterize(
        const TextLayout& layout, const Matrix& transform) const;

    private:
      struct Backend;
      std::unique_ptr<Backend> m_backend;

      TextRasterizer(const TextRasterizer&) = delete;
      TextRasterizer& operator =(const TextRasterizer&) = delete;
  };

  /**
   * Returns the opacity Adobe XD paints a pixel of text with.
   * @param coverage The pixel's coverage in sixteenths.
   * @param color The color of the text.
   */
  double get_text_opacity(int coverage, Color color);

  /** Returns the TextRasterizer shared by default. */
  const TextRasterizer& get_default_text_rasterizer();
}

#endif

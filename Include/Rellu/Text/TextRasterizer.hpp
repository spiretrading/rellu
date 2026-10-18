#ifndef RELLU_TEXT_TEXT_RASTERIZER_HPP
#define RELLU_TEXT_TEXT_RASTERIZER_HPP
#include <cstdint>
#include <memory>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
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
   * every glyph positioned to a quarter of a pixel along its baseline and to
   * a whole pixel across it, hinted vertically and sampled at 4x4 points per
   * pixel, with the coverage of overlapping glyphs summed. A TextRasterizer
   * is safe to use from multiple threads.
   */
  class TextRasterizer {
    public:

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

      ~TextRasterizer();

      /** Returns the method of rasterizing glyphs. */
      GlyphRasterization get_rasterization() const;

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
       * Rasterizes a layout's decorations and the glyphs that are not
       * outlined.
       * @param layout The layout to rasterize.
       * @param transform The supported transform from the layout's space to
       *        device pixels.
       * @return One raster per color in order of each color's first
       *         appearance, glyphs before decorations.
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

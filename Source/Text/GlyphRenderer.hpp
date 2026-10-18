#ifndef RELLU_TEXT_GLYPH_RENDERER_HPP
#define RELLU_TEXT_GLYPH_RENDERER_HPP
#include <cstdint>
#include <memory>
#include <vector>
#include "Rellu/Text/TextRasterizer.hpp"
#include "Rellu/Text/Typeface.hpp"

namespace Rellu::Details {

  /** Renders single glyphs into coverage masks. */
  class GlyphRenderer {
    public:
      virtual ~GlyphRenderer() = default;

      /**
       * Renders a glyph.
       * @param typeface The glyph's face.
       * @param glyph The glyph's index.
       * @param size The size of the em in pixels.
       * @param transform The transform from the glyph's space in pixels,
       *        with its origin on the baseline, to device pixels.
       * @param mask Receives the glyph's coverage.
       * @return <code>true</code> iff the <i>typeface</i> is supported.
       */
      virtual bool render(const std::shared_ptr<const Typeface>& typeface,
        std::uint32_t glyph, double size, const Matrix& transform,
        CoverageMask& mask) = 0;

    protected:

      /** Constructs a GlyphRenderer. */
      GlyphRenderer() = default;

    private:
      GlyphRenderer(const GlyphRenderer&) = delete;
      GlyphRenderer& operator =(const GlyphRenderer&) = delete;
  };

  /** Makes a GlyphRenderer using FreeType. */
  std::unique_ptr<GlyphRenderer> make_free_type_renderer();

  /**
   * Makes a GlyphRenderer using DirectWrite.
   * @return The renderer, or a null pointer where DirectWrite is unavailable.
   */
  std::unique_ptr<GlyphRenderer> make_direct_write_renderer();
}

#endif

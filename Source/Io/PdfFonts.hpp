#ifndef RELLU_IO_PDF_FONTS_HPP
#define RELLU_IO_PDF_FONTS_HPP
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Rellu/Text/Typeface.hpp"
#include "PdfFile.hpp"

namespace Rellu {

  /** A glyph as a PDF font resource encodes it. */
  struct PdfGlyph {

    /** The number of the font object. */
    int m_font;

    /** The glyph's code within the font. */
    std::uint16_t m_code;

    /** Whether the font encodes glyphs in two bytes. */
    bool m_is_multibyte;

    /** The glyph's advance in ems. */
    double m_advance;

    /** Whether PDF viewers agree on the advances of the font's glyphs. */
    bool m_is_agreed;
  };

  /** Embeds the fonts of the glyphs drawn as Adobe XD's PDF writer does. */
  class PdfFonts {
    public:

      /**
       * Constructs a PdfFonts.
       * @param file The file the fonts are written to.
       */
      explicit PdfFonts(PdfFile& file);

      ~PdfFonts();

      /**
       * Returns the font resource drawing a glyph, reserving it on first
       * use, and records the glyph's use.
       * @param typeface The glyph's face.
       * @param index The glyph's index.
       * @return The glyph, or none if the face can not draw it.
       */
      std::optional<PdfGlyph> use(
        const std::shared_ptr<const Typeface>& typeface, std::uint32_t index);

      /** Writes every font used, in the order they were reserved. */
      void finish();

    private:
      struct Face;
      struct Font {
        int m_id;
        Face* m_face;
        std::uint16_t m_first;
        std::uint16_t m_last;
        bool m_is_type3;
        std::vector<bool> m_usage;
      };
      PdfFile* m_file;
      std::unordered_map<const Typeface*, std::unique_ptr<Face>> m_faces;
      std::map<std::pair<const Typeface*, std::uint16_t>, Font> m_fonts;
      std::unordered_map<const Face*, int> m_descriptors;

      PdfFonts(const PdfFonts&) = delete;
      PdfFonts& operator =(const PdfFonts&) = delete;
      Face& get_face(const std::shared_ptr<const Typeface>& typeface);
      void write_type0(const Font& font);
      void write_type3(const Font& font);
      int get_type3_descriptor(Face& face);
  };
}

#endif

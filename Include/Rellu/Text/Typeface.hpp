#ifndef RELLU_TEXT_TYPEFACE_HPP
#define RELLU_TEXT_TYPEFACE_HPP
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"

struct hb_blob_t;
struct hb_face_t;
struct hb_font_t;

namespace Rellu {

  /**
   * The vertical metrics of a Typeface, in ems. Distances above the baseline
   * are positive.
   */
  struct TypefaceMetrics {

    /** The ascender used for line spacing. */
    double m_ascender = 0;

    /** The descender used for line spacing, negative below the baseline. */
    double m_descender = 0;

    /** The extra space between lines. */
    double m_line_gap = 0;

    /** The height above the baseline outside of which glyphs are clipped. */
    double m_clipping_ascent = 0;

    /** The depth below the baseline outside of which glyphs are clipped. */
    double m_clipping_descent = 0;

    /**
     * The ascent lines are spaced with: the typographic ascender if the font
     * asks for it to be used, otherwise the height above the baseline outside
     * of which glyphs are clipped.
     */
    double m_line_ascent = 0;

    /**
     * The descent lines are spaced with, positive below the baseline: the
     * typographic descender if the font asks for it to be used, otherwise
     * the depth below the baseline outside of which glyphs are clipped.
     */
    double m_line_descent = 0;

    /** The position of the top of an underline. */
    double m_underline_offset = 0;

    /** The thickness of an underline. */
    double m_underline_thickness = 0;

    /** The position of the top of a strikethrough. */
    double m_strikeout_offset = 0;

    /** The thickness of a strikethrough. */
    double m_strikeout_thickness = 0;

    /** The raise of the baseline of superscript text. */
    double m_superscript_offset = 0;

    /** The size of superscript text relative to the em. */
    double m_superscript_size = 0;

    /** The drop of the baseline of subscript text, positive downward. */
    double m_subscript_offset = 0;

    /** The size of subscript text relative to the em. */
    double m_subscript_size = 0;
  };

  /** A glyph produced by shaping text, positioned in ems. */
  struct ShapedGlyph {

    /** The glyph's index within its Typeface. */
    std::uint32_t m_index;

    /** The index of the first UTF-16 code unit the glyph represents. */
    std::size_t m_cluster;

    /** The distance the pen advances after the glyph. */
    double m_advance;

    /**
     * The displacement of the glyph from the pen position, with y growing
     * downward.
     */
    Point m_offset;
  };

  /** Lists the properties identifying a font face. */
  struct TypefaceNames {

    /** The family name used by legacy applications. */
    std::string m_family;

    /** The style name used by legacy applications. */
    std::string m_style;

    /** The typographic family name, or the family name if absent. */
    std::string m_typographic_family;

    /** The typographic style name, or the style name if absent. */
    std::string m_typographic_style;

    /** The PostScript name. */
    std::string m_postscript_name;

    /** The weight in the range [1, 1000], where 400 is regular. */
    double m_weight = 400;

    /** Whether the face is italic or oblique. */
    bool m_is_italic = false;
  };

  /**
   * A font face loaded from a font file, which shapes text and outlines
   * glyphs in ems. A Typeface is safe to use from multiple threads.
   */
  class Typeface {
    public:

      /**
       * Loads a face from a font file.
       * @param path The path of the font file.
       * @param index The index of the face within a font collection.
       * @param instance The named instance of a variable font to select, or
       *        none for the default instance.
       * @return The Typeface, or a null pointer if the file holds no such
       *         face.
       */
      static std::shared_ptr<Typeface> load(const std::filesystem::path& path,
        unsigned int index = 0,
        std::optional<unsigned int> instance = std::nullopt);

      /**
       * Loads a face from font data held in memory.
       * @param data The contents of a font file.
       * @param index The index of the face within a font collection.
       * @param instance The named instance of a variable font to select, or
       *        none for the default instance.
       * @return The Typeface, or a null pointer if the data holds no such
       *         face.
       */
      static std::shared_ptr<Typeface> load(
        std::shared_ptr<const std::vector<char>> data, unsigned int index = 0,
        std::optional<unsigned int> instance = std::nullopt);

      ~Typeface();

      /** Returns the path of the font file, empty if loaded from memory. */
      const std::filesystem::path& get_path() const;

      /** Returns the index of the face within its font file. */
      unsigned int get_index() const;

      /** Returns the named instance selected, if any. */
      std::optional<unsigned int> get_instance() const;

      /** Returns the contents of the font file. */
      std::string_view get_data() const;

      /**
       * Returns the variation axis values selected, each an OpenType axis tag
       * and its value in design units.
       */
      std::vector<std::pair<std::uint32_t, double>> get_variations() const;

      /** Returns the names identifying this face. */
      const TypefaceNames& get_names() const;

      /** Returns the vertical metrics. */
      const TypefaceMetrics& get_metrics() const;

      /**
       * Returns whether this face maps a character to a glyph.
       * @param character The Unicode code point.
       */
      bool has_glyph(char32_t character) const;

      /**
       * Shapes a span of text, applying kerning and the font's default
       * features.
       * @param text The text containing the span, providing context.
       * @param start The index of the span's first UTF-16 code unit.
       * @param length The number of UTF-16 code units in the span.
       * @return The glyphs in visual order with clusters indexing <i>text</i>.
       */
      std::vector<ShapedGlyph> shape(
        std::u16string_view text, std::size_t start, std::size_t length) const;

      /**
       * Returns the outline of a glyph in ems, with the origin on the
       * baseline at the pen position and y growing downward.
       * @param index The glyph's index.
       */
      const Path& get_outline(std::uint32_t index) const;

    private:
      std::shared_ptr<const void> m_data;
      std::filesystem::path m_path;
      unsigned int m_index;
      std::optional<unsigned int> m_instance;
      hb_blob_t* m_blob;
      hb_face_t* m_face;
      hb_font_t* m_font;
      double m_scale;
      TypefaceNames m_names;
      TypefaceMetrics m_metrics;
      mutable std::mutex m_mutex;
      mutable std::unordered_map<std::uint32_t, Path> m_outlines;

      Typeface(std::shared_ptr<const void> data, std::filesystem::path path,
        hb_blob_t* blob, unsigned int index,
        std::optional<unsigned int> instance);
      Typeface(const Typeface&) = delete;
      Typeface& operator =(const Typeface&) = delete;
  };
}

#endif

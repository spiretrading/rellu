#ifndef RELLU_TEXT_TEXT_LAYOUT_HPP
#define RELLU_TEXT_TEXT_LAYOUT_HPP
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Text.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Text/Typeface.hpp"

namespace Rellu {

  /** A glyph placed in the local space of a text node. */
  struct PlacedGlyph {

    /** The face the glyph belongs to. */
    std::shared_ptr<const Typeface> m_typeface;

    /** The glyph's index within its face. */
    std::uint32_t m_index = 0;

    /** The glyph's origin on the baseline. */
    Point m_origin = Point(0, 0);

    /** The size of the em. */
    double m_size = 0;

    /** The color the glyph is filled with. */
    Color m_color = Color(0, 0, 0, 1);
  };

  /** An underline or strikethrough. */
  struct TextDecoration {

    /** The region the line covers. */
    Rect m_rect = Rect(0, 0, 0, 0);

    /** The color the line is filled with. */
    Color m_color = Color(0, 0, 0, 1);
  };

  /** The glyphs and decorations of a text node in its local space. */
  struct TextLayout {

    /** The glyphs in drawing order. */
    std::vector<PlacedGlyph> m_glyphs;

    /** The underlines and strikethroughs in drawing order. */
    std::vector<TextDecoration> m_decorations;

    /** The region outside of which nothing is drawn, or none. */
    std::optional<Rect> m_clip;
  };

  /** A region of a text node's drawing filled with one color. */
  struct TextFill {

    /** The outline of the region in the node's local space. */
    Path m_path;

    /** The color the region is filled with. */
    Color m_color = Color(0, 0, 0, 1);
  };

  /** A line of a text node laid out as by lay_out, with its caret positions. */
  struct LineGeometry {

    /** The index of the line's first UTF-16 code unit. */
    std::size_t m_start = 0;

    /**
     * The index one past the line's last UTF-16 code unit, before any line
     * separator ending it.
     */
    std::size_t m_end = 0;

    /** Whether the line ends its paragraph. */
    bool m_is_paragraph_end = false;

    /** The y of the baseline in the node's local space. */
    double m_baseline = 0;

    /** The distance from the baseline to the top of the line. */
    double m_ascent = 0;

    /** The distance from the baseline to the bottom of the line. */
    double m_descent = 0;

    /**
     * The x in the node's local space of the caret before each UTF-16 code
     * unit from m_start through m_end.
     */
    std::vector<double> m_carets;
  };

  /**
   * Applies a case conversion to text.
   * @param text The text to convert.
   * @param transform The conversion to apply.
   * @return The converted <i>text</i>.
   */
  std::u16string apply_transform(
    std::u16string_view text, TextTransform transform);

  /**
   * Lays out a text node the way Adobe XD does, keeping the line breaks of
   * its stored layout, or breaking lines itself when it has none.
   * @param text The text node's payload.
   * @param library The fonts to resolve the text's fonts with.
   * @return The glyphs and decorations of the <i>text</i>.
   */
  TextLayout lay_out(const Text& text, const FontLibrary& library);

  /**
   * Measures a text node laid out as by lay_out: the frame of area text,
   * otherwise the boxes of its lines from ascent to descent, spanning the
   * frame's width for auto height text.
   * @param text The text node's payload.
   * @param library The fonts to resolve the text's fonts with.
   * @return The bounds in the node's local space.
   */
  Rect measure_text(const Text& text, const FontLibrary& library);

  /**
   * Computes the line breaks and run positions Adobe XD stores for a text
   * node laid out as by lay_out.
   * @param text The text node's payload.
   * @param library The fonts to resolve the text's fonts with.
   * @return The layout to store, the text's own if it has one.
   */
  StoredLayout store_layout(const Text& text, const FontLibrary& library);

  /**
   * Measures the lines of a text node laid out as by lay_out.
   * @param text The text node's payload.
   * @param library The fonts to resolve the text's fonts with.
   * @return The lines in order, at least one.
   */
  std::vector<LineGeometry> measure_lines(
    const Text& text, const FontLibrary& library);

  /**
   * Converts a TextLayout into filled regions, merging the glyphs and
   * decorations of each color, with each layer of a color glyph filled in
   * its own color.
   * @param layout The layout to convert.
   * @return The regions in the order they are drawn.
   */
  std::vector<TextFill> outline(const TextLayout& layout);
}

#endif

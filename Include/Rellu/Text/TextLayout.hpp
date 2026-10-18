#ifndef RELLU_TEXT_TEXT_LAYOUT_HPP
#define RELLU_TEXT_TEXT_LAYOUT_HPP
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
   * Converts a TextLayout into filled regions, merging the glyphs and
   * decorations of each color.
   * @param layout The layout to convert.
   * @return The regions in order of each color's first appearance.
   */
  std::vector<TextFill> outline(const TextLayout& layout);
}

#endif

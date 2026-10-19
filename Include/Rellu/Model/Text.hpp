#ifndef RELLU_MODEL_TEXT_HPP
#define RELLU_MODEL_TEXT_HPP
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Shared.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /** Identifies a typeface at a size. */
  struct Font {

    /** The family name, such as Roboto. */
    std::string m_family;

    /** The style name within the family, such as Medium. */
    std::string m_style;

    /** The PostScript name, such as Roboto-Medium. */
    std::string m_postscript_name;

    /** The size in document pixels. */
    double m_size = 12;

    bool operator ==(const Font& font) const;
  };

  /** Lists the case conversions applied to text when displayed. */
  enum class TextTransform : unsigned char {

    /** The text is displayed as written. */
    NONE,

    /** Every letter is displayed in upper case. */
    UPPERCASE,

    /** Every letter is displayed in lower case. */
    LOWERCASE,

    /** The first letter of every word is displayed in upper case. */
    TITLE_CASE
  };

  /** Lists the vertical placements of text relative to its baseline. */
  enum class TextScript : unsigned char {

    /** On the baseline. */
    NORMAL,

    /** Raised and reduced. */
    SUPERSCRIPT,

    /** Lowered and reduced. */
    SUBSCRIPT
  };

  /** The character level styling of a span of text. */
  struct TextStyle {

    /** The font. */
    Font m_font;

    /** The extra space between characters, in thousandths of an em. */
    double m_letter_spacing = 0;

    /** The color of the glyphs. */
    Color m_color = Color(0, 0, 0, 1);

    /** Whether the text is underlined. */
    bool m_is_underlined = false;

    /** Whether the text is struck through. */
    bool m_is_struck_through = false;

    /** The case conversion applied when displayed. */
    TextTransform m_transform = TextTransform::NONE;

    /** The placement relative to the baseline. */
    TextScript m_script = TextScript::NORMAL;

    bool operator ==(const TextStyle& style) const;
  };

  /** A span of text sharing one style. */
  struct TextRun {

    /** The length of the span in UTF-16 code units. */
    std::size_t m_length = 0;

    /** The style of the span. */
    TextStyle m_style;

    /** The character style the span is linked to, or nil if unlinked. */
    CharacterStyleId m_character_style;

    /** The URL the span links to, or empty if it links nowhere. */
    std::string m_hyperlink;

    bool operator ==(const TextRun&) const = default;
  };

  /** A text frame anchored at a point, where lines never wrap. */
  struct PositionedFrame {
    bool operator ==(const PositionedFrame&) const = default;
  };

  /** A text frame of fixed size, where lines wrap and overflow is clipped. */
  struct AreaFrame {

    /** The size of the frame. */
    Size m_size = Size(0, 0);

    bool operator ==(const AreaFrame&) const = default;
  };

  /** A text frame of fixed width whose height follows its content. */
  struct AutoHeightFrame {

    /** The width at which lines wrap. */
    double m_width = 0;

    bool operator ==(const AutoHeightFrame& frame) const;
  };

  /** The frame text is laid out in. */
  using TextFrame = std::variant<PositionedFrame, AreaFrame, AutoHeightFrame>;

  /** Lists the horizontal alignments of lines within a text frame. */
  enum class TextAlignment : unsigned char {

    /** Lines start at the left edge. */
    LEFT,

    /** Lines are centered. */
    CENTER,

    /** Lines end at the right edge. */
    RIGHT
  };

  /** The paragraph level styling of text. */
  struct ParagraphStyle {

    /** The alignment of lines. */
    TextAlignment m_alignment = TextAlignment::LEFT;

    /** The distance between baselines, or none for automatic spacing. */
    std::optional<double> m_line_height;

    /** The extra space after each paragraph. */
    double m_paragraph_spacing = 0;

    bool operator ==(const ParagraphStyle& style) const;
  };

  /** A span of a line placed by a stored layout. */
  struct StoredRun {

    /** The index of the span's first UTF-16 code unit. */
    std::size_t m_from = 0;

    /** The index one past the span's last UTF-16 code unit. */
    std::size_t m_to = 0;

    /** The position of the span's baseline origin in the node's space. */
    Point m_position = Point(0, 0);

    /** The font the span was set in, if it differs from its run's. */
    std::optional<Font> m_font;

    /** The color the span was set in, if it differs from its run's. */
    std::optional<Color> m_color;

    bool operator ==(const StoredRun&) const = default;
  };

  /** A line placed by a stored layout. */
  struct StoredLine {

    /** The spans making up the line. */
    std::vector<StoredRun> m_runs;

    bool operator ==(const StoredLine&) const = default;
  };

  /** A paragraph placed by a stored layout. */
  struct StoredParagraph {

    /** The lines making up the paragraph. */
    std::vector<StoredLine> m_lines;

    bool operator ==(const StoredParagraph&) const = default;
  };

  /** The line breaks and positions Adobe XD computed for a text node. */
  struct StoredLayout {

    /** The paragraphs in order. */
    std::vector<StoredParagraph> m_paragraphs;

    bool operator ==(const StoredLayout&) const = default;
  };

  /** The payload of a text node. */
  struct Text {

    /** The characters, in UTF-16. */
    Shared<std::u16string> m_content;

    /** The frame the text is laid out in. */
    TextFrame m_frame;

    /** The paint and effects of the glyphs. */
    Style m_style;

    /** The character style of text not covered by a run. */
    TextStyle m_base_style;

    /**
     * The spans covering the content in order, or empty to style all of it
     * with the base style.
     */
    std::vector<TextRun> m_runs;

    /** The paragraph style. */
    ParagraphStyle m_paragraph_style;

    /** The layout stored by Adobe XD, if any. */
    std::optional<Shared<StoredLayout>> m_layout;

    /**
     * The series of contents the cells of a repeat grid holding the text
     * show in turn, or empty if there is none.
     */
    std::vector<std::u16string> m_repeat_data;

    bool operator ==(const Text&) const = default;
  };

  inline bool Font::operator ==(const Font& font) const {
    return m_family == font.m_family && m_style == font.m_style &&
      m_postscript_name == font.m_postscript_name &&
      equals(m_size, font.m_size);
  }

  inline bool TextStyle::operator ==(const TextStyle& style) const {
    return m_font == style.m_font &&
      equals(m_letter_spacing, style.m_letter_spacing) &&
      m_color == style.m_color && m_is_underlined == style.m_is_underlined &&
      m_is_struck_through == style.m_is_struck_through &&
      m_transform == style.m_transform && m_script == style.m_script;
  }

  inline bool AutoHeightFrame::operator ==(const AutoHeightFrame& frame) const {
    return equals(m_width, frame.m_width);
  }

  inline bool ParagraphStyle::operator ==(const ParagraphStyle& style) const {
    return m_alignment == style.m_alignment &&
      equals(m_line_height, style.m_line_height) &&
      equals(m_paragraph_spacing, style.m_paragraph_spacing);
  }
}

#endif

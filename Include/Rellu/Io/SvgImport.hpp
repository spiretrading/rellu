#ifndef RELLU_IO_SVG_IMPORT_HPP
#define RELLU_IO_SVG_IMPORT_HPP
#include <functional>
#include <optional>
#include <QString>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /** The vertical extent of the glyph cells of a font, in ems. */
  struct SvgFontExtent {

    /** The height above the baseline. */
    double m_ascent = 0;

    /** The depth below the baseline. */
    double m_descent = 0;
  };

  /**
   * Returns the extent of the glyph cells of the face displaying a font, or
   * none if it is unknown.
   */
  using SvgFontMeasurer =
    std::function<std::optional<SvgFontExtent> (const Font& font)>;

  /**
   * Tests whether text is SVG markup: an XML document whose root element is
   * an svg element.
   * @param text The text.
   */
  bool is_svg(const QString& text);

  /**
   * Reads SVG markup into a document as Adobe XD pastes it: the graphic
   * elements become editable shapes, text, images, masks and groups on the
   * canvas, wrapped in a group when the markup holds several. Throws
   * std::invalid_argument if the text is not SVG markup.
   * @param text The markup.
   * @param document The empty document receiving the nodes.
   * @param measurer The function measuring fonts to space the lines of text
   *        placed at irregular intervals, or empty to leave them unspaced.
   */
  void read_svg(const QString& text, Document& document,
    const SvgFontMeasurer& measurer = {});
}

#endif

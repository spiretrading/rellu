#ifndef RELLU_IO_SVG_IMPORT_HPP
#define RELLU_IO_SVG_IMPORT_HPP
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <QString>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /** The vertical extent of the glyph cells of a font, in ems. */
  struct SvgFontExtent {

    /** The height above the baseline. */
    double m_ascent = 0;

    /** The depth below the baseline. */
    double m_descent = 0;

    /** The height of lowercase letters, or 0 if it is unknown. */
    double m_x_height = 0;
  };

  /** Measures the fonts SVG text is displayed in. */
  struct SvgMeasurer {

    /**
     * Returns the extent of the glyph cells of the face displaying a font,
     * or none if it is unknown.
     */
    std::function<std::optional<SvgFontExtent> (const Font& font)> m_extent;

    /**
     * Returns the width text displayed in a font covers, in pixels: the
     * union of its glyphs' advances and outlines, or none if it is unknown.
     */
    std::function<std::optional<double> (
      const Font& font, const std::u16string& text)> m_width;
  };

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
   * std::invalid_argument if the text is not SVG markup, and
   * std::runtime_error if Adobe XD fails to convert it, pasting nothing.
   * @param text The markup.
   * @param document The empty document receiving the nodes.
   * @param measurer The functions measuring fonts, or empty functions to
   *        leave text that depends on them unmeasured.
   * @return The empty groups left by elements that failed to convert,
   *         which Adobe XD places at the document's origin.
   */
  std::vector<NodeId> read_svg(const QString& text, Document& document,
    const SvgMeasurer& measurer = {});
}

#endif

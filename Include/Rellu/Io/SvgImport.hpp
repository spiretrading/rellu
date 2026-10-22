#ifndef RELLU_IO_SVG_IMPORT_HPP
#define RELLU_IO_SVG_IMPORT_HPP
#include <QString>
#include "Rellu/Model/Document.hpp"

namespace Rellu {

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
   */
  void read_svg(const QString& text, Document& document);
}

#endif

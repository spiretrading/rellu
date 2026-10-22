#ifndef RELLU_UI_TEXT_MEASUREMENT_HPP
#define RELLU_UI_TEXT_MEASUREMENT_HPP
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Text/FontLibrary.hpp"

namespace Rellu {

  /**
   * Makes a TextMeasurer measuring text as it is laid out for display.
   * @param library The fonts to resolve the text's fonts with, which must
   *        outlive the measurer.
   */
  TextMeasurer make_text_measurer(const FontLibrary& library);

  /**
   * Makes a TextMeasurer measuring the region text paints, as Adobe XD
   * measures draw bounds.
   * @param library The fonts to resolve the text's fonts with, which must
   *        outlive the measurer.
   */
  TextMeasurer make_painted_text_measurer(const FontLibrary& library);

  /**
   * Makes a TextOutliner outlining the glyphs and decorations of text as
   * they are drawn, mirrored within the text's bounds when it is flipped.
   * @param library The fonts to resolve the text's fonts with, which must
   *        outlive the outliner.
   */
  TextOutliner make_text_outliner(const FontLibrary& library);

  /**
   * Makes the layout an Adobe XD package stores for text without a stored
   * layout, from the text as it is laid out for display.
   * @param library The fonts to resolve the text's fonts with, which must
   *        outlive the layout.
   */
  XdTextLayout make_xd_text_layout(const FontLibrary& library);
}

#endif

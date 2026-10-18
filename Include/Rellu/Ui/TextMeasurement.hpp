#ifndef RELLU_UI_TEXT_MEASUREMENT_HPP
#define RELLU_UI_TEXT_MEASUREMENT_HPP
#include "Rellu/Commands/Bounds.hpp"
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
   * Makes the layout an Adobe XD package stores for text without a stored
   * layout, from the text as it is laid out for display.
   * @param library The fonts to resolve the text's fonts with, which must
   *        outlive the layout.
   */
  XdTextLayout make_xd_text_layout(const FontLibrary& library);
}

#endif

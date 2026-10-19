#ifndef RELLU_UI_TEXT_PROPERTY_EDITORS_HPP
#define RELLU_UI_TEXT_PROPERTY_EDITORS_HPP
#include <functional>
#include <QString>
#include <QStringList>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Editor/TextSelection.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"

namespace Rellu {

  /** Returns the text being edited in place, or nullptr if there is none. */
  using TextSelectionSource = std::function<const TextSelection* ()>;

  /**
   * Adds the editors of the fields of Adobe XD's TEXT section:
   * text.font_family and text.font_style (QString), text.size,
   * text.letter_spacing, text.line_height and text.paragraph_spacing
   * (double), text.alignment, text.frame, text.transform and text.script
   * (Choice, in the order of the model's enumerations, text.frame in the
   * order Auto Width, Fixed Size, Auto Height), text.underline and
   * text.strikethrough (bool) and text.color (Color). A character property
   * set while a range of the text node edited is selected applies to that
   * range, otherwise to the whole of every text node.
   * @param editors The editors to add to.
   * @param library The fonts that font names resolve with and text is laid
   *        out with, which must outlive the editors.
   * @param measurer The function measuring text.
   * @param selection Returns the text being edited in place.
   */
  void add_text_property_editors(PropertyEditors& editors,
    const FontLibrary& library, TextMeasurer measurer,
    TextSelectionSource selection);

  /**
   * Makes the TextLayouter computing the layout Adobe XD stores, as laid out
   * for display.
   * @param library The fonts to lay out with, which must outlive the
   *        layouter.
   */
  TextLayouter make_text_layouter(const FontLibrary& library);

  /**
   * Lists the font families a library holds, sorted, as the font family
   * field offers them.
   * @param library The fonts.
   */
  QStringList list_font_families(const FontLibrary& library);

  /**
   * Lists the styles of a font family, as the font style field offers them.
   * @param library The fonts.
   * @param family The font family.
   */
  QStringList list_font_styles(
    const FontLibrary& library, const QString& family);
}

#endif

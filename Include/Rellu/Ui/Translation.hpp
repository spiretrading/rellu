#ifndef RELLU_UI_TRANSLATION_HPP
#define RELLU_UI_TRANSLATION_HPP
#include <string_view>
#include <QString>
#include "Rellu/Localization/Localization.hpp"

namespace Rellu {

  /**
   * Translates English user interface text into the language of the user
   * interface, falling back to the text itself where there is no
   * translation.
   * @param context The dot separated part of the interface showing the text.
   * @param text The English text.
   */
  QString translate(std::string_view context, const QString& text);

  /**
   * Translates English user interface text shown in no particular part of
   * the interface into the language of the user interface.
   * @param text The English text.
   */
  QString translate(const QString& text);
}

#endif

#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

QString Rellu::translate(std::string_view context, const QString& text) {
  if(get_language() == Language::ENGLISH || text.isEmpty()) {
    return text;
  }
  auto english = text.toStdString();
  auto translation = localize(context, english);
  if(translation.data() == english.data()) {
    return text;
  }
  return QString::fromUtf8(translation.data(),
    static_cast<qsizetype>(translation.size()));
}

QString Rellu::translate(const QString& text) {
  return translate(std::string_view(), text);
}

#ifndef RELLU_IO_PARALLEL_JSON_HPP
#define RELLU_IO_PARALLEL_JSON_HPP
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace Rellu {

  /**
   * Parses JSON text with the result of QJsonDocument::fromJson, parsing the
   * parts of a large text on several processors.
   * @param bytes The UTF-8 text.
   * @param error Set to the outcome of parsing.
   * @return The document, null if the text is not valid JSON.
   */
  QJsonDocument parse_json(const QByteArray& bytes, QJsonParseError& error);
}

#endif

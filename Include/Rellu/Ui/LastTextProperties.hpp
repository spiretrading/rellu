#ifndef RELLU_UI_LAST_TEXT_PROPERTIES_HPP
#define RELLU_UI_LAST_TEXT_PROPERTIES_HPP
#include <optional>
#include <string_view>
#include <QByteArray>
#include <QDateTime>
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /**
   * The text properties last used in a document, which Adobe XD keeps for
   * each document across sessions and gives new text.
   */
  struct LastTextProperties {

    /**
     * The character style, whose font, character spacing and color new
     * text takes.
     */
    TextStyle m_style;

    /** The paragraph style new text takes. */
    ParagraphStyle m_paragraph;

    bool operator ==(const LastTextProperties&) const = default;
  };

  /** The number of documents whose last used text properties are kept. */
  inline constexpr auto MAXIMUM_LAST_TEXT_PROPERTIES = 100;

  /**
   * Finds the text properties last used in a document in a store of them.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The id Adobe XD gives the document.
   * @return The properties, or none if the store has none for the document.
   */
  std::optional<LastTextProperties> find_last_text_properties(
    const QByteArray& store, std::string_view document);

  /**
   * Records the text properties last used in a document in a store of them,
   * keeping those of the MAXIMUM_LAST_TEXT_PROPERTIES documents recorded
   * most recently.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The id Adobe XD gives the document.
   * @param properties The properties.
   * @param time The time of the recording.
   * @return The store with the properties recorded, JSON encoded.
   */
  QByteArray record_last_text_properties(const QByteArray& store,
    std::string_view document, const LastTextProperties& properties,
    const QDateTime& time);
}

#endif

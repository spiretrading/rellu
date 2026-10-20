#ifndef RELLU_UI_LAST_PROPERTIES_HPP
#define RELLU_UI_LAST_PROPERTIES_HPP
#include <optional>
#include <string_view>
#include <QByteArray>
#include <QDateTime>
#include "Rellu/Model/Style.hpp"
#include "Rellu/Model/Text.hpp"
#include "Rellu/Ui/ColorPicker.hpp"

namespace Rellu {

  /** The text properties last used in a document, which new text takes. */
  struct LastTextProperties {

    /**
     * The character style, whose font, character spacing and color new
     * text takes.
     */
    TextStyle m_style;

    /** The paragraph style new text takes. */
    ParagraphStyle m_paragraph;

    /** Whether the fill of new text is enabled. */
    bool m_is_fill_enabled = true;

    /**
     * The color, width and enabled state of the border new text takes, or
     * none for no border.
     */
    std::optional<Stroke> m_border;

    bool operator ==(const LastTextProperties&) const = default;
  };

  /**
   * The properties last used in a document, which Adobe XD keeps for each
   * document across sessions.
   */
  struct LastProperties {

    /** The text properties, or none if no text has been styled. */
    std::optional<LastTextProperties> m_text;

    /** The color model the color picker shows values in. */
    ColorModel m_color_model = ColorModel::HEX;

    bool operator ==(const LastProperties&) const = default;
  };

  /** The number of documents whose last used properties are kept. */
  inline constexpr auto MAXIMUM_LAST_PROPERTIES = 100;

  /**
   * Finds the properties last used in a document in a store of them.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The id Adobe XD gives the document.
   * @return The properties, or none if the store has none for the document.
   */
  std::optional<LastProperties> find_last_properties(
    const QByteArray& store, std::string_view document);

  /**
   * Records the properties last used in a document in a store of them,
   * keeping those of the MAXIMUM_LAST_PROPERTIES documents recorded most
   * recently.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The id Adobe XD gives the document.
   * @param properties The properties.
   * @param time The time of the recording.
   * @return The store with the properties recorded, JSON encoded.
   */
  QByteArray record_last_properties(const QByteArray& store,
    std::string_view document, const LastProperties& properties,
    const QDateTime& time);

  /**
   * Carries the properties last used in a document over to the new id Adobe
   * XD gives the document, keeping those recorded under its previous id.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The new id of the document.
   * @param properties The properties last used in the document, replaced by
   *        those the store has for the new id if it has any.
   * @param time The time of the recording.
   * @return The store with the properties recorded under the new id, JSON
   *         encoded.
   */
  QByteArray change_last_properties_id(const QByteArray& store,
    std::string_view document, LastProperties& properties,
    const QDateTime& time);
}

#endif

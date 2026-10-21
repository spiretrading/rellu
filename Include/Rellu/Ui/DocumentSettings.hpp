#ifndef RELLU_UI_DOCUMENT_SETTINGS_HPP
#define RELLU_UI_DOCUMENT_SETTINGS_HPP
#include <cstdint>
#include <optional>
#include <string_view>
#include <QByteArray>
#include <QJsonObject>

namespace Rellu {

  /**
   * The number of milliseconds Adobe XD keeps the window settings recorded
   * for a document other than the one it opens.
   */
  inline constexpr auto DOCUMENT_SETTINGS_LIFETIME = std::int64_t(2592000000);

  /**
   * Finds the settings recorded for a document in a store of them, kept as
   * Adobe XD keeps a window's settings in its preferences: by the
   * document's uxdesign#id, each record holding the settings as JSON text
   * and the time of the recording.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The uxdesign#id of the document.
   * @return The settings, or none if the store has none for the document.
   */
  std::optional<QJsonObject> find_document_settings(
    const QByteArray& store, std::string_view document);

  /**
   * Records the settings of a document in a store of them.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The uxdesign#id of the document.
   * @param settings The settings, or none for the defaults, which remove
   *        the document's record.
   * @param time The time of the recording, in milliseconds since the epoch.
   * @return The store with the settings recorded, JSON encoded.
   */
  QByteArray record_document_settings(const QByteArray& store,
    std::string_view document, const std::optional<QJsonObject>& settings,
    std::int64_t time);

  /**
   * Discards the records of the documents other than one that were made
   * more than DOCUMENT_SETTINGS_LIFETIME ago or have no time.
   * @param store The store, JSON encoded, empty if there is none.
   * @param document The uxdesign#id of the document kept.
   * @param time The current time, in milliseconds since the epoch.
   * @return The store purged, JSON encoded.
   */
  QByteArray purge_document_settings(
    const QByteArray& store, std::string_view document, std::int64_t time);
}

#endif

#include <doctest/doctest.h>
#include <QJsonDocument>
#include <QJsonObject>
#include "Rellu/Ui/DocumentSettings.hpp"

using namespace Rellu;

TEST_SUITE("DocumentSettings") {
  TEST_CASE("record") {
    auto settings = QJsonObject();
    settings.insert("visible", false);
    auto store =
      record_document_settings(QByteArray(), "first", settings, 1000);
    REQUIRE(find_document_settings(store, "first") == settings);
    REQUIRE(!find_document_settings(store, "second"));
    REQUIRE(!find_document_settings(QByteArray(), "first"));
    store = record_document_settings(store, "first", std::nullopt, 2000);
    REQUIRE(!find_document_settings(store, "first"));
    REQUIRE(QJsonDocument::fromJson(store).object().isEmpty());
  }

  TEST_CASE("purge") {
    auto settings = QJsonObject();
    settings.insert("visible", false);
    auto store = record_document_settings(QByteArray(), "old", settings, 0);
    store = record_document_settings(store, "recent", settings,
      DOCUMENT_SETTINGS_LIFETIME);
    store = record_document_settings(store, "open", settings, 0);
    REQUIRE(purge_document_settings(
      store, "open", DOCUMENT_SETTINGS_LIFETIME) == store);
    auto purged =
      purge_document_settings(store, "open", DOCUMENT_SETTINGS_LIFETIME + 1);
    REQUIRE(!find_document_settings(purged, "old"));
    REQUIRE(find_document_settings(purged, "recent"));
    REQUIRE(find_document_settings(purged, "open"));
  }
}

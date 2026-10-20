#include <optional>
#include <string>
#include <doctest/doctest.h>
#include <QDate>
#include <QTime>
#include <QTimeZone>
#include "Rellu/Ui/LastTextProperties.hpp"

using namespace Rellu;

namespace {
  LastTextProperties make_properties() {
    auto properties = LastTextProperties();
    properties.m_style.m_font =
      Font("Roboto", "Medium", "Roboto-Medium", 24);
    properties.m_style.m_letter_spacing = 50;
    properties.m_style.m_color = Color(0.25, 0.5, 0.75, 0.5);
    properties.m_paragraph.m_alignment = TextAlignment::CENTER;
    properties.m_paragraph.m_line_height = 30;
    properties.m_paragraph.m_paragraph_spacing = 12;
    return properties;
  }

  QDateTime make_time(int second) {
    return QDateTime(QDate(2026, 10, 19), QTime(12, 0),
      QTimeZone::UTC).addSecs(second);
  }
}

TEST_SUITE("LastTextProperties") {
  TEST_CASE("round_trip") {
    auto properties = make_properties();
    REQUIRE(!find_last_text_properties(QByteArray(), "document"));
    auto store = record_last_text_properties(
      QByteArray(), "document", properties, make_time(0));
    REQUIRE(find_last_text_properties(store, "document") == properties);
    REQUIRE(!find_last_text_properties(store, "other"));
    auto automatic = properties;
    automatic.m_paragraph.m_line_height = std::nullopt;
    store = record_last_text_properties(
      store, "document", automatic, make_time(1));
    REQUIRE(find_last_text_properties(store, "document") == automatic);
  }

  TEST_CASE("id_change") {
    auto properties = make_properties();
    auto store = record_last_text_properties(
      QByteArray(), "original", properties, make_time(0));
    auto current = properties;
    current.m_style.m_font.m_size = 36;
    store = change_last_text_properties_id(
      store, "copy", current, make_time(1));
    REQUIRE(current.m_style.m_font.m_size == 36);
    REQUIRE(find_last_text_properties(store, "copy") == current);
    REQUIRE(find_last_text_properties(store, "original") == properties);
    auto other = properties;
    other.m_paragraph.m_alignment = TextAlignment::RIGHT;
    auto changed = change_last_text_properties_id(
      store, "original", other, make_time(2));
    REQUIRE(changed == store);
    REQUIRE(other == properties);
  }

  TEST_CASE("pruning") {
    auto properties = make_properties();
    auto store = QByteArray();
    for(auto i = 0; i != MAXIMUM_LAST_TEXT_PROPERTIES + 2; ++i) {
      store = record_last_text_properties(
        store, std::to_string(i), properties, make_time(i));
    }
    REQUIRE(!find_last_text_properties(store, "0"));
    REQUIRE(find_last_text_properties(store, "2"));
    REQUIRE(find_last_text_properties(
      store, std::to_string(MAXIMUM_LAST_TEXT_PROPERTIES + 1)));
  }
}

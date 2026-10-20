#include <optional>
#include <string>
#include <doctest/doctest.h>
#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QTimeZone>
#include "Rellu/Ui/LastProperties.hpp"

using namespace Rellu;

namespace {
  LastProperties make_properties() {
    auto text = LastTextProperties();
    text.m_style.m_font = Font("Roboto", "Medium", "Roboto-Medium", 24);
    text.m_style.m_letter_spacing = 50;
    text.m_style.m_color = Color(0.25, 0.5, 0.75, 0.5);
    text.m_paragraph.m_alignment = TextAlignment::CENTER;
    text.m_paragraph.m_line_height = 30;
    text.m_paragraph.m_paragraph_spacing = 12;
    text.m_is_fill_enabled = false;
    auto border = Stroke();
    border.m_color = Color(1, 0, 0, 1);
    border.m_width = 3;
    border.m_is_enabled = false;
    text.m_border = border;
    auto properties = LastProperties();
    properties.m_text = text;
    properties.m_color_model = ColorModel::HSB;
    return properties;
  }

  QDateTime make_time(int second) {
    return QDateTime(QDate(2026, 10, 19), QTime(12, 0),
      QTimeZone::UTC).addSecs(second);
  }
}

TEST_SUITE("LastProperties") {
  TEST_CASE("round_trip") {
    auto properties = make_properties();
    REQUIRE(!find_last_properties(QByteArray(), "document"));
    auto store = record_last_properties(
      QByteArray(), "document", properties, make_time(0));
    REQUIRE(find_last_properties(store, "document") == properties);
    REQUIRE(!find_last_properties(store, "other"));
    auto json = QJsonDocument::fromJson(store).object().value(
      "versions").toObject().value("1").toObject().value(
      "documents").toObject().value("document").toObject().value(
      "properties").toObject();
    REQUIRE(json.value("colorModel").toInt() == 2);
    REQUIRE(!json.value("text").toObject().value("fillEnabled").toBool());
    REQUIRE(json.value("text").toObject().value("strokeWidth").toDouble() ==
      3);
    auto automatic = properties;
    automatic.m_text->m_paragraph.m_line_height = std::nullopt;
    automatic.m_text->m_border = std::nullopt;
    store = record_last_properties(
      store, "document", automatic, make_time(1));
    REQUIRE(find_last_properties(store, "document") == automatic);
    auto color = LastProperties();
    color.m_color_model = ColorModel::RGB;
    store = record_last_properties(store, "document", color, make_time(2));
    REQUIRE(find_last_properties(store, "document") == color);
  }

  TEST_CASE("id_change") {
    auto properties = make_properties();
    auto store = record_last_properties(
      QByteArray(), "original", properties, make_time(0));
    auto current = properties;
    current.m_text->m_style.m_font.m_size = 36;
    current.m_color_model = ColorModel::RGB;
    store = change_last_properties_id(store, "copy", current, make_time(1));
    REQUIRE(current.m_text->m_style.m_font.m_size == 36);
    REQUIRE(find_last_properties(store, "copy") == current);
    REQUIRE(find_last_properties(store, "original") == properties);
    auto other = properties;
    other.m_color_model = ColorModel::HEX;
    auto changed =
      change_last_properties_id(store, "original", other, make_time(2));
    REQUIRE(changed == store);
    REQUIRE(other == properties);
  }

  TEST_CASE("pruning") {
    auto properties = make_properties();
    auto store = QByteArray();
    for(auto i = 0; i != MAXIMUM_LAST_PROPERTIES + 2; ++i) {
      store = record_last_properties(
        store, std::to_string(i), properties, make_time(i));
    }
    REQUIRE(!find_last_properties(store, "0"));
    REQUIRE(find_last_properties(store, "2"));
    REQUIRE(find_last_properties(
      store, std::to_string(MAXIMUM_LAST_PROPERTIES + 1)));
  }
}

#include <algorithm>
#include <utility>
#include <vector>
#include <QJsonDocument>
#include <QJsonObject>
#include "Rellu/Ui/LastProperties.hpp"

using namespace Rellu;

namespace {
  const auto VERSION = QStringLiteral("1");

  QString to_alignment(TextAlignment alignment) {
    if(alignment == TextAlignment::CENTER) {
      return "center";
    } else if(alignment == TextAlignment::RIGHT) {
      return "right";
    }
    return "left";
  }

  TextAlignment parse_alignment(const QString& alignment) {
    if(alignment == "center") {
      return TextAlignment::CENTER;
    } else if(alignment == "right") {
      return TextAlignment::RIGHT;
    }
    return TextAlignment::LEFT;
  }

  QJsonObject to_json(const Color& color) {
    auto value = QJsonObject();
    value.insert("r", color.m_red * 255);
    value.insert("g", color.m_green * 255);
    value.insert("b", color.m_blue * 255);
    auto json = QJsonObject();
    json.insert("mode", "RGB");
    json.insert("value", value);
    json.insert("alpha", color.m_alpha);
    return json;
  }

  Color parse_color(const QJsonObject& json, const Color& fallback) {
    if(!json.value("value").isObject()) {
      return fallback;
    }
    auto value = json.value("value").toObject();
    return Color(value.value("r").toDouble(fallback.m_red * 255) / 255,
      value.value("g").toDouble(fallback.m_green * 255) / 255,
      value.value("b").toDouble(fallback.m_blue * 255) / 255,
      json.value("alpha").toDouble(fallback.m_alpha));
  }

  QJsonObject to_json(const LastTextProperties& properties) {
    auto& style = properties.m_style;
    auto& paragraph = properties.m_paragraph;
    auto text = QJsonObject();
    text.insert("fontFamily", QString::fromStdString(style.m_font.m_family));
    text.insert("fontStyle", QString::fromStdString(style.m_font.m_style));
    text.insert("postscriptName",
      QString::fromStdString(style.m_font.m_postscript_name));
    text.insert("fontSize", style.m_font.m_size);
    text.insert("textAlign", to_alignment(paragraph.m_alignment));
    if(paragraph.m_line_height) {
      text.insert("lineSpacing", *paragraph.m_line_height);
    }
    text.insert("paragraphSpacing", paragraph.m_paragraph_spacing);
    text.insert("fill", to_json(style.m_color));
    text.insert("charSpacing", style.m_letter_spacing);
    text.insert("fillEnabled", properties.m_is_fill_enabled);
    if(auto& border = properties.m_border) {
      text.insert("stroke", to_json(border->m_color));
      text.insert("strokeEnabled", border->m_is_enabled);
      text.insert("strokeWidth", border->m_width);
    }
    return text;
  }

  LastTextProperties parse_text(const QJsonObject& text) {
    auto properties = LastTextProperties();
    auto& style = properties.m_style;
    auto& paragraph = properties.m_paragraph;
    style.m_font.m_family = text.value("fontFamily").toString().toStdString();
    style.m_font.m_style = text.value("fontStyle").toString().toStdString();
    style.m_font.m_postscript_name =
      text.value("postscriptName").toString().toStdString();
    style.m_font.m_size = text.value("fontSize").toDouble(style.m_font.m_size);
    style.m_color = parse_color(text.value("fill").toObject(), style.m_color);
    style.m_letter_spacing = text.value("charSpacing").toDouble();
    paragraph.m_alignment =
      parse_alignment(text.value("textAlign").toString());
    if(text.value("lineSpacing").isDouble()) {
      paragraph.m_line_height = text.value("lineSpacing").toDouble();
    }
    paragraph.m_paragraph_spacing = text.value("paragraphSpacing").toDouble();
    properties.m_is_fill_enabled = text.value("fillEnabled").toBool(true);
    if(text.value("stroke").isObject()) {
      auto border = Stroke();
      border.m_color =
        parse_color(text.value("stroke").toObject(), border.m_color);
      border.m_is_enabled = text.value("strokeEnabled").toBool(true);
      border.m_width = text.value("strokeWidth").toDouble(border.m_width);
      properties.m_border = std::move(border);
    }
    return properties;
  }

  ColorModel parse_color_model(const QJsonValue& value) {
    auto model = value.toInt();
    if(model == static_cast<int>(ColorModel::RGB)) {
      return ColorModel::RGB;
    } else if(model == static_cast<int>(ColorModel::HSB)) {
      return ColorModel::HSB;
    }
    return ColorModel::HEX;
  }

  QJsonObject get_documents(const QJsonObject& root) {
    return root.value("versions").toObject().value(VERSION).toObject().value(
      "documents").toObject();
  }

  QString to_key(std::string_view document) {
    return QString::fromUtf8(
      document.data(), static_cast<qsizetype>(document.size()));
  }
}

std::optional<LastProperties> Rellu::find_last_properties(
    const QByteArray& store, std::string_view document) {
  auto documents = get_documents(QJsonDocument::fromJson(store).object());
  auto entry = documents.value(to_key(document));
  if(!entry.isObject() ||
      !entry.toObject().value("properties").isObject()) {
    return std::nullopt;
  }
  auto json = entry.toObject().value("properties").toObject();
  auto properties = LastProperties();
  if(json.value("text").isObject()) {
    properties.m_text = parse_text(json.value("text").toObject());
  }
  properties.m_color_model = parse_color_model(json.value("colorModel"));
  return properties;
}

QByteArray Rellu::record_last_properties(const QByteArray& store,
    std::string_view document, const LastProperties& properties,
    const QDateTime& time) {
  auto root = QJsonDocument::fromJson(store).object();
  auto documents = get_documents(root);
  auto json = QJsonObject();
  if(properties.m_text) {
    json.insert("text", to_json(*properties.m_text));
  }
  json.insert("colorModel", static_cast<int>(properties.m_color_model));
  auto entry = QJsonObject();
  entry.insert("properties", json);
  entry.insert("date", time.toUTC().toString(Qt::ISODateWithMs));
  documents.insert(to_key(document), entry);
  if(documents.size() > MAXIMUM_LAST_PROPERTIES) {
    auto dates = std::vector<std::pair<QString, QString>>();
    for(auto i = documents.begin(); i != documents.end(); ++i) {
      dates.emplace_back(
        i.value().toObject().value("date").toString(), i.key());
    }
    std::sort(dates.begin(), dates.end());
    auto excess = dates.size() - MAXIMUM_LAST_PROPERTIES;
    for(auto i = std::size_t(0); i != excess; ++i) {
      documents.remove(dates[i].second);
    }
  }
  auto version = QJsonObject();
  version.insert("documents", documents);
  auto versions = root.value("versions").toObject();
  versions.insert(VERSION, version);
  root.insert("versions", versions);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray Rellu::change_last_properties_id(const QByteArray& store,
    std::string_view document, LastProperties& properties,
    const QDateTime& time) {
  if(auto recorded = find_last_properties(store, document)) {
    properties = std::move(*recorded);
    return store;
  }
  return record_last_properties(store, document, properties, time);
}

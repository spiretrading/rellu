#include <QJsonDocument>
#include <QString>
#include "Rellu/Ui/DocumentSettings.hpp"

using namespace Rellu;

namespace {
  QString to_key(std::string_view document) {
    return QString::fromUtf8(
      document.data(), static_cast<qsizetype>(document.size()));
  }

  QJsonObject parse(const QByteArray& store) {
    return QJsonDocument::fromJson(store).object();
  }

  QByteArray encode(const QJsonObject& root) {
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
  }
}

std::optional<QJsonObject> Rellu::find_document_settings(
    const QByteArray& store, std::string_view document) {
  auto entry = parse(store).value(to_key(document)).toObject();
  auto data = entry.value("data");
  if(!data.isString()) {
    return std::nullopt;
  }
  auto settings = QJsonDocument::fromJson(data.toString().toUtf8());
  if(!settings.isObject()) {
    return std::nullopt;
  }
  return settings.object();
}

QByteArray Rellu::record_document_settings(const QByteArray& store,
    std::string_view document, const std::optional<QJsonObject>& settings,
    std::int64_t time) {
  auto root = parse(store);
  auto key = to_key(document);
  if(!settings) {
    if(!root.contains(key)) {
      return store;
    }
    root.remove(key);
    return encode(root);
  }
  auto entry = QJsonObject();
  entry.insert("data", QString::fromUtf8(
    QJsonDocument(*settings).toJson(QJsonDocument::Compact)));
  entry.insert("time", QString::number(time));
  root.insert(key, entry);
  return encode(root);
}

QByteArray Rellu::purge_document_settings(
    const QByteArray& store, std::string_view document, std::int64_t time) {
  auto root = parse(store);
  auto key = to_key(document);
  auto is_purged = false;
  for(auto& other : root.keys()) {
    if(other == key) {
      continue;
    }
    auto is_valid = false;
    auto recorded = root.value(other).toObject().value("time").toString().
      toLongLong(&is_valid);
    if(!is_valid || time - recorded > DOCUMENT_SETTINGS_LIFETIME) {
      root.remove(other);
      is_purged = true;
    }
  }
  if(!is_purged) {
    return store;
  }
  return encode(root);
}

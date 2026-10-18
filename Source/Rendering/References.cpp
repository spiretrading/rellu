#include <stdexcept>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include "Rellu/Rendering/References.hpp"

using namespace Rellu;

namespace {
  auto read(const QString& path) {
    auto file = QFile(path);
    if(!file.open(QIODevice::ReadOnly)) {
      throw std::runtime_error(
        QString("Unable to open %1.").arg(path).toStdString());
    }
    return file.readAll();
  }

  auto to_rectangle(const QJsonValue& value) {
    auto bounds = value.toObject();
    return QRectF(bounds["x"].toDouble(), bounds["y"].toDouble(),
      bounds["width"].toDouble(), bounds["height"].toDouble());
  }

  auto to_scale(const QString& file) {
    static const auto PATTERN = QRegularExpression("@(\\d+)x\\.png$");
    auto match = PATTERN.match(file);
    if(!match.hasMatch()) {
      return 0;
    }
    return match.captured(1).toInt();
  }

  auto to_item(const QJsonObject& node, const QDir& directory) {
    auto item = ReferenceItem();
    item.m_index = node["index"].toInt();
    item.m_guid = node["guid"].toString();
    item.m_name = node["name"].toString();
    item.m_type = node["type"].toString();
    item.m_is_visible = node["visible"].toBool();
    item.m_bounds = to_rectangle(node["bounds"]);
    item.m_draw_bounds = to_rectangle(node["drawBounds"]);
    for(const auto& file : node["files"].toArray()) {
      auto name = file.toString();
      if(auto scale = to_scale(name)) {
        item.m_images[scale] = directory.absoluteFilePath(name);
      }
    }
    return item;
  }

  auto to_error(const QJsonObject& error) {
    return ReferenceError(error["index"].toInt(), error["scale"].toInt(),
      error["error"].toString());
  }
}

std::vector<QString> Rellu::list_reference_documents(const QString& root) {
  auto data = read(QDir(root).filePath("exported.txt"));
  auto stream = QTextStream(data);
  auto documents = std::vector<QString>();
  while(!stream.atEnd()) {
    auto line = stream.readLine().trimmed();
    if(!line.isEmpty()) {
      documents.push_back(line);
    }
  }
  return documents;
}

ReferenceDocument Rellu::load_reference_document(
    const QString& root, const QString& path) {
  auto directory = QDir(QDir(root).filePath(path));
  auto nodes_path = directory.filePath("nodes.json");
  auto error = QJsonParseError();
  auto json = QJsonDocument::fromJson(read(nodes_path), &error);
  if(error.error != QJsonParseError::NoError || !json.isObject()) {
    throw std::runtime_error(
      QString("Malformed %1.").arg(nodes_path).toStdString());
  }
  auto document = ReferenceDocument();
  document.m_path = path;
  document.m_directory = directory.absolutePath();
  auto object = json.object();
  for(const auto& node : object["nodes"].toArray()) {
    document.m_items.push_back(to_item(node.toObject(), directory));
  }
  for(const auto& failure : object["errors"].toArray()) {
    document.m_errors.push_back(to_error(failure.toObject()));
  }
  return document;
}

std::vector<ReferenceDocument> Rellu::load_reference_documents(
    const QString& root) {
  auto documents = std::vector<ReferenceDocument>();
  for(const auto& path : list_reference_documents(root)) {
    documents.push_back(load_reference_document(root, path));
  }
  return documents;
}

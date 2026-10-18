#ifndef RELLU_IO_TESTS_XD_TEST_PACKAGE_HPP
#define RELLU_IO_TESTS_XD_TEST_PACKAGE_HPP
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <QBuffer>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <private/qzipwriter_p.h>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Model/Document.hpp"

namespace Rellu::Tests {

  /**
   * Parses JSON text holding an object.
   * @param json The JSON text.
   * @return The object, or an empty object if the text is not an object.
   */
  inline QJsonObject parse_json(std::string_view json) {
    return QJsonDocument::fromJson(
      QByteArray(json.data(), static_cast<qsizetype>(json.size()))).object();
  }

  /**
   * Parses JSON text holding an array.
   * @param json The JSON text.
   * @return The array, or an empty array if the text is not an array.
   */
  inline QJsonArray parse_json_array(std::string_view json) {
    return QJsonDocument::fromJson(
      QByteArray(json.data(), static_cast<qsizetype>(json.size()))).array();
  }

  /**
   * Writes a ZIP archive in memory.
   * @param entries The name and contents of each entry, in archive order.
   * @return The archive's bytes.
   */
  inline QByteArray make_archive(
      const std::vector<std::pair<std::string, QByteArray>>& entries) {
    auto bytes = QByteArray();
    auto buffer = QBuffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    auto writer = QZipWriter(&buffer);
    for(auto& entry : entries) {
      writer.addFile(QString::fromStdString(entry.first), entry.second);
    }
    writer.close();
    return bytes;
  }

  /** The contents of an Adobe XD package built for a test. */
  struct XdTestPackage {

    /** The children of the pasteboard. */
    QJsonArray m_pasteboard;

    /** The resource settings, the resources' meta.ux object. */
    QJsonObject m_resources;

    /** The frame of each artboard, by artboard id. */
    QJsonObject m_frames;

    /** The artboard nodes, each in its own entry, in manifest order. */
    std::vector<QJsonObject> m_artboards;

    /** The prototype interactions, omitted when empty. */
    QJsonObject m_interactions;

    /** Additional entries, such as images. */
    std::vector<std::pair<std::string, QByteArray>> m_entries;

    /** Returns the bytes of the package. */
    QByteArray build() const;
  };

  /**
   * Imports a package with every built-in importer.
   * @param package The package's bytes.
   * @param document The document receiving the import.
   * @return The report of the import.
   */
  inline ImportReport import_package(
      const QByteArray& package, Document& document) {
    return import_xd(XdPackage(package), document);
  }

  /**
   * Imports a package with every built-in importer.
   * @param package The package's contents.
   * @param document The document receiving the import.
   * @return The report of the import.
   */
  inline ImportReport import_package(
      const XdTestPackage& package, Document& document) {
    return import_package(package.build(), document);
  }

  /**
   * Imports pasteboard nodes with every built-in importer.
   * @param children The JSON text of the array of pasteboard nodes.
   * @param document The document receiving the import.
   * @return The report of the import.
   */
  inline ImportReport import_nodes(
      std::string_view children, Document& document) {
    auto package = XdTestPackage();
    package.m_pasteboard = parse_json_array(children);
    return import_package(package, document);
  }

  inline QByteArray XdTestPackage::build() const {
    auto entries = std::vector<std::pair<std::string, QByteArray>>();
    auto artwork = QJsonArray();
    auto pasteboard_entry = QJsonObject();
    pasteboard_entry.insert("path", "pasteboard");
    artwork.append(pasteboard_entry);
    auto artboard_entries = std::vector<std::pair<std::string, QByteArray>>();
    for(auto& artboard : m_artboards) {
      auto path = "artboard-" + artboard.value("id").toString();
      auto manifest_entry = QJsonObject();
      manifest_entry.insert("path", path);
      artwork.append(manifest_entry);
      auto content = QJsonObject();
      content.insert("version", "1.5.0");
      content.insert("children", QJsonArray({artboard}));
      artboard_entries.emplace_back(
        "artwork/" + path.toStdString() + "/graphics/graphicContent.agc",
        QJsonDocument(content).toJson(QJsonDocument::Compact));
    }
    auto artwork_entry = QJsonObject();
    artwork_entry.insert("path", "artwork");
    artwork_entry.insert("children", artwork);
    auto manifest = QJsonObject();
    manifest.insert("manifest-format-version", 6);
    manifest.insert("children", QJsonArray({artwork_entry}));
    entries.emplace_back(std::string(XdPackage::MANIFEST),
      QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    auto meta = QJsonObject();
    meta.insert("ux", m_resources);
    auto resources = QJsonObject();
    resources.insert("meta", meta);
    auto resources_content = QJsonObject();
    resources_content.insert("version", "1.5.0");
    resources_content.insert("children", QJsonArray());
    resources_content.insert("resources", resources);
    resources_content.insert("artboards", m_frames);
    entries.emplace_back(std::string(XdPackage::RESOURCES),
      QJsonDocument(resources_content).toJson(QJsonDocument::Compact));
    auto pasteboard = QJsonObject();
    pasteboard.insert("version", "1.5.0");
    pasteboard.insert("children", m_pasteboard);
    entries.emplace_back(std::string(XdPackage::PASTEBOARD),
      QJsonDocument(pasteboard).toJson(QJsonDocument::Compact));
    entries.insert(
      entries.end(), artboard_entries.begin(), artboard_entries.end());
    if(!m_interactions.isEmpty()) {
      entries.emplace_back(std::string(XdPackage::INTERACTIONS),
        QJsonDocument(m_interactions).toJson(QJsonDocument::Compact));
    }
    entries.insert(entries.end(), m_entries.begin(), m_entries.end());
    return make_archive(entries);
  }
}

#endif

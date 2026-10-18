#include <stdexcept>
#include <vector>
#include <doctest/doctest.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "Rellu/Rendering/References.hpp"

using namespace Rellu;

namespace {
  const auto FIRST_NODES =
    "{\n"
    "  \"folder\": \"C:\\\\export\",\n"
    "  \"nodes\": [\n"
    "    {\n"
    "      \"index\": 0,\n"
    "      \"guid\": \"1c2b1745-5055-49fd-8c9a-e6895d4d095c\",\n"
    "      \"name\": \"Components\",\n"
    "      \"type\": \"Artboard\",\n"
    "      \"visible\": true,\n"
    "      \"bounds\": {\"x\": 0, \"y\": 327, \"width\": 2300, "
    "\"height\": 3453},\n"
    "      \"drawBounds\": {\"x\": -1, \"y\": 326, \"width\": 2302, "
    "\"height\": 3455},\n"
    "      \"files\": [\n"
    "        \"0-1c2b1745-5055-49fd-8c9a-e6895d4d095c@1x.png\",\n"
    "        \"0-1c2b1745-5055-49fd-8c9a-e6895d4d095c@2x.png\"\n"
    "      ]\n"
    "    },\n"
    "    {\n"
    "      \"index\": 1,\n"
    "      \"guid\": \"a7939584-a886-440f-ba5f-6a59cbb79005\",\n"
    "      \"name\": \"TITLE\",\n"
    "      \"type\": \"Group\",\n"
    "      \"visible\": false,\n"
    "      \"bounds\": {\"x\": 0, \"y\": 10, \"width\": 2300, "
    "\"height\": 152.5},\n"
    "      \"drawBounds\": {\"x\": 0, \"y\": 10, \"width\": 2300, "
    "\"height\": 153},\n"
    "      \"files\": [\"1-a7939584-a886-440f-ba5f-6a59cbb79005@1x.png\"]\n"
    "    }\n"
    "  ],\n"
    "  \"errors\": [\n"
    "    {\"index\": 1, \"scale\": 2, \"error\": \"Error: rendition failed\"}\n"
    "  ]\n"
    "}\n";
  const auto SECOND_NODES = "{\"folder\": \"\", \"nodes\": [], \"errors\": []}";

  void write(const QString& path, const QByteArray& contents) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto file = QFile(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(contents) == contents.size());
  }
}

TEST_SUITE("References") {
  TEST_CASE("reference_root") {
    auto previous = qgetenv("RELLU_REFERENCES");
    auto is_set = qEnvironmentVariableIsSet("RELLU_REFERENCES");
    qputenv("RELLU_REFERENCES", "D:/references");
    REQUIRE(get_reference_root() == "D:/references");
    qunsetenv("RELLU_REFERENCES");
    REQUIRE(
      get_reference_root() == "C:/Users/Kranar/Nextcloud/rellu_references");
    if(is_set) {
      qputenv("RELLU_REFERENCES", previous);
    }
  }

  TEST_CASE("load") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto root = QDir(directory.path());
    write(root.filePath("exported.txt"),
      "drafts/first/components\r\n\r\n  final/second  \n");
    write(root.filePath("drafts/first/components/nodes.json"), FIRST_NODES);
    write(root.filePath("final/second/nodes.json"), SECOND_NODES);
    auto paths = list_reference_documents(root.path());
    REQUIRE(paths ==
      std::vector<QString>({"drafts/first/components", "final/second"}));
    auto document =
      load_reference_document(root.path(), "drafts/first/components");
    REQUIRE(document.m_path == "drafts/first/components");
    REQUIRE(QDir(document.m_directory) ==
      QDir(root.filePath("drafts/first/components")));
    REQUIRE(document.m_items.size() == 2);
    const auto& artboard = document.m_items[0];
    REQUIRE(artboard.m_index == 0);
    REQUIRE(artboard.m_guid == "1c2b1745-5055-49fd-8c9a-e6895d4d095c");
    REQUIRE(artboard.m_name == "Components");
    REQUIRE(artboard.m_type == "Artboard");
    REQUIRE(artboard.m_is_visible);
    REQUIRE(artboard.m_bounds == QRectF(0, 327, 2300, 3453));
    REQUIRE(artboard.m_draw_bounds == QRectF(-1, 326, 2302, 3455));
    REQUIRE(artboard.m_images.size() == 2);
    REQUIRE(artboard.m_images.at(1) == QDir(document.m_directory).filePath(
      "0-1c2b1745-5055-49fd-8c9a-e6895d4d095c@1x.png"));
    REQUIRE(artboard.m_images.at(2) == QDir(document.m_directory).filePath(
      "0-1c2b1745-5055-49fd-8c9a-e6895d4d095c@2x.png"));
    const auto& title = document.m_items[1];
    REQUIRE(title.m_index == 1);
    REQUIRE(title.m_type == "Group");
    REQUIRE(!title.m_is_visible);
    REQUIRE(title.m_bounds == QRectF(0, 10, 2300, 152.5));
    REQUIRE(title.m_draw_bounds == QRectF(0, 10, 2300, 153));
    REQUIRE(title.m_images.size() == 1);
    REQUIRE(title.m_images.contains(1));
    REQUIRE(document.m_errors.size() == 1);
    REQUIRE(document.m_errors[0].m_index == 1);
    REQUIRE(document.m_errors[0].m_scale == 2);
    REQUIRE(document.m_errors[0].m_message == "Error: rendition failed");
    auto documents = load_reference_documents(root.path());
    REQUIRE(documents.size() == 2);
    REQUIRE(documents[1].m_path == "final/second");
    REQUIRE(documents[1].m_items.empty());
    REQUIRE(documents[1].m_errors.empty());
  }

  TEST_CASE("malformed") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto root = QDir(directory.path());
    REQUIRE_THROWS_AS(
      list_reference_documents(root.path()), std::runtime_error);
    REQUIRE_THROWS_AS(
      load_reference_document(root.path(), "missing"), std::runtime_error);
    write(root.filePath("broken/nodes.json"), "{\"nodes\": [");
    REQUIRE_THROWS_AS(
      load_reference_document(root.path(), "broken"), std::runtime_error);
  }

  TEST_CASE("reference_set") {
    auto root = get_reference_root();
    if(!QFileInfo::exists(QDir(root).filePath("exported.txt"))) {
      MESSAGE("Skipped, no reference set at " << root.toStdString());
      return;
    }
    auto documents = load_reference_documents(root);
    REQUIRE(!documents.empty());
    for(const auto& document : documents) {
      for(const auto& item : document.m_items) {
        REQUIRE(!item.m_guid.isEmpty());
        for(const auto& image : item.m_images) {
          REQUIRE(QFileInfo::exists(image.second));
        }
      }
    }
  }
}

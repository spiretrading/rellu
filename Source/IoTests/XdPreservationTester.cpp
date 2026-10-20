#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include <QByteArray>
#include <QJsonArray>
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "XdTestPackage.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  std::string join(const std::vector<std::string>& differences) {
    auto text = std::string();
    for(auto& difference : differences) {
      text += difference + '\n';
    }
    return text;
  }

  const auto SHAPE = "00000000-0000-0000-0000-00000000000a";
  const auto MOVED = "00000000-0000-0000-0000-00000000000b";
  const auto FILLED = "00000000-0000-0000-0000-00000000000c";
  const auto DELETED = "00000000-0000-0000-0000-00000000000d";
  const auto COMPOUND = "00000000-0000-0000-0000-00000000000e";

  XdTestPackage make_package() {
    auto package = XdTestPackage();
    package.m_pasteboard = parse_json_array(R"([
      {"type": "shape", "id": "00000000-0000-0000-0000-00000000000a",
       "name": "Kept", "plugin": {"data": [1, 2, {"deep": true}]},
       "meta": {"ux": {"hasCustomName": true, "pluginMetadata": "xyz"}},
       "style": {"fill": {"type": "solid", "uxdesign#type": "solid",
         "color": {"mode": "RGB", "value": {"r": 1, "g": 2, "b": 3}}},
         "filters": [{"type": "dropShadow", "global": true, "unknown": 7,
           "params": {"dropShadows": [{"dx": 0, "dy": 1, "r": 2,
             "color": {"mode": "RGB", "value": {"r": 0, "g": 0, "b": 0}}}]}}]},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 10, "height": 10,
         "extra": "e"},
       "visualBounds": {"x": 0, "y": 0, "width": 10, "height": 10}},
      {"type": "shape", "id": "00000000-0000-0000-0000-00000000000b",
       "name": "Moved", "meta": {"ux": {"rotation": 0}},
       "transform": {"a": 1, "b": 0, "c": 0, "d": 1, "tx": 5, "ty": 5},
       "style": {"fill": {"type": "none"}},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 4, "height": 4},
       "visualBounds": {"x": 5, "y": 5, "width": 4, "height": 4}},
      {"type": "shape", "id": "00000000-0000-0000-0000-00000000000c",
       "name": "Filled", "meta": {"ux": {}},
       "style": {"fill": {"type": "pattern", "pattern": {"width": 8,
         "height": 8, "href": "/a/b.png", "meta": {"ux": {"uid":
           "000000000000000000000000000000ff", "scaleBehavior": "fill",
           "hrefLastModifiedDate": 9, "secret": 1}}}}},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 8, "height": 8}},
      {"type": "shape", "id": "00000000-0000-0000-0000-00000000000d",
       "name": "Deleted", "meta": {"ux": {"gone": true}},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 1, "height": 1}},
      {"type": "shape", "id": "00000000-0000-0000-0000-00000000000e",
       "name": "Compound", "meta": {"ux": {}},
       "shape": {"type": "compound", "operation": "add",
         "path": "M 0 0 L 1 0 L 1 1 L 0 1 Z",
         "children": [
           {"type": "text", "id": "00000000-0000-0000-0000-0000000000f1",
            "text": {"rawText": ""}},
           {"type": "shape", "id": "00000000-0000-0000-0000-0000000000f2",
            "meta": {"ux": {}}, "shape": {"type": "rect", "x": 0, "y": 0,
              "width": 1, "height": 1}}]}}])");
    package.m_resources = parse_json(R"({"colorSwatches": [],
      "documentLibrary": {"version": 5, "elements": []},
      "futureSetting": {"on": true}})");
    package.m_interactions = parse_json(R"({"version": "0.24",
      "futureKey": [1]})");
    package.m_entries.emplace_back("resources/000000000000000000000000000000ff",
      QByteArray("\x89PNG\r\n\x1A\n", 8));
    package.m_entries.emplace_back("plugins/data.bin", QByteArray("raw"));
    return package;
  }

  const auto MANIFEST = std::string_view(R"({"state":"unmodified",)"
    R"("manifest-format-version":6,"name":"Design\u0001","children":[)"
    R"({"path":"artwork","name":"artwork",)"
    R"("id":"00000000-0000-0000-0000-000000000101","children":[)"
    R"({"path":"pasteboard","name":"pasteboard",)"
    R"("id":"00000000-0000-0000-0000-000000000102","children":[)"
    R"({"path":"graphics","name":"graphics",)"
    R"("id":"00000000-0000-0000-0000-000000000103","components":[)"
    R"({"path":"graphicContent.agc","name":"","state":"unmodified",)"
    R"("id":"00000000-0000-0000-0000-000000000104",)"
    R"("type":"application/vnd.adobe.agc.graphicsTree+json",)"
    R"("rel":"primary"}]}]}]},{"path":"resources","name":"resources",)"
    R"("id":"00000000-0000-0000-0000-000000000105","children":[)"
    R"({"path":"graphics","name":"graphics",)"
    R"("id":"00000000-0000-0000-0000-000000000106","components":[)"
    R"({"path":"graphicContent.agc","name":"","state":"unmodified",)"
    R"("id":"00000000-0000-0000-0000-000000000107",)"
    R"("type":"application/vnd.adobe.agc.graphicsTree+json",)"
    R"("rel":"primary"}]}]}],)"
    R"("uxdesign#parentDocId":"00000000-0000-0000-0000-000000000108",)"
    R"("id":"00000000-0000-0000-0000-000000000109",)"
    R"("uxdesign#compositeID":"00000000-0000-0000-0000-000000000109",)"
    R"("uxdesign#version":"2.33",)"
    R"("uxdesign#id":"00000000-0000-0000-0000-00000000010a","components":[)"
    R"({"path":"META-INF/metadata.xml","name":"xmp-metadata",)"
    R"("state":"unmodified","id":"00000000-0000-0000-0000-00000000010b",)"
    R"("type":"application/rdf+xml","rel":"metadata"}],)"
    R"("type":"application/vnd.adobe.sparkler.project+dcx",)"
    R"("uxdesign#serializationId":"00000000-0000-0000-0000-00000000010c",)"
    R"("uxdesign#applicationVersion":"61.0.12.1","scale":0.5})");

  const auto METADATA = std::string_view(
    R"(<rdf:Description xmpMM:DocumentID=")"
    R"(00000000-0000-0000-0000-000000000109" xmpMM:OriginalDocumentID=")"
    R"(00000000-0000-0000-0000-000000000109" xmpMM:InstanceID=")"
    R"(00000000-0000-0000-0000-00000000010d"/>)");

  QByteArray make_manifest_package() {
    auto package = XdTestPackage();
    package.m_pasteboard = parse_json_array(R"([
      {"type": "shape", "id": "00000000-0000-0000-0000-00000000000a",
       "name": "Kept", "meta": {"ux": {"hasCustomName": true}},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 10,
         "height": 10}}])");
    auto built = XdPackage(package.build());
    auto entries = std::vector<std::pair<std::string, QByteArray>>();
    for(auto& name : built.get_entries()) {
      if(name == XdPackage::MANIFEST) {
        entries.emplace_back(name, QByteArray(MANIFEST.data(),
          static_cast<qsizetype>(MANIFEST.size())));
      } else {
        entries.emplace_back(name, built.read(name));
      }
    }
    entries.emplace_back("META-INF/metadata.xml", QByteArray(METADATA.data(),
      static_cast<qsizetype>(METADATA.size())));
    return make_archive(entries);
  }

  std::vector<std::string> collect_ids(const QJsonValue& value) {
    auto ids = std::vector<std::string>();
    if(value.isArray()) {
      for(auto element : value.toArray()) {
        auto nested = collect_ids(element);
        ids.insert(ids.end(), nested.begin(), nested.end());
      }
    } else if(value.isObject()) {
      auto object = value.toObject();
      for(auto i = object.constBegin(); i != object.constEnd(); ++i) {
        if(i.key() == "id"_L1) {
          ids.push_back(i.value().toString().toStdString());
        } else {
          auto nested = collect_ids(i.value());
          ids.insert(ids.end(), nested.begin(), nested.end());
        }
      }
    }
    return ids;
  }

  NodeId to_id(const char* text) {
    return *parse_node_id(text);
  }

  QJsonObject find_child(const QJsonArray& children, const char* id) {
    for(auto child : children) {
      if(child.toObject().value("id"_L1).toString() ==
          QLatin1StringView(id)) {
        return child.toObject();
      }
    }
    return QJsonObject();
  }
}

TEST_SUITE("XdPreservation") {
  TEST_CASE("unknown_keys") {
    auto document = Document();
    import_package(make_package(), document);
    REQUIRE(!document.get(to_id(SHAPE)).m_header.m_opaque.is_empty());
    REQUIRE(!document.get_opaque().is_empty());
    auto package = XdPackage(write_xd(document));
    auto children = package.read_pasteboard().value("children"_L1).toArray();
    auto kept = find_child(children, SHAPE);
    REQUIRE(kept.value("plugin"_L1).toObject() ==
      parse_json(R"({"data": [1, 2, {"deep": true}]})"));
    auto ux = kept.value("meta"_L1).toObject().value("ux"_L1).toObject();
    REQUIRE(ux.value("pluginMetadata"_L1) == "xyz"_L1);
    REQUIRE(ux.value("hasCustomName"_L1).toBool());
    auto style = kept.value("style"_L1).toObject();
    REQUIRE(style.value("fill"_L1).toObject().value("uxdesign#type"_L1) ==
      "solid"_L1);
    REQUIRE(style.value("filters"_L1).toArray().first().toObject().value(
      "unknown"_L1).toInt() == 7);
    REQUIRE(kept.value("shape"_L1).toObject().value("extra"_L1) == "e"_L1);
    REQUIRE(kept.contains("visualBounds"_L1));
    REQUIRE(package.read("plugins/data.bin") == "raw");
    auto resources = package.read_resources().value("resources"_L1).
      toObject().value("meta"_L1).toObject().value("ux"_L1).toObject();
    REQUIRE(resources.value("futureSetting"_L1).toObject().value(
      "on"_L1).toBool());
    REQUIRE(package.read_interactions().value("futureKey"_L1).toArray().
      size() == 1);
  }

  TEST_CASE("edits") {
    auto document = Document();
    import_package(make_package(), document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(to_id(SHAPE)).m_header;
      header.m_name = "Renamed";
      transaction.replace_header(header);
      auto moved = document.get(to_id(MOVED)).m_header;
      moved.m_transform = make_translation(50, 60);
      transaction.replace_header(moved);
      auto filled = *document.get(to_id(FILLED)).get_shape();
      filled.m_style.m_fill = SolidFill(make_color(9, 9, 9));
      transaction.replace_payload(to_id(FILLED), filled);
      transaction.remove(to_id(DELETED));
      transaction.commit();
    }
    auto bytes = write_xd(document);
    auto children = XdPackage(bytes).read_pasteboard().value(
      "children"_L1).toArray();
    REQUIRE(children.size() == 4);
    auto kept = find_child(children, SHAPE);
    REQUIRE(kept.value("name"_L1) == "Renamed"_L1);
    REQUIRE(kept.value("plugin"_L1).isObject());
    REQUIRE(kept.contains("visualBounds"_L1));
    auto moved = find_child(children, MOVED);
    REQUIRE(moved.value("transform"_L1).toObject().value("tx"_L1).
      toDouble() == 50);
    REQUIRE(!moved.contains("visualBounds"_L1));
    REQUIRE(!moved.value("meta"_L1).toObject().value("ux"_L1).toObject().
      contains("rotation"_L1));
    auto filled = find_child(children, FILLED);
    auto fill = filled.value("style"_L1).toObject().value("fill"_L1).
      toObject();
    REQUIRE(fill.value("type"_L1) == "solid"_L1);
    REQUIRE(!fill.contains("pattern"_L1));
    REQUIRE(find_child(children, DELETED).isEmpty());
    auto reimported = Document();
    import_package(bytes, reimported);
    REQUIRE(reimported.get(to_id(SHAPE)).m_header.m_name == "Renamed");
    REQUIRE(reimported.get(to_id(MOVED)).m_header.m_transform ==
      make_translation(50, 60));
    REQUIRE(std::get<SolidFill>(reimported.get(
      to_id(FILLED)).get_shape()->m_style.m_fill).m_color ==
        make_color(9, 9, 9));
    REQUIRE(!reimported.find(to_id(DELETED)));
    auto again = Document();
    import_package(write_xd(reimported), again);
    REQUIRE(join(find_differences(reimported, again)) == "");
  }

  TEST_CASE("orphans") {
    auto document = Document();
    auto report = import_package(make_package(), document);
    REQUIRE(report.m_warnings.size() == 1);
    REQUIRE(!document.find(to_id("00000000-0000-0000-0000-0000000000f1")));
    auto children = XdPackage(write_xd(document)).read_pasteboard().value(
      "children"_L1).toArray();
    auto compound = find_child(children, COMPOUND).value("shape"_L1).
      toObject().value("children"_L1).toArray();
    REQUIRE(compound.size() == 2);
    REQUIRE(compound.at(0).toObject().value("type"_L1) == "text"_L1);
    REQUIRE(compound.at(1).toObject().value("type"_L1) == "shape"_L1);
  }

  TEST_CASE("manifest") {
    auto document = Document();
    import_package(make_manifest_package(), document);
    auto written = write_xd(document);
    REQUIRE(XdPackage(written).read(XdPackage::MANIFEST).toStdString() ==
      MANIFEST);
    REQUIRE(XdPackage(written).read("META-INF/metadata.xml").toStdString() ==
      METADATA);
    auto reimported = Document();
    import_package(written, reimported);
    REQUIRE(XdPackage(write_xd(reimported)).read(
      XdPackage::MANIFEST).toStdString() == MANIFEST);
  }

  TEST_CASE("new_composite") {
    auto document = Document();
    import_package(make_manifest_package(), document);
    auto options = XdWriteOptions();
    options.m_composite = "00000000-0000-0000-0000-0000000001ff";
    options.m_name = "Copy\x01";
    options.m_serialization = "00000000-0000-0000-0000-0000000001fe";
    auto written = XdPackage(write_xd(document, options));
    auto manifest = written.get_manifest();
    REQUIRE(manifest.value("id"_L1) ==
      "00000000-0000-0000-0000-0000000001ff"_L1);
    REQUIRE(manifest.value("uxdesign#compositeID"_L1) ==
      "00000000-0000-0000-0000-0000000001ff"_L1);
    REQUIRE(manifest.value("name"_L1) == "Copy\x01"_L1);
    REQUIRE(manifest.value("uxdesign#serializationId"_L1) ==
      "00000000-0000-0000-0000-0000000001fe"_L1);
    REQUIRE(manifest.value("uxdesign#id"_L1) ==
      "00000000-0000-0000-0000-00000000010a"_L1);
    REQUIRE(manifest.value("uxdesign#parentDocId"_L1) ==
      "00000000-0000-0000-0000-000000000108"_L1);
    auto original = collect_ids(parse_json(MANIFEST).value("children"_L1));
    auto ids = collect_ids(manifest.value("children"_L1));
    REQUIRE(ids.size() == original.size());
    for(auto& id : ids) {
      REQUIRE(parse_node_id(id));
      REQUIRE(std::find(original.begin(), original.end(), id) ==
        original.end());
    }
    auto text = written.read(XdPackage::MANIFEST).toStdString();
    REQUIRE(text.starts_with(R"({"state":"unmodified",)"
      R"("manifest-format-version":6,"name":"Copy\u0001","children":[)"));
    REQUIRE(text.ends_with(R"("uxdesign#applicationVersion":"61.0.12.1",)"
      R"("scale":0.5})"));
    REQUIRE(written.read("META-INF/metadata.xml").toStdString() ==
      R"(<rdf:Description xmpMM:DocumentID=")"
      R"(00000000-0000-0000-0000-0000000001ff" xmpMM:OriginalDocumentID=")"
      R"(00000000-0000-0000-0000-0000000001ff" xmpMM:InstanceID=")"
      R"(00000000-0000-0000-0000-00000000010d"/>)");
    REQUIRE(XdPackage(write_xd(document, options)).read(
      XdPackage::MANIFEST).toStdString() == text);
    options.m_composite = "00000000-0000-0000-0000-000000000109";
    REQUIRE(collect_ids(XdPackage(write_xd(document, options)).get_manifest().
      value("children"_L1)) == original);
  }

  TEST_CASE("unchanged_round_trip") {
    auto document = Document();
    import_package(make_package(), document);
    auto reimported = Document();
    import_package(write_xd(document), reimported);
    auto differences = find_differences(document, reimported);
    std::erase(differences, "document opaque data differs");
    REQUIRE(join(differences) == "");
    auto again = Document();
    import_package(write_xd(reimported), again);
    REQUIRE(join(find_differences(reimported, again)) == "");
  }
}

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <doctest/doctest.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include "Rellu/Io/XdClipboard.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  const auto RECTANGLE = std::string_view(R"({"version":"1.5.0","children":[)"
    R"({"type":"shape","name":"R1","meta":{"ux":{"sourceGuid":)"
    R"("739758a6-709b-420d-b4ad-4ddeec8ed555","hasCustomName":true}},)"
    R"("transform":{"a":1,"b":0,"c":0,"d":1,"tx":50,"ty":60},"style":)"
    R"({"fill":{"type":"solid","color":{"mode":"RGB","value":{"r":255,)"
    R"("g":0,"b":0}}},"stroke":{"type":"solid","color":{"mode":"RGB",)"
    R"("value":{"r":0,"g":0,"b":0},"alpha":0},"width":1,"align":)"
    R"("inside"}},"shape":{"type":"rect","x":0,"y":0,"width":100,)"
    R"("height":80}}],"resources":{"gradients":{},"clipPaths":{},)"
    R"("patterns":{}},"artboards":{},"viewSource":{"x":50,"y":60,)"
    R"("width":100,"height":80},"meta":{"ux":{"artboardOffset":[{"x":50,)"
    R"("y":60}],"artboardId":[847],"groupItems":false,)"
    R"("isCloudDocument":false}}})");

  const auto ARTBOARD = std::string_view(R"({"version":"1.5.0","children":[)"
    R"({"type":"artboard","meta":{"ux":{"hasCustomName":true}},"style":)"
    R"({"fill":{"type":"solid","color":{"mode":"RGB","value":{"r":255,)"
    R"("g":255,"b":255}}}},"artboard":{"children":[{"type":"shape",)"
    R"("name":"R1","transform":{"a":1,"b":0,"c":0,"d":1,"tx":3050,)"
    R"("ty":3060},"style":{"fill":{"type":"solid","color":{"mode":"RGB",)"
    R"("value":{"r":0,"g":0,"b":255}}}},"shape":{"type":"rect","x":0,)"
    R"("y":0,"width":10,"height":20}}],"ref":"artboard1"}}],)"
    R"("resources":{"gradients":{},"clipPaths":{},"patterns":{}},)"
    R"("artboards":{"artboard1":{"width":400,"height":600,"name":"A",)"
    R"("x":3000,"y":3000}},"viewSource":{"x":3000,"y":3000,"width":400,)"
    R"("height":600},"meta":{"ux":{"groupItems":false}}})");

  const auto INSTANCE = std::string_view(R"({"version":"1.5.0","children":[)"
    R"({"type":"group","name":"Component 1 - 2","meta":{"ux":{)"
    R"("nameL10N":"SHAPE_COMPONENT","sourceGuid":)"
    R"("eefaccbc-2f8a-45b3-adf2-ee7ca06b4da7","symbolId":)"
    R"("c268a1cc-bcd1-44bf-840f-ab0a1b5693da","width":747,"height":575,)"
    R"("componentType":"componentNoBounds","symbolOnClipboard":true,)"
    R"("definitionModTime":1792605639514,"syncMap":{)"
    R"("a8fd85dd-7929-45d6-a888-f11113ef6f79":)"
    R"("ce7e9567-98d4-4162-b8bc-eafc0d0d8eda"}}},"transform":{"a":1,"b":0,)"
    R"("c":0,"d":1,"tx":253,"ty":181},"group":{"children":[{"type":)"
    R"("shape","name":"Rectangle 1","meta":{"ux":{"nameL10N":)"
    R"("SHAPE_RECTANGLE","sourceGuid":)"
    R"("ce7e9567-98d4-4162-b8bc-eafc0d0d8eda"}},"transform":{"a":1,"b":0,)"
    R"("c":0,"d":1,"tx":0,"ty":0},"style":{"fill":{"type":"solid",)"
    R"("color":{"mode":"RGB","value":{"r":255,"g":255,"b":255}}}},"shape":)"
    R"({"type":"rect","x":0,"y":0,"width":747,"height":575}}]}}],)"
    R"("resources":{"gradients":{},"clipPaths":{},"patterns":{},"meta":{)"
    R"("ux":{"symbols":[{"type":"group","name":"Component 1","meta":{"ux":)"
    R"({"nameL10N":"SHAPE_COMPONENT","symbolId":)"
    R"("c268a1cc-bcd1-44bf-840f-ab0a1b5693da","width":747,"height":575,)"
    R"("componentType":"componentNoBounds","symbolOnClipboard":true,)"
    R"("definitionModTime":1792605639514,"modTime":1792605639514}},"id":)"
    R"("7199f987-8e6d-44f1-b3de-9cbda06bd6ab","transform":{"a":1,"b":0,)"
    R"("c":0,"d":1,"tx":0,"ty":0},"group":{"children":[{"type":"shape",)"
    R"("name":"Rectangle 1","meta":{"ux":{"nameL10N":"SHAPE_RECTANGLE"}},)"
    R"("id":"a8fd85dd-7929-45d6-a888-f11113ef6f79","transform":{"a":1,)"
    R"("b":0,"c":0,"d":1,"tx":0,"ty":0},"style":{"fill":{"type":"solid",)"
    R"("color":{"mode":"RGB","value":{"r":255,"g":255,"b":255}}}},"shape":)"
    R"({"type":"rect","x":0,"y":0,"width":747,"height":575}}]}}]}}},)"
    R"("artboards":{},"viewSource":{"x":253,"y":181,"width":747,)"
    R"("height":575},"meta":{"ux":{"artboardOffset":[{"x":253,"y":181}],)"
    R"("artboardId":[815],"groupItems":false}}})");

  QJsonObject decode(const QByteArray& data) {
    auto text = QString::fromUtf16(
      reinterpret_cast<const char16_t*>(data.constData()), data.size() / 2);
    text.chop(1);
    return QJsonDocument::fromJson(text.toUtf8()).object();
  }

  QJsonObject get_ux(const QJsonValue& node) {
    return node.toObject().value("meta").toObject().value("ux").toObject();
  }

  QByteArray encode(std::string_view json) {
    auto text = QString::fromUtf8(json.data(),
      static_cast<qsizetype>(json.size()));
    text.append(QChar(0));
    return QByteArray(reinterpret_cast<const char*>(text.utf16()),
      text.size() * static_cast<qsizetype>(sizeof(char16_t)));
  }
}

TEST_SUITE("XdClipboard") {
  TEST_CASE("read") {
    auto document = Document();
    auto content = read_xd_clipboard(encode(RECTANGLE), document);
    REQUIRE(document.get_canvas().size() == 1);
    auto& node = document.get(document.get_canvas().front());
    REQUIRE(node.m_header.m_name == "R1");
    REQUIRE(node.m_header.m_transform == make_translation(50, 60));
    REQUIRE(node.get_shape());
    REQUIRE(content.m_offsets.size() == 1);
    REQUIRE(content.m_offsets.front() == Point(50, 60));
    REQUIRE(content.m_bounds == Rect(50, 60, 100, 80));
  }

  TEST_CASE("read_artboard") {
    auto document = Document();
    auto content = read_xd_clipboard(encode(ARTBOARD), document);
    REQUIRE(content.m_offsets.empty());
    REQUIRE(document.get_canvas().size() == 1);
    auto& node = document.get(document.get_canvas().front());
    REQUIRE(node.m_header.m_name == "A");
    REQUIRE(node.get_artboard()->m_frame == Rect(3000, 3000, 400, 600));
    REQUIRE(node.m_children.size() == 1);
    REQUIRE(document.get(node.m_children.front()).m_header.m_transform ==
      make_translation(50, 60));
  }

  TEST_CASE("read_component") {
    auto document = Document();
    read_xd_clipboard(encode(INSTANCE), document);
    REQUIRE(document.get_canvas().size() == 1);
    auto& node = document.get(document.get_canvas().front());
    auto& instance = *node.get_group()->m_component_instance;
    auto& components = document.get_resources().m_components;
    REQUIRE(components.contains(instance.m_component));
    auto& master = components.at(instance.m_component).m_master;
    REQUIRE(master ==
      *parse_node_id("7199f987-8e6d-44f1-b3de-9cbda06bd6ab"));
    auto child = *parse_node_id("ce7e9567-98d4-4162-b8bc-eafc0d0d8eda");
    REQUIRE(node.m_children == std::vector<NodeId>({child}));
    REQUIRE(instance.m_sync_map.at(child) ==
      *parse_node_id("a8fd85dd-7929-45d6-a888-f11113ef6f79"));
  }

  TEST_CASE("write_component_and_image") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto image = ImageResource();
    image.m_id = generate_node_id();
    image.m_media_type = "image/png";
    image.m_data = Shared(std::vector<std::byte>({std::byte(7)}));
    transaction.add_image(image);
    auto master_header = NodeHeader();
    master_header.m_id = generate_node_id();
    auto master = transaction.insert_root(master_header, Group());
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, 10, 10));
    auto fill = ImageFill();
    fill.m_image = image.m_id;
    fill.m_size = Size(1, 1);
    shape.m_style.m_fill = fill;
    auto shape_header = NodeHeader();
    shape_header.m_id = generate_node_id();
    auto master_shape = transaction.append(master, shape_header, shape);
    auto component = Component();
    component.m_master = master;
    component.m_size = Size(10, 10);
    auto id = transaction.add_component(component);
    auto group = Group();
    group.m_component_instance = ComponentInstance(id, StateId(),
      Size(10, 10));
    auto instance_header = NodeHeader();
    instance_header.m_id = generate_node_id();
    auto instance = transaction.append(NodeId(), instance_header, group);
    auto copy_header = NodeHeader();
    copy_header.m_id = generate_node_id();
    auto copy = transaction.append(instance, copy_header, shape);
    auto instance_group = *document.get(instance).get_group();
    instance_group.m_component_instance->m_sync_map.emplace(
      copy, master_shape);
    transaction.replace_payload(instance, instance_group);
    transaction.commit();
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto json = decode(write_xd_clipboard(document,
      {XdClipboardItem(instance, Matrix(), std::nullopt)},
      Rect(0, 0, 10, 10), {},
      std::filesystem::path(directory.path().toStdU16String())));
    auto node = json.value("children").toArray().at(0).toObject();
    REQUIRE(!node.contains("id"));
    REQUIRE(get_ux(node).value("sourceGuid").toString().toStdString() ==
      to_string(instance));
    REQUIRE(get_ux(node).value("symbolOnClipboard").toBool());
    auto symbols = get_ux(json.value("resources")).value("symbols").toArray();
    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols.at(0).toObject().value("id").toString().toStdString() ==
      to_string(master));
    REQUIRE(get_ux(symbols.at(0)).value("symbolOnClipboard").toBool());
    auto pattern = symbols.at(0).toObject().value("group").toObject().value(
      "children").toArray().at(0).toObject().value("style").toObject().
      value("fill").toObject().value("pattern").toObject();
    auto file = QFile(pattern.value("href").toString());
    REQUIRE(file.open(QIODevice::ReadOnly));
    REQUIRE(file.readAll() == QByteArray(1, 7));
  }

  TEST_CASE("invalid") {
    auto document = Document();
    REQUIRE_THROWS_AS(read_xd_clipboard(encode("[1, 2]"), document),
      std::invalid_argument);
    REQUIRE_THROWS_AS(read_xd_clipboard(QByteArray("text"), document),
      std::invalid_argument);
  }

  TEST_CASE("round_trip") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 300, 200);
    auto board = transaction.append(NodeId(), NodeHeader(), artboard);
    auto header = NodeHeader();
    header.m_name = "Box";
    header.m_has_custom_name = true;
    header.m_transform = make_translation(10, 20);
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, 30, 40));
    shape.m_style.m_fill = SolidFill(make_color(1, 2, 3));
    auto box = transaction.append(board, header, shape);
    transaction.commit();
    auto data = write_xd_clipboard(document, {XdClipboardItem(box,
      make_translation(110, 70), Point(10, 20))}, Rect(110, 70, 30, 40));
    REQUIRE(data.endsWith(QByteArray(2, '\0')));
    auto copy = Document();
    auto content = read_xd_clipboard(data, copy);
    REQUIRE(content.m_offsets.front() == Point(10, 20));
    REQUIRE(content.m_bounds == Rect(110, 70, 30, 40));
    auto& node = copy.get(copy.get_canvas().front());
    REQUIRE(node.m_header.m_name == "Box");
    REQUIRE(node.m_header.m_transform == make_translation(110, 70));
    REQUIRE(node.get_shape()->m_geometry == shape.m_geometry);
    REQUIRE(node.get_shape()->m_style.m_fill == shape.m_style.m_fill);
  }
}

#include <stdexcept>
#include <string_view>
#include <doctest/doctest.h>
#include <QString>
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

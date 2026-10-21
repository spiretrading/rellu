#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QJsonObject>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/NodeId.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  std::string make_stroke(const std::string& rgb, const std::string& alpha) {
    return R"({"type":"solid","color":{"mode":"RGB","value":)" + rgb +
      R"(,"alpha":)" + alpha + "}}";
  }

  std::string make_entry(const std::string& id, int width, int columns,
      int gutter, int margin) {
    auto square = make_stroke(R"({"r":199,"g":199,"b":199})",
      "0.5019607843137255");
    auto layout =
      make_stroke(R"({"r":0,"g":255,"b":255})", "0.25098039215686274");
    return R"({"version":"1.5.0","children":[{"type":"artboard",)"
      R"("meta":{"ux":{"nameL10N":"SHAPE_RECTANGLE",)"
      R"("constraintsDisabled":true,"gridStyle":{"rowStroke":)" + square +
      R"(,"columnStroke":)" + square +
      R"(,"rowSpacing":8,"columnSpacing":8,"defaultLayoutWidth":)" +
      std::to_string(width) + R"(,"columns":)" + std::to_string(columns) +
      R"(,"gutter":)" + std::to_string(gutter) + R"(,"marginLeft":)" +
      std::to_string(margin) + R"(,"marginTop":0,"marginRight":)" +
      std::to_string(margin) + R"(,"marginBottom":0,"layoutRowStroke":)" +
      layout + R"(,"layoutColumnStroke":)" + layout + R"(}}},"id":")" + id +
      R"(","style":{"fill":{"type":"solid","color":{"mode":"RGB",)"
      R"("value":{"r":255,"g":255,"b":255}}}},"artboard":{"children":[],)"
      R"("meta":{"ux":{"path":")" + id + R"("}},"ref":")" + id +
      R"("}}],"resources":{"href":"/resources/graphics/)"
      R"(graphicContent.agc"},"artboards":{"href":"/resources/graphics/)"
      R"(graphicContent.agc"}})";
  }

  ArtboardPreset make_custom_size(double width, double height) {
    auto preset = ArtboardPreset();
    preset.m_name = "Custom Size";
    preset.m_size = Size(width, height);
    return preset;
  }

  struct Written {
    std::string m_id;
    std::string m_text;
    QJsonObject m_frame;
  };

  Written create(const ArtboardPreset& preset) {
    auto document = Document();
    auto history = History(document);
    auto command = history.execute(std::make_unique<CreateArtboardCommand>(
      preset, std::vector<NodeId>(), Rect(0, 0, 1600, 900)));
    REQUIRE(command);
    auto id = to_string(command->get_selection()->front());
    auto package = XdPackage(write_xd(document));
    auto text = package.read(
      "artwork/artboard-" + id + "/graphics/graphicContent.agc").
      toStdString();
    auto written = Written();
    written.m_id = id;
    written.m_text = std::move(text);
    written.m_frame = package.read_resources().value("artboards"_L1).
      toObject().value(QString::fromStdString(id)).toObject();
    return written;
  }
}

TEST_SUITE("ArtboardGrid") {
  TEST_CASE("preset") {
    auto web = get_artboard_presets()[2].m_presets[0];
    REQUIRE(web.m_name == "Web 1920");
    auto written = create(web);
    REQUIRE(written.m_text == make_entry(written.m_id, 1920, 12, 16, 140));
    REQUIRE(written.m_frame.value("viewportHeight"_L1).toDouble() == 1080);
  }

  TEST_CASE("custom_size") {
    auto small = create(make_custom_size(200, 100));
    REQUIRE(small.m_text == make_entry(small.m_id, 200, 1, 0, 28));
    REQUIRE(small.m_frame.value("viewportHeight"_L1).toDouble() == 100);
    auto large = create(make_custom_size(1200, 800));
    REQUIRE(large.m_text == make_entry(large.m_id, 1200, 12, 16, 140));
    REQUIRE(large.m_frame.value("viewportHeight"_L1).toDouble() == 800);
  }
}

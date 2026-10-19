#include <cstring>
#include <doctest/doctest.h>
#include "Rellu/Io/XdValues.hpp"
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("ResourceImporter") {
  TEST_CASE("images") {
    auto package = XdTestPackage();
    package.m_entries.emplace_back(
      "resources/09990725aa1fb4caa6b9fecb4a18e30a", QByteArray("\x89PNG\r\n"));
    package.m_entries.emplace_back(
      "resources/f46a9711a3f6510dd2ddb8b7509238fc", QByteArray("\xFF\xD8\xFF"));
    package.m_entries.emplace_back(
      "resources/5a3efb1658aabb1ae3bd2ad889ac5876", QByteArray("GIF89a"));
    package.m_entries.emplace_back("resources/bc4e63905ff05d3f1e4677a1bab292f5"
      ".mp4", QByteArray("\x00\x00\x00 ftypisom", 12));
    package.m_entries.emplace_back("resources/short", QByteArray("data"));
    auto document = Document();
    auto report = import_package(package, document);
    REQUIRE(report.m_warnings.empty());
    auto& images = document.get_resources().m_images;
    REQUIRE(images.size() == 5);
    auto png = images.at(*parse_node_id("09990725aa1fb4caa6b9fecb4a18e30a"));
    REQUIRE(png.m_media_type == "image/png");
    REQUIRE(png.m_data->size() == 6);
    REQUIRE(std::memcmp(png.m_data->data(), "\x89PNG\r\n", 6) == 0);
    REQUIRE(images.at(*parse_node_id("f46a9711a3f6510dd2ddb8b7509238fc")).
      m_media_type == "image/jpeg");
    REQUIRE(images.at(*parse_node_id("5a3efb1658aabb1ae3bd2ad889ac5876")).
      m_media_type == "image/gif");
    REQUIRE(images.at(*parse_node_id("bc4e63905ff05d3f1e4677a1bab292f5")).
      m_media_type == "video/mp4");
    REQUIRE(images.at(to_node_id(u"short")).m_media_type ==
      "application/octet-stream");
  }

  TEST_CASE("swatches") {
    auto package = XdTestPackage();
    package.m_resources = parse_json(R"({"colorSwatches": [{"mode": "RGB",
      "value": {"r": 255, "g": 187, "b": 0}}, {"mode": "RGB", "value": {
      "r": 0, "g": 102, "b": 255}, "alpha": 0.5}], "documentLibrary": {
      "version": 5, "isStickerSheet": false, "hashedMetadata": {},
      "groupData": {"groups": [{"name": "Brand", "id": "g1", "order": "an",
      "parentId": "groupColors", "childrenIds": [],
      "elementIds": ["12c4c8f8-9852-4ac8-95a8-45b41e9a919a"]}]},
      "elements": [{
      "id": "95cac2c3-b0fc-4791-96a9-b066dd1742a5",
      "type": "application/vnd.adobe.element.color+dcx", "name": "Gray",
      "created": 1, "modified": 2, "displayName": null, "index": 4,
      "updateElement": 0, "assetGroupData": {"groupColors": "b"},
      "representations": [{"id": "ff5907fe-706a-4e88-9b45-64c34740de43",
      "type": "application/vnd.adobe.xdcolor+json", "relationship": "primary",
      "content": {"mode": "RGB", "value": {"r": 245, "g": 245, "b": 245}}}]},
      {"id": "12c4c8f8-9852-4ac8-95a8-45b41e9a919a",
      "type": "application/vnd.adobe.element.gradient+dcx", "name": "Fade",
      "assetGroupData": {"g1": "n"},
      "representations": [{"content": [{"color": {"mode": "RGB", "value": {
      "r": 255, "g": 0, "b": 0}}, "stop": 0}, {"color": {"mode": "RGB",
      "value": {"r": 0, "g": 0, "b": 255}}, "stop": 1}]}]}]},
      "gridDefaults": {"defaultGrid": null}, "colorProfile": "srgb"})");
    auto document = Document();
    auto report = import_package(package, document);
    REQUIRE(report.m_unconsumed.empty());
    auto& swatches = document.get_resources().m_swatches;
    REQUIRE(swatches.size() == 4);
    REQUIRE(swatches[0].m_color == make_color(255, 187, 0));
    REQUIRE(swatches[1].m_color == make_color(0, 102, 255, 0.5));
    REQUIRE(swatches[0].m_id != swatches[1].m_id);
    REQUIRE(swatches[2] == ColorSwatch(
      *parse_node_id("95cac2c3-b0fc-4791-96a9-b066dd1742a5"), "Gray",
      make_color(245, 245, 245), std::vector<GradientStop>(),
      std::vector{AssetPlacement("groupColors", "b")}));
    REQUIRE(swatches[3].m_name == "Fade");
    REQUIRE(swatches[3].m_color == Color(1, 0, 0, 1));
    REQUIRE(swatches[3].m_gradient == std::vector<GradientStop>({
      GradientStop(0, Color(1, 0, 0, 1)), GradientStop(1, Color(0, 0, 1, 1))}));
    REQUIRE(swatches[3].m_placements ==
      std::vector{AssetPlacement("g1", "n")});
    REQUIRE(document.get_resources().m_asset_groups == std::vector{
      AssetGroup("g1", "Brand", "an", "groupColors",
        std::vector<std::string>(),
        std::vector<std::string>({"12c4c8f8-9852-4ac8-95a8-45b41e9a919a"}))});
  }

  TEST_CASE("character_styles") {
    auto package = XdTestPackage();
    package.m_resources = parse_json(R"({"documentLibrary": {"elements": [{
      "id": "08e6689b-c9f5-48d9-9781-ff805cc2e2de",
      "type": "application/vnd.adobe.element.characterstyle+dcx",
      "name": "Heading", "representations": [{"content": {
      "fontFamily": "Roboto", "fontStyle": "Medium", "fontSize": 14,
      "fontColor": {"mode": "RGB", "value": {"r": 0, "g": 0, "b": 0}},
      "charSpacing": 20, "lineSpacing": 18, "linkId": "", "underline": true,
      "strikethrough": false, "textTransform": "uppercase",
      "textScript": "subscript", "postscriptName": "Roboto-Medium",
      "paragraphSpacing": null}}]}]}})");
    auto document = Document();
    auto report = import_package(package, document);
    REQUIRE(report.m_unconsumed.empty());
    auto& styles = document.get_resources().m_character_styles;
    REQUIRE(styles.size() == 1);
    auto& style = styles.front();
    REQUIRE(style.m_id ==
      *parse_node_id("08e6689b-c9f5-48d9-9781-ff805cc2e2de"));
    REQUIRE(style.m_name == "Heading");
    REQUIRE(style.m_style.m_font ==
      Font("Roboto", "Medium", "Roboto-Medium", 14));
    REQUIRE(style.m_style.m_letter_spacing == 20);
    REQUIRE(style.m_style.m_is_underlined);
    REQUIRE(style.m_style.m_transform == TextTransform::UPPERCASE);
    REQUIRE(style.m_style.m_script == TextScript::SUBSCRIPT);
    REQUIRE(style.m_line_height == 18);
    REQUIRE(style.m_paragraph_spacing == 0);
  }
}

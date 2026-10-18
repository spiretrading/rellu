#include <doctest/doctest.h>
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto GROUP_ID = *parse_node_id("5a84f6ec-cee8-485b-967e-3fcf33dfdcea");
  const auto FIRST_ID = *parse_node_id("9d8aedae-2c61-4929-b5c8-90430c38cbc8");
  const auto SECOND_ID =
    *parse_node_id("09efb50d-acb7-47a2-a45e-5017439ce00e");

  const Group& import_group(Document& document, const char* ux,
      ImportReport* report = nullptr) {
    auto result = import_nodes(std::string(R"([{"type": "group",
      "id": "5a84f6ec-cee8-485b-967e-3fcf33dfdcea", "meta": {"ux": )") + ux +
      R"(}, "group": {"children": [{"type": "shape",
      "id": "9d8aedae-2c61-4929-b5c8-90430c38cbc8",
      "meta": {"ux": {"contentStackMarginAfter": 8}}, "shape": {
      "type": "rect", "x": 0, "y": 0, "width": 10, "height": 10}},
      {"type": "syncRef", "guid": "09efb50d-acb7-47a2-a45e-5017439ce00e",
      "syncSourceGuid": "50350512-5853-40a8-9706-54c544cf26a4"}]}}])",
      document);
    if(report) {
      *report = std::move(result);
    }
    return *document.get(GROUP_ID).get_group();
  }
}

TEST_SUITE("GroupImporter") {
  TEST_CASE("children") {
    auto document = Document();
    auto& group = import_group(document, "{}");
    REQUIRE(group == Group());
    REQUIRE(document.get_children(GROUP_ID) ==
      std::vector<NodeId>({FIRST_ID, SECOND_ID}));
  }

  TEST_CASE("mask") {
    auto document = Document();
    auto report = ImportReport();
    auto& group = import_group(document, R"({"clipPathResources": {
      "type": "clipPath", "children": [{"type": "shape", "name": "Mask",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd", "meta": {"ux": {
      "nameL10N": "SHAPE_RECTANGLE"}}, "transform": {"a": 1, "b": 0, "c": 0,
      "d": 1, "tx": 5, "ty": 6}, "style": {"fill": {"type": "solid"}},
      "shape": {"type": "rect", "x": 0, "y": 0, "width": 10,
      "height": 20}}]}})", &report);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(group.m_mask == Mask(std::vector<MaskShape>({
      MaskShape(make_translation(5, 6),
      RectangleShape(Rect(0, 0, 10, 20), CornerRadii(0, 0, 0, 0)))})));
  }

  TEST_CASE("repeat_grid") {
    auto document = Document();
    auto& group = import_group(document, R"({"repeatGrid": {"width": 216,
      "height": 622, "paddingX": 20, "paddingY": 4, "cellWidth": 216,
      "cellHeight": 26, "columns": 1, "rows": 24}})");
    REQUIRE(group.m_repeat_grid == RepeatGrid(Size(216, 26), 20, 4, 1, 24,
      Size(216, 622)));
  }

  TEST_CASE("scroll_group") {
    auto document = Document();
    auto& group = import_group(document, R"({"scrollingType": "panning",
      "offsetX": 20, "viewportWidth": 825, "offsetY": 24,
      "viewportHeight": 494})");
    REQUIRE(group.m_scroll_group == ScrollGroup(ScrollDirection::PANNING,
      Size(825, 494), Point(20, 24)));
    auto other = Document();
    auto& vertical = import_group(other, R"({"scrollingType": "vertical",
      "viewportHeight": 260})");
    REQUIRE(vertical.m_scroll_group == ScrollGroup(ScrollDirection::VERTICAL,
      Size(0, 260), Point(0, 0)));
  }

  TEST_CASE("stack") {
    auto document = Document();
    auto report = ImportReport();
    auto& group = import_group(document, R"({"contentStackType": "horizontal",
      "isContentAware": true, "hasContentBackground": true,
      "contentPadding": {"top": 1, "right": 2, "bottom": 3, "left": 4}})",
      &report);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(group.m_stack == Stack(Orientation::HORIZONTAL,
      std::map<NodeId, double>({{FIRST_ID, 8}}), Margins(1, 2, 3, 4), true));
    REQUIRE(!group.m_padding);
  }

  TEST_CASE("padding") {
    auto document = Document();
    auto& group = import_group(document, R"({"isContentAware": true,
      "hasContentBackground": true, "contentPadding": {"top": 5, "right": 6,
      "bottom": 7, "left": 8}})");
    REQUIRE(group.m_padding == Padding(Margins(5, 6, 7, 8), true, true));
    REQUIRE(!group.m_stack);
  }
}

#include <doctest/doctest.h>
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto ARTBOARD_ID =
    *parse_node_id("39933d05-e40a-4c79-a8ec-deda4884ac82");
  const auto OTHER_ID = *parse_node_id("1f8922ab-d204-4ea7-82ac-4c9e480f3170");
  const auto CHILD_ID = *parse_node_id("5a84f6ec-cee8-485b-967e-3fcf33dfdcea");
  const auto LOOSE_ID = *parse_node_id("9d8aedae-2c61-4929-b5c8-90430c38cbc8");

  XdTestPackage make_package() {
    auto package = XdTestPackage();
    package.m_frames = parse_json(R"({
      "39933d05-e40a-4c79-a8ec-deda4884ac82": {"width": 256, "height": 128,
      "name": "Main", "x": 698, "y": -389, "viewportHeight": 600},
      "1f8922ab-d204-4ea7-82ac-4c9e480f3170": {"width": 10, "height": 10,
      "name": "Other", "x": 0, "y": 0}})");
    package.m_artboards.push_back(parse_json(R"({"type": "artboard",
      "id": "39933d05-e40a-4c79-a8ec-deda4884ac82", "meta": {"ux": {
      "hasCustomName": true, "constraintsDisabled": false,
      "gridStyle": {"rowStroke": {"type": "solid", "color": {"mode": "RGB",
      "value": {"r": 199, "g": 199, "b": 199}, "alpha": 0.5}},
      "columnStroke": {"type": "solid", "color": {"mode": "RGB",
      "value": {"r": 199, "g": 199, "b": 199}, "alpha": 0.25}},
      "rowSpacing": 8, "columnSpacing": 9, "defaultLayoutWidth": 256,
      "columns": 4, "gutter": 12, "marginLeft": 28, "marginTop": 1,
      "marginRight": 27, "marginBottom": 2, "layoutRowStroke": {
      "type": "solid", "color": {"mode": "RGB", "value": {"r": 0, "g": 255,
      "b": 255}, "alpha": 0.5}}, "layoutColumnStroke": {"type": "solid",
      "color": {"mode": "RGB", "value": {"r": 255, "g": 0, "b": 255},
      "alpha": 0.5}}, "type": "layout", "visible": false},
      "guidesModel": {"horizontalGuides": {"guides": [{"position": 10,
      "guid": "a"}]}, "verticalGuides": {"guides": [{"position": 20,
      "guid": "b"}]}}}},
      "style": {"fill": {"type": "none"}},
      "artboard": {"children": [{"type": "group",
      "id": "5a84f6ec-cee8-485b-967e-3fcf33dfdcea",
      "transform": {"a": 1, "b": 0, "c": 0, "d": 1, "tx": 708, "ty": -379},
      "meta": {"ux": {"localTransform": {"a": 1, "b": 0, "c": 0, "d": 1,
      "tx": 10, "ty": 10}}}}],
      "meta": {"ux": {"path": "39933d05-e40a-4c79-a8ec-deda4884ac82"}},
      "ref": "39933d05-e40a-4c79-a8ec-deda4884ac82"}})"));
    package.m_artboards.push_back(parse_json(R"({"type": "artboard",
      "id": "1f8922ab-d204-4ea7-82ac-4c9e480f3170",
      "artboard": {"ref": "1f8922ab-d204-4ea7-82ac-4c9e480f3170"}})"));
    package.m_pasteboard = parse_json_array(R"([{"type": "group",
      "id": "9d8aedae-2c61-4929-b5c8-90430c38cbc8",
      "transform": {"a": 1, "b": 0, "c": 0, "d": 1, "tx": 708,
      "ty": -379}}])");
    return package;
  }
}

TEST_SUITE("ArtboardImporter") {
  TEST_CASE("artboard") {
    auto document = Document();
    auto report = import_package(make_package(), document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(report.m_warnings.empty());
    REQUIRE(document.get_canvas() ==
      std::vector<NodeId>({ARTBOARD_ID, OTHER_ID, LOOSE_ID}));
    auto& node = document.get(ARTBOARD_ID);
    REQUIRE(node.m_header.m_name == "Main");
    REQUIRE(node.m_header.m_constraints.m_is_enabled);
    auto& artboard = *node.get_artboard();
    REQUIRE(artboard.m_frame == Rect(698, -389, 256, 128));
    REQUIRE(artboard.m_viewport_height == 600);
    REQUIRE(std::holds_alternative<NoFill>(artboard.m_background));
    REQUIRE(artboard.m_guides == std::vector<Guide>({
      Guide(Orientation::HORIZONTAL, 10), Guide(Orientation::VERTICAL, 20)}));
    auto& grid = *artboard.m_layout_grid;
    REQUIRE(grid.m_type == GridType::LAYOUT);
    REQUIRE(!grid.m_is_visible);
    REQUIRE(grid.m_column_count == 4);
    REQUIRE(grid.m_gutter == 12);
    REQUIRE(grid.m_default_width == 256);
    REQUIRE(grid.m_margins == Margins(1, 27, 2, 28));
    REQUIRE(grid.m_column_color == Color(1, 0, 1, 0.5));
    REQUIRE(grid.m_row_color == Color(0, 1, 1, 0.5));
    REQUIRE(grid.m_square_size == Size(9, 8));
    REQUIRE(grid.m_square_column_color == make_color(199, 199, 199, 0.25));
    REQUIRE(grid.m_square_row_color == make_color(199, 199, 199, 0.5));
    auto& other = *document.get(OTHER_ID).get_artboard();
    REQUIRE(!other.m_viewport_height);
    REQUIRE(!other.m_layout_grid);
  }

  TEST_CASE("children_offset") {
    auto document = Document();
    import_package(make_package(), document);
    REQUIRE(document.get_children(ARTBOARD_ID) ==
      std::vector<NodeId>({CHILD_ID}));
    REQUIRE(document.get(CHILD_ID).m_header.m_transform ==
      make_translation(10, 10));
    REQUIRE(document.get(LOOSE_ID).m_header.m_transform ==
      make_translation(708, -379));
  }

  TEST_CASE("missing_frame") {
    auto package = make_package();
    package.m_frames = QJsonObject();
    auto document = Document();
    auto report = import_package(package, document);
    REQUIRE(report.m_warnings.at("Artboard without a frame") == 2);
    REQUIRE(document.get(ARTBOARD_ID).get_artboard()->m_frame ==
      Rect(0, 0, 0, 0));
  }
}

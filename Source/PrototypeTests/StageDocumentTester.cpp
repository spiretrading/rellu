#include <doctest/doctest.h>
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Prototype/StageDocument.hpp"
#include "../IoTests/XdTestPackage.hpp"
#include "Stage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId id(const char* text) {
    return *parse_node_id(text);
  }

  const auto BOARD_ID = id("39933d05-e40a-4c79-a8ec-deda4884ac82");
  const auto FIXED_ID = id("9d8aedae-2c61-4929-b5c8-90430c38cbc8");
  const auto GROUP_ID = id("a650ae17-29ed-4167-b3c4-1a7883857f22");
  const auto NESTED_ID = id("345b49ae-25ab-4510-8001-2278ccf9c1bc");
  const auto LOOSE_ID = id("e636e1d1-c8ff-4610-9405-f65119cdcf77");

  XdTestPackage make_package() {
    auto package = XdTestPackage();
    package.m_frames = parse_json(R"({
      "39933d05-e40a-4c79-a8ec-deda4884ac82": {"x": 0, "y": 0, "width": 100,
      "height": 400, "viewportHeight": 100, "name": "Board"}})");
    package.m_artboards.push_back(parse_json(R"({"type": "artboard",
      "id": "39933d05-e40a-4c79-a8ec-deda4884ac82", "artboard": {
      "ref": "39933d05-e40a-4c79-a8ec-deda4884ac82", "children": [{
      "type": "shape", "id": "e636e1d1-c8ff-4610-9405-f65119cdcf77",
      "shape": {"type": "rect", "x": 0, "y": 0, "width": 10,
      "height": 10}}, {
      "type": "shape", "id": "9d8aedae-2c61-4929-b5c8-90430c38cbc8",
      "transform": {"a": 1, "b": 0, "c": 0, "d": 1, "tx": 0, "ty": 80},
      "meta": {"ux": {"fixed": true}}, "shape": {"type": "rect", "x": 0,
      "y": 0, "width": 100, "height": 20}}, {
      "type": "group", "id": "a650ae17-29ed-4167-b3c4-1a7883857f22",
      "transform": {"a": 1, "b": 0, "c": 0, "d": 1, "tx": 10, "ty": 5},
      "group": {"children": [{"type": "shape",
      "id": "345b49ae-25ab-4510-8001-2278ccf9c1bc",
      "transform": {"a": 1, "b": 0, "c": 0, "d": 1, "tx": 20, "ty": 0},
      "meta": {"ux": {"fixed": true}}, "shape": {"type": "rect", "x": 0,
      "y": 0, "width": 10, "height": 10}}]}}]}})"));
    return package;
  }
}

TEST_SUITE("StageDocument") {
  TEST_CASE("copy") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      make_default_interaction(scene.m_button, scene.m_second).m_action);
    add_interaction(document, scene.m_instance, HoverTrigger(),
      StateTransition(scene.m_hover, Animation()));
    auto transaction = Transaction(document);
    transaction.set_flows({Flow(scene.m_home, "Flow 1")});
    transaction.set_home_artboard(scene.m_home);
    transaction.commit();
    auto copy = copy_document(document);
    REQUIRE(find_differences(document, *copy).empty());
    REQUIRE(copy->get_prototype() == document.get_prototype());
    REQUIRE(copy->get_resources() == document.get_resources());
    REQUIRE(copy->get_canvas() == document.get_canvas());
    REQUIRE(copy->get_undo_count() == 0);
  }

  TEST_CASE("fixed_nodes") {
    auto document = Document();
    import_package(make_package(), document);
    REQUIRE(find_fixed_nodes(document) ==
      std::unordered_set<NodeId>({FIXED_ID, NESTED_ID}));
    auto plain = Document();
    build_prototype_scene(plain);
    REQUIRE(find_fixed_nodes(plain).empty());
  }

  TEST_CASE("fixed_layers") {
    auto document = Document();
    import_package(make_package(), document);
    add_interaction(document, FIXED_ID, TapTrigger(), PreviousArtboard());
    auto player = make_player(document, BOARD_ID);
    auto& stage = player->get_stage();
    auto layers = player->get_layers();
    REQUIRE(layers.size() == 1);
    auto& fixed = layers[0].m_fixed;
    REQUIRE(!fixed.is_nil());
    REQUIRE(stage.get(fixed).get_artboard()->m_frame ==
      Rect(0, 0, 100, 400));
    REQUIRE(stage.get_children(fixed) ==
      std::vector<NodeId>({FIXED_ID, NESTED_ID}));
    REQUIRE(stage.get(NESTED_ID).m_header.m_transform ==
      make_translation(30, 5));
    REQUIRE(stage.get_children(GROUP_ID).empty());
    REQUIRE(stage.get_children(BOARD_ID) ==
      std::vector<NodeId>({LOOSE_ID, GROUP_ID}));
    player->scroll(200);
    REQUIRE(player->find_interaction(Point(50, 90), TapTrigger()));
    REQUIRE(!player->find_interaction(Point(50, 10), TapTrigger()));
  }
}

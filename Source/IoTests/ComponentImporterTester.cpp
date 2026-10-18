#include <doctest/doctest.h>
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId id(const char* text) {
    return *parse_node_id(text);
  }

  const auto COMPONENT_ID = id("d631c702-7e61-4081-a819-f8a7b1f06234");
  const auto MASTER_ID = id("14d2fae5-9fa1-42b6-8318-615fabb5ff28");
  const auto STATE_ID = id("39ddf6a0-4f57-463a-b655-c8b5eba46177");
  const auto MASTER_SHAPE_ID = id("f18b6781-d8cc-429d-888f-bb4f3f9e0da7");
  const auto STATE_SHAPE_ID = id("1269c890-0c9e-4247-8f88-f6a6ab6a2d21");
  const auto INSTANCE_ID = id("345b49ae-25ab-4510-8001-2278ccf9c1bc");
  const auto INSTANCE_SHAPE_ID = id("c940ed89-d252-42cc-a4e5-b534fe0c9979");
  const auto INSTANCE_STATE_ID = id("6aa68cb5-302d-4233-a3a8-6dd498552b5a");
  const auto DEFAULT_ID = id("225db36e-d0a8-4044-abe7-706c74ffdaa9");
  const auto REMOVED_ID = id("1eed2b25-c194-40b8-95c6-ee825a833333");

  XdTestPackage make_package() {
    auto package = XdTestPackage();
    package.m_resources = parse_json(R"({"symbols": [{"type": "group",
      "name": "Button", "id": "14d2fae5-9fa1-42b6-8318-615fabb5ff28",
      "meta": {"ux": {"symbolId": "d631c702-7e61-4081-a819-f8a7b1f06234",
      "width": 143, "height": 26, "componentType": "componentNoBounds",
      "modTime": 1, "stateId": "d631c702-7e61-4081-a819-f8a7b1f06234",
      "linkedElementRef": "library", "resolvedRefName": "Layout",
      "linkedElementLastModified": 1688052553040,
      "states": [{"type": "group", "name": "Hover State",
      "id": "39ddf6a0-4f57-463a-b655-c8b5eba46177", "meta": {"ux": {
      "symbolId": "d631c702-7e61-4081-a819-f8a7b1f06234", "width": 143,
      "height": 26, "componentType": "componentNoBounds",
      "stateId": "39ddf6a0-4f57-463a-b655-c8b5eba46177", "syncMap": {
      "f18b6781-d8cc-429d-888f-bb4f3f9e0da7":
      "1269c890-0c9e-4247-8f88-f6a6ab6a2d21"}, "symbolOverrides": {
      "removedGuids": ["1eed2b25-c194-40b8-95c6-ee825a833333"]}}},
      "group": {"children": [{"type": "shape",
      "id": "1269c890-0c9e-4247-8f88-f6a6ab6a2d21", "meta": {"ux": {
      "symbolOverrides": {"propertyNames": ["fill"]}}}, "shape": {
      "type": "rect", "x": 0, "y": 0, "width": 143, "height": 26}}]}}]}},
      "group": {"children": [{"type": "shape",
      "id": "f18b6781-d8cc-429d-888f-bb4f3f9e0da7", "shape": {"type": "rect",
      "x": 0, "y": 0, "width": 143, "height": 26}}]}}]})");
    package.m_pasteboard = parse_json_array(R"([{"type": "group",
      "id": "345b49ae-25ab-4510-8001-2278ccf9c1bc", "meta": {"ux": {
      "symbolId": "d631c702-7e61-4081-a819-f8a7b1f06234", "width": 26,
      "height": 20, "componentType": "componentNoBounds",
      "stateId": "39ddf6a0-4f57-463a-b655-c8b5eba46177",
      "linkedElementRef": "library", "isCustomNameInherited": true,
      "syncMap": {"f18b6781-d8cc-429d-888f-bb4f3f9e0da7":
      "c940ed89-d252-42cc-a4e5-b534fe0c9979"}, "symbolOverrides": {
      "propertyNames": ["width"], "reorderedGuids": {
      "c940ed89-d252-42cc-a4e5-b534fe0c9979":
      "1eed2b25-c194-40b8-95c6-ee825a833333"}}, "states": [{"type": "group",
      "name": "Hover State", "id": "6aa68cb5-302d-4233-a3a8-6dd498552b5a",
      "meta": {"ux": {"symbolId": "d631c702-7e61-4081-a819-f8a7b1f06234",
      "stateId": "39ddf6a0-4f57-463a-b655-c8b5eba46177", "width": 26,
      "height": 20, "syncMap": {}}}}]}},
      "group": {"children": [{"type": "shape",
      "id": "c940ed89-d252-42cc-a4e5-b534fe0c9979", "meta": {"ux": {
      "symbolOverrides": {"propertyNames": ["fill", "stroke"]}}}, "shape": {
      "type": "rect", "x": 0, "y": 0, "width": 26, "height": 20}}]}},
      {"type": "group", "id": "225db36e-d0a8-4044-abe7-706c74ffdaa9",
      "meta": {"ux": {"symbolId": "d631c702-7e61-4081-a819-f8a7b1f06234",
      "stateId": "d631c702-7e61-4081-a819-f8a7b1f06234",
      "symbolOverrides": null}}},
      {"type": "group", "id": "9d8aedae-2c61-4929-b5c8-90430c38cbc8",
      "meta": {"ux": {"symbolOverrides": {"propertyNames": ["fill"]}}}}])");
    return package;
  }
}

TEST_SUITE("ComponentImporter") {
  TEST_CASE("component") {
    auto document = Document();
    auto report = import_package(make_package(), document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(report.m_warnings.empty());
    auto& components = document.get_resources().m_components;
    REQUIRE(components.size() == 1);
    auto& component = components.at(COMPONENT_ID);
    REQUIRE(component.m_name == "Button");
    REQUIRE(component.m_master == MASTER_ID);
    REQUIRE(component.m_default_state == COMPONENT_ID);
    REQUIRE(component.m_link ==
      ComponentLink("library", "Layout", 1688052553040));
    REQUIRE(!document.is_on_canvas(MASTER_ID));
    REQUIRE(document.get_children(MASTER_ID) ==
      std::vector<NodeId>({MASTER_SHAPE_ID}));
    REQUIRE(component.m_states.size() == 1);
    auto& state = component.m_states.front();
    REQUIRE(state.m_id == STATE_ID);
    REQUIRE(state.m_name == "Hover State");
    REQUIRE(state.m_tree == STATE_ID);
    REQUIRE(state.m_sync_map ==
      std::map<NodeId, NodeId>({{STATE_SHAPE_ID, MASTER_SHAPE_ID}}));
    REQUIRE(state.m_overrides.m_removed == std::vector<NodeId>({REMOVED_ID}));
    REQUIRE(state.m_overrides.m_properties ==
      std::map<NodeId, std::vector<std::string>>({{STATE_SHAPE_ID,
      {"fill"}}}));
    REQUIRE(document.get_children(STATE_ID) ==
      std::vector<NodeId>({STATE_SHAPE_ID}));
  }

  TEST_CASE("instance") {
    auto document = Document();
    import_package(make_package(), document);
    auto& instance =
      *document.get(INSTANCE_ID).get_group()->m_component_instance;
    REQUIRE(instance.m_component == COMPONENT_ID);
    REQUIRE(instance.m_state == STATE_ID);
    REQUIRE(instance.m_size == Size(26, 20));
    REQUIRE(instance.m_sync_map ==
      std::map<NodeId, NodeId>({{INSTANCE_SHAPE_ID, MASTER_SHAPE_ID}}));
    REQUIRE(instance.m_overrides.m_reordered ==
      std::map<NodeId, NodeId>({{INSTANCE_SHAPE_ID, REMOVED_ID}}));
    REQUIRE(instance.m_overrides.m_properties ==
      std::map<NodeId, std::vector<std::string>>({{INSTANCE_ID, {"width"}},
      {INSTANCE_SHAPE_ID, {"fill", "stroke"}}}));
    REQUIRE(instance.m_states.size() == 1);
    REQUIRE(instance.m_states.front().m_id == STATE_ID);
    REQUIRE(instance.m_states.front().m_tree == INSTANCE_STATE_ID);
    REQUIRE(!document.is_on_canvas(INSTANCE_STATE_ID));
    auto& other = *document.get(DEFAULT_ID).get_group()->m_component_instance;
    REQUIRE(other.m_state.is_nil());
    REQUIRE(other.m_overrides == Overrides());
  }
}

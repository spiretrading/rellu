#include <algorithm>
#include <doctest/doctest.h>
#include "Rellu/Io/XdValues.hpp"
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId id(const char* text) {
    return *parse_node_id(text);
  }

  const auto FIRST_ID = id("39933d05-e40a-4c79-a8ec-deda4884ac82");
  const auto SECOND_ID = id("1f8922ab-d204-4ea7-82ac-4c9e480f3170");
  const auto BUTTON_ID = id("9d8aedae-2c61-4929-b5c8-90430c38cbc8");
  const auto SOURCE_ID = id("a650ae17-29ed-4167-b3c4-1a7883857f22");
  const auto INSTANCE_ID = id("345b49ae-25ab-4510-8001-2278ccf9c1bc");
  const auto TAP_ID = id("e636e1d1-c8ff-4610-9405-f65119cdcf77");
  const auto TIME_ID = id("78e87816-0642-4bb2-99f3-6521fcb4f913");
  const auto HOVER_ID = id("9bfc7929-3147-422e-af80-edbd4a5c5db4");
  const auto STATE_ID = id("ad3e2c2c-8a99-4f12-ac9c-5e84181e1197");

  XdTestPackage make_package() {
    auto package = XdTestPackage();
    package.m_frames = parse_json(R"({
      "39933d05-e40a-4c79-a8ec-deda4884ac82": {"x": 0, "y": 0, "width": 10,
      "height": 10, "name": "First"},
      "1f8922ab-d204-4ea7-82ac-4c9e480f3170": {"x": 20, "y": 0, "width": 10,
      "height": 10, "name": "Second"}})");
    package.m_artboards.push_back(parse_json(R"({"type": "artboard",
      "id": "39933d05-e40a-4c79-a8ec-deda4884ac82", "meta": {"ux": {
      "isFlowRoot": true, "flowName": "Main", "interactions": [{
      "guid": "78e87816-0642-4bb2-99f3-6521fcb4f913", "inherited": false,
      "enabled": true, "valid": true}]}}, "artboard": {
      "ref": "39933d05-e40a-4c79-a8ec-deda4884ac82", "children": [{
      "type": "shape", "id": "9d8aedae-2c61-4929-b5c8-90430c38cbc8",
      "meta": {"ux": {"interactions": [{
      "guid": "e636e1d1-c8ff-4610-9405-f65119cdcf77", "inherited": false,
      "enabled": false, "valid": true, "data": {"interaction": {
      "properties": {"transition": "dissolve", "duration": 0.3,
      "easing": "ease-out",
      "destination": "1f8922ab-d204-4ea7-82ac-4c9e480f3170",
      "preserveScrollPosition": true, "voiceLocale": "en-us"},
      "triggerEvent": "tap", "action": "artboard-transition"},
      "version": "0.24"}}]}}, "shape": {"type": "rect", "x": 0, "y": 0,
      "width": 1, "height": 1}}]}})"));
    package.m_artboards.push_back(parse_json(R"({"type": "artboard",
      "id": "1f8922ab-d204-4ea7-82ac-4c9e480f3170", "artboard": {
      "ref": "1f8922ab-d204-4ea7-82ac-4c9e480f3170"}})"));
    package.m_pasteboard = parse_json_array(R"([{"type": "group",
      "id": "a650ae17-29ed-4167-b3c4-1a7883857f22"}, {"type": "group",
      "id": "345b49ae-25ab-4510-8001-2278ccf9c1bc", "meta": {"ux": {
      "interactions": [{"guid": "9bfc7929-3147-422e-af80-edbd4a5c5db4",
      "inherited": true, "enabled": false, "valid": true}]}}}])");
    package.m_interactions = parse_json(R"({"version": "0.24",
      "homeArtboard": "1f8922ab-d204-4ea7-82ac-4c9e480f3170",
      "interactions": {
      "9d8aedae-2c61-4929-b5c8-90430c38cbc8": [{"properties": {
      "transition": "dissolve", "duration": 0.3, "easing": "ease-out",
      "destination": "1f8922ab-d204-4ea7-82ac-4c9e480f3170",
      "preserveScrollPosition": true, "voiceLocale": "en-us"},
      "triggerEvent": "tap", "action": "artboard-transition",
      "id": "e636e1d1-c8ff-4610-9405-f65119cdcf77"}],
      "39933d05-e40a-4c79-a8ec-deda4884ac82": [{"properties": {"delay": 2,
      "destination": "1f8922ab-d204-4ea7-82ac-4c9e480f3170",
      "duration": 0.5, "easing": "linear", "transition": "none"},
      "triggerEvent": "time", "action": "auto-animate",
      "id": "78e87816-0642-4bb2-99f3-6521fcb4f913"}],
      "a650ae17-29ed-4167-b3c4-1a7883857f22": [{"properties": {
      "destination": "ad3e2c2c-8a99-4f12-ac9c-5e84181e1197",
      "transition": "auto-animate", "duration": 0.25, "easing": "snap"},
      "triggerEvent": "hover", "action": "state-transition",
      "id": "9bfc7929-3147-422e-af80-edbd4a5c5db4"},
      {"properties": {"destination": "39933d05-e40a-4c79-a8ec-deda4884ac82",
      "overlayPosition": {"x": 5, "y": 6}, "transition": "none",
      "duration": 0, "easing": "ease-in-out"}, "triggerEvent": "drag",
      "action": "overlay-transition",
      "id": "17e27c4b-0ecc-4f79-a58d-c5981cc0d688"},
      {"properties": {"keysGamepad": "Ctrl+K", "keysGamepadDevice": "keyboard",
      "keysGamepadPrintedBaseKey": "K", "transition": "none", "duration": 0,
      "easing": "ease-out", "destinationType": "previous-artboard"},
      "triggerEvent": "keys-gamepad", "action": "previous-artboard",
      "id": "f9dcc966-a8c0-4241-92bb-e2e0cb2a2866"},
      {"properties": {"subtype": "play"}, "triggerEvent": "time",
      "action": "video-playback",
      "id": "0dee9cd4-956a-4eef-ad45-237f36c96b7d"},
      {"properties": {}, "triggerEvent": "voice", "action": "speak",
      "id": "c11adcdf-bc79-437b-b291-d6c641c1b899"}],
      "b2391a3f-5075-43b6-8929-134c49e0b287": [{"properties": {},
      "triggerEvent": "tap", "action": "previous-artboard",
      "id": "48ea0b93-8afe-489c-9d70-4fdf14a064c1"}]}})");
    return package;
  }

  const Interaction& find(const Document& document, const NodeId& id) {
    auto& interactions = document.get_prototype().m_interactions;
    auto interaction = std::find_if(interactions.begin(), interactions.end(),
      [&] (const auto& interaction) { return interaction.m_id == id; });
    REQUIRE(interaction != interactions.end());
    return *interaction;
  }
}

TEST_SUITE("PrototypeImporter") {
  TEST_CASE("interactions") {
    auto document = Document();
    auto report = import_package(make_package(), document);
    REQUIRE(report.m_warnings.at("Unsupported interaction") == 1);
    REQUIRE(report.m_warnings.at("Interaction owner not found") == 1);
    REQUIRE(report.m_unconsumed == std::map<std::string, std::size_t,
      std::less<>>({{"interactions.interactions.<id>[].action:str", 1},
      {"interactions.interactions.<id>[].id:str", 1},
      {"interactions.interactions.<id>[].triggerEvent:str", 1}}));
    REQUIRE(document.get_prototype().m_interactions.size() == 7);
    auto& tap = find(document, TAP_ID);
    REQUIRE(tap.m_owner == BUTTON_ID);
    REQUIRE(std::holds_alternative<TapTrigger>(tap.m_trigger));
    REQUIRE(!tap.m_is_enabled);
    REQUIRE(tap.m_action == Action(ArtboardTransition(SECOND_ID,
      Animation(Transition::DISSOLVE, Easing::EASE_OUT, 0.3), true)));
    auto& time = find(document, TIME_ID);
    REQUIRE(time.m_owner == FIRST_ID);
    REQUIRE(time.m_trigger == Trigger(TimeTrigger(2)));
    REQUIRE(time.m_action == Action(ArtboardTransition(SECOND_ID,
      Animation(Transition::AUTO_ANIMATE, Easing::LINEAR, 0.5), false)));
    auto& hover = find(document, HOVER_ID);
    REQUIRE(hover.m_owner == SOURCE_ID);
    REQUIRE(hover.m_action == Action(StateTransition(STATE_ID,
      Animation(Transition::AUTO_ANIMATE, Easing::SNAP, 0.25))));
    auto& overlay = find(document, id("17e27c4b-0ecc-4f79-a58d-c5981cc0d688"));
    REQUIRE(std::holds_alternative<DragTrigger>(overlay.m_trigger));
    REQUIRE(overlay.m_action == Action(Overlay(FIRST_ID, Point(5, 6),
      Animation(Transition::NONE, Easing::EASE_IN_OUT, 0))));
    auto& keys = find(document, id("f9dcc966-a8c0-4241-92bb-e2e0cb2a2866"));
    REQUIRE(keys.m_trigger ==
      Trigger(KeyTrigger("Ctrl+K", KeyDevice::KEYBOARD)));
    REQUIRE(std::holds_alternative<PreviousArtboard>(keys.m_action));
    auto& video = find(document, id("0dee9cd4-956a-4eef-ad45-237f36c96b7d"));
    REQUIRE(video.m_action ==
      Action(VideoPlayback(SOURCE_ID, VideoCommand::PLAY)));
  }

  TEST_CASE("inherited") {
    auto document = Document();
    import_package(make_package(), document);
    auto copy_id = derive_node_id(u"9bfc7929-3147-422e-af80-edbd4a5c5db4",
      u"345b49ae-25ab-4510-8001-2278ccf9c1bc");
    auto& copy = find(document, copy_id);
    REQUIRE(copy.m_owner == INSTANCE_ID);
    REQUIRE(!copy.m_is_enabled);
    REQUIRE(copy.m_action == find(document, HOVER_ID).m_action);
  }

  TEST_CASE("flows") {
    auto document = Document();
    import_package(make_package(), document);
    auto& prototype = document.get_prototype();
    REQUIRE(prototype.m_home_artboard == SECOND_ID);
    REQUIRE(prototype.m_flows == std::vector<Flow>({Flow(FIRST_ID, "Main")}));
  }
}

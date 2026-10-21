#include <algorithm>
#include <cctype>
#include <memory>
#include <doctest/doctest.h>
#include <QJsonDocument>
#include "Rellu/Commands/History.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Stage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  std::vector<Interaction> get_sorted(const Document& document) {
    auto interactions = document.get_prototype().m_interactions;
    std::ranges::sort(interactions, [] (const auto& left, const auto& right) {
      return left.m_id < right.m_id;
    });
    return interactions;
  }

  void reload(const Document& document, Document& reloaded) {
    import_xd(XdPackage(write_xd(document)), reloaded);
  }
}

TEST_SUITE("RoundTrip") {
  TEST_CASE("interactions") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto history = History(document);
    auto add = [&] (Interaction interaction) {
      auto command = std::make_unique<AddInteractionCommand>(
        std::move(interaction));
      auto id = command->get_interaction().m_id;
      REQUIRE(history.execute(std::move(command)));
      return id;
    };
    add(make_default_interaction(scene.m_button, scene.m_second));
    add(Interaction(NodeId(), scene.m_box, KeyTrigger("Ctrl+Shift+K"),
      ArtboardTransition(scene.m_long, Animation(
        Transition::PUSH_UP, Easing::EASE_IN_OUT, 0.6), true)));
    add(Interaction(NodeId(), scene.m_home, TimeTrigger(1.5),
      ArtboardTransition(scene.m_second, Animation(
        Transition::AUTO_ANIMATE, Easing::SNAP, 0.8))));
    add(Interaction(NodeId(), scene.m_second_box, DragTrigger(),
      Overlay(scene.m_menu, Point(20, 30), Animation(
        Transition::SLIDE_DOWN, Easing::LINEAR, 0.2))));
    add(Interaction(NodeId(), scene.m_back, TapTrigger(),
      PreviousArtboard()));
    add(Interaction(NodeId(), scene.m_instance, HoverTrigger(),
      StateTransition(scene.m_hover, Animation(
        Transition::AUTO_ANIMATE, Easing::WIND_UP, 0.4))));
    add(Interaction(NodeId(), scene.m_header,
      KeyTrigger("B", KeyDevice::GAMEPAD), ArtboardTransition(scene.m_home,
        Animation(Transition::NONE, Easing::BOUNCE, 1))));
    add(Interaction(NodeId(), scene.m_footer, TapTrigger(),
      VideoPlayback(scene.m_footer, VideoCommand::PAUSE)));
    auto disabled = make_default_interaction(scene.m_menu, scene.m_home);
    disabled.m_is_enabled = false;
    add(disabled);
    REQUIRE(history.execute(
      std::make_unique<AddFlowCommand>(scene.m_home, std::string())));
    REQUIRE(history.execute(
      std::make_unique<AddFlowCommand>(scene.m_second, "Checkout")));
    REQUIRE(history.execute(
      std::make_unique<SetHomeArtboardCommand>(scene.m_home)));
    auto edited = get_interactions(document, scene.m_button).front();
    auto changed = *edited;
    changed.m_trigger = TimeTrigger(3);
    REQUIRE(history.execute(
      std::make_unique<SetInteractionCommand>(changed)));
    auto reloaded = Document();
    reload(document, reloaded);
    REQUIRE(get_sorted(reloaded) == get_sorted(document));
    REQUIRE(reloaded.get_prototype().m_flows ==
      document.get_prototype().m_flows);
    REQUIRE(reloaded.get_prototype().m_home_artboard == scene.m_home);
    auto again = Document();
    reload(reloaded, again);
    REQUIRE(find_differences(reloaded, again).empty());
  }

  TEST_CASE("scroll_to") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_header, TapTrigger(),
      ScrollTransition(scene.m_footer, -40, Animation(
        Transition::DISSOLVE, Easing::EASE_IN_OUT, DEFAULT_DURATION)));
    add_interaction(document, scene.m_footer, TapTrigger(),
      ScrollTransition(NodeId(), 0, make_empty_animation()));
    auto data = write_xd(document);
    auto raw = XdPackage(data).read(XdPackage::INTERACTIONS);
    auto json = QByteArray();
    for(auto c : raw) {
      if(!std::isspace(static_cast<unsigned char>(c))) {
        json.append(c);
      }
    }
    auto header = json.indexOf(
      QByteArray::fromStdString(to_string(scene.m_header)));
    REQUIRE(header >= 0);
    auto find = [&] (const char* key, qsizetype from) {
      auto index = json.indexOf(key, from);
      REQUIRE(index > from);
      return index;
    };
    auto properties = find("\"properties\"", header);
    auto transition = find("\"transition\":\"dissolve\"", properties);
    auto duration = find("\"duration\"", transition);
    auto easing = find("\"easing\":\"ease-in-out\"", duration);
    auto destination = find("\"destination\"", easing);
    auto offset = find("\"yOffset\":-40", destination);
    auto locale = find("\"voiceLocale\":\"en-us\"", offset);
    auto trigger = find("\"triggerEvent\":\"tap\"", locale);
    find("\"action\":\"scroll-transition\"", trigger);
    auto footer = json.indexOf(
      QByteArray::fromStdString(to_string(scene.m_footer)), header);
    auto footer_properties = find("\"properties\"", footer);
    auto end = find("\"triggerEvent\"", footer_properties);
    auto body = json.mid(footer_properties, end - footer_properties);
    REQUIRE(!body.contains("destination"));
    REQUIRE(!body.contains("yOffset"));
    auto reloaded = Document();
    import_xd(XdPackage(data), reloaded);
    REQUIRE(get_sorted(reloaded) == get_sorted(document));
    REQUIRE(QJsonDocument::fromJson(XdPackage(write_xd(reloaded)).read(
      XdPackage::INTERACTIONS)) == QJsonDocument::fromJson(raw));
  }

  TEST_CASE("removal") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto id = add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_default_animation()));
    auto reloaded = Document();
    reload(document, reloaded);
    REQUIRE(reloaded.get_prototype().m_interactions.size() == 1);
    auto history = History(reloaded);
    REQUIRE(history.execute(std::make_unique<RemoveInteractionCommand>(id)));
    REQUIRE(history.execute(
      std::make_unique<RemoveFlowCommand>(scene.m_home)) == nullptr);
    auto again = Document();
    reload(reloaded, again);
    REQUIRE(again.get_prototype().m_interactions.empty());
  }
}

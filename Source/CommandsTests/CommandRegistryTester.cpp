#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("CommandRegistry") {
  TEST_CASE("names") {
    auto registry = CommandRegistry();
    add_standard_commands(registry);
    REQUIRE(registry.get_names() == std::vector<std::string>{
      "arrange.bring_forward", "arrange.bring_to_front",
      "arrange.send_backward", "arrange.send_to_back", "edit.delete",
      "edit.duplicate", "object.group", "object.nudge_down",
      "object.nudge_down_large", "object.nudge_left",
      "object.nudge_left_large", "object.nudge_right",
      "object.nudge_right_large", "object.nudge_up", "object.nudge_up_large",
      "object.toggle_lock", "object.toggle_visibility", "object.ungroup"});
    REQUIRE(registry.contains("object.group"));
    REQUIRE(!registry.contains("object.unknown"));
    REQUIRE_THROWS_AS(registry.add("object.group", CommandFactory()),
      std::invalid_argument);
  }

  TEST_CASE("make") {
    auto document = Document();
    auto scene = build_scene(document);
    auto registry = CommandRegistry();
    add_standard_commands(registry);
    auto none = std::vector<NodeId>();
    REQUIRE(!registry.make("edit.delete", document, none));
    REQUIRE(!registry.make("object.group", document, {scene.m_artboard}));
    REQUIRE(!registry.make("object.ungroup", document, {scene.m_rectangle}));
    REQUIRE(!registry.make("edit.delete", document, {scene.m_master}));
    REQUIRE(registry.make("object.group", document,
      {scene.m_rectangle})->get_label() == "Group");
    REQUIRE(registry.make("object.ungroup", document,
      {scene.m_group, scene.m_rectangle})->get_label() == "Ungroup");
    REQUIRE(registry.make("edit.duplicate", document,
      {scene.m_rectangle})->get_label() == "Duplicate");
    REQUIRE(registry.make("arrange.send_to_back", document,
      {scene.m_rectangle})->get_label() == "Send to Back");
    REQUIRE_THROWS_AS(registry.make("object.unknown", document, none),
      std::out_of_range);
    auto nudge = registry.make(
      "object.nudge_left_large", document, {scene.m_rectangle});
    REQUIRE(nudge->get_label() == "Nudge");
    REQUIRE(static_cast<const TranslateCommand&>(*nudge).get_delta() ==
      Point(-10, 0));
  }

  TEST_CASE("toggles") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto registry = CommandRegistry();
    add_standard_commands(registry);
    auto nodes = std::vector{scene.m_rectangle, scene.m_loose};
    history.execute(
      std::make_unique<SetLockCommand>(std::vector{scene.m_loose}, true));
    auto lock = registry.make("object.toggle_lock", document, nodes);
    REQUIRE(lock->get_label() == "Lock");
    history.execute(std::move(lock));
    REQUIRE(document.get(scene.m_rectangle).m_header.m_is_locked);
    REQUIRE(registry.make("object.toggle_lock", document, nodes)->
      get_label() == "Unlock");
    REQUIRE(registry.make("object.toggle_visibility", document, nodes)->
      get_label() == "Hide");
    history.execute(
      registry.make("object.toggle_visibility", document, nodes));
    REQUIRE(registry.make("object.toggle_visibility", document, nodes)->
      get_label() == "Show");
  }

  TEST_CASE("unhideable") {
    auto document = Document();
    auto scene = build_scene(document);
    auto operand = NodeId();
    {
      auto transaction = Transaction(document);
      auto compound = Shape();
      compound.m_geometry = CompoundShape(CompoundOperation::UNION,
        Shared(make_rectangle(Rect(0, 0, 5, 5))));
      auto id = transaction.append(
        scene.m_artboard, make_header("Compound"), compound);
      operand = transaction.append(
        id, make_header("Operand"), make_rectangle_shape(5, 5));
      transaction.commit();
    }
    auto history = History(document);
    auto registry = CommandRegistry();
    add_standard_commands(registry);
    REQUIRE(!registry.make("object.toggle_visibility", document, {operand}));
    REQUIRE(!registry.make(
      "object.toggle_visibility", document, {scene.m_artboard}));
    history.execute(registry.make("object.toggle_visibility", document,
      {operand, scene.m_artboard, scene.m_rectangle}));
    REQUIRE(!document.get(scene.m_rectangle).m_header.m_is_visible);
    REQUIRE(document.get(operand).m_header.m_is_visible);
    REQUIRE(document.get(scene.m_artboard).m_header.m_is_visible);
  }

  TEST_CASE("custom") {
    auto document = Document();
    auto scene = build_scene(document);
    auto registry = CommandRegistry();
    registry.add("object.rename", [] (const auto&, const auto& targets) ->
        std::unique_ptr<Command> {
      if(targets.size() != 1) {
        return nullptr;
      }
      return std::make_unique<RenameCommand>(targets.front(), "Renamed");
    });
    REQUIRE(!registry.make("object.rename", document, {}));
    REQUIRE(registry.make(
      "object.rename", document, {scene.m_loose})->get_label() == "Rename");
  }
}

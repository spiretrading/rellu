#include <doctest/doctest.h>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("CompositeCommand") {
  TEST_CASE("steps") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle, scene.m_mask};
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(
      std::make_unique<TranslateCommand>(nodes, Point(10, 0)));
    commands.push_back(std::make_unique<GroupCommand>(nodes, "Card"));
    commands.push_back(std::make_unique<SetOpacityCommand>(nodes, 0.5));
    auto command = require_undoable(history,
      std::make_unique<CompositeCommand>("Make Card", std::move(commands)));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Make Card");
    auto& composite = static_cast<const CompositeCommand&>(*command);
    auto& group = static_cast<const GroupCommand&>(
      *composite.get_commands()[1]).get_group();
    REQUIRE(command->get_selection() == std::vector{group});
    REQUIRE(document.get(group).m_header.m_name == "Card");
    REQUIRE(document.get_children(group) == nodes);
    REQUIRE(document.get(scene.m_rectangle).m_header.m_opacity == 0.5);
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(120, 60));
  }

  TEST_CASE("empty") {
    auto document = Document();
    build_scene(document);
    auto history = History(document);
    REQUIRE(!history.execute(std::make_unique<CompositeCommand>(
      "Nothing", std::vector<std::unique_ptr<Command>>())));
    REQUIRE(!history.can_undo());
  }
}

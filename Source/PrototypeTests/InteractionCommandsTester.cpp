#include <memory>
#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_artboard(Document& document, const char* name) {
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(600, 0, 100, 100);
    auto id = transaction.append(NodeId(), make_header(name), artboard);
    transaction.commit();
    document.clear_history();
    return id;
  }
}

TEST_SUITE("InteractionCommands") {
  TEST_CASE("default_interaction") {
    auto owner = generate_node_id();
    auto destination = generate_node_id();
    auto interaction = make_default_interaction(owner, destination);
    REQUIRE(interaction.m_id.is_nil());
    REQUIRE(interaction.m_owner == owner);
    REQUIRE(std::holds_alternative<TapTrigger>(interaction.m_trigger));
    REQUIRE(interaction.m_is_enabled);
    REQUIRE(interaction.m_action == Action(ArtboardTransition(destination,
      Animation(Transition::DISSOLVE, Easing::EASE_OUT, 0.3))));
  }

  TEST_CASE("add") {
    auto document = Document();
    auto scene = build_scene(document);
    auto second = add_artboard(document, "Second");
    auto history = History(document);
    auto command = std::make_unique<AddInteractionCommand>(
      make_default_interaction(scene.m_ellipse, second));
    auto id = command->get_interaction().m_id;
    REQUIRE(!id.is_nil());
    require_undoable(history, std::move(command));
    auto interactions = get_interactions(document, scene.m_ellipse);
    REQUIRE(interactions.size() == 1);
    REQUIRE(interactions.front()->m_id == id);
    REQUIRE(get_interactions(document, scene.m_rectangle).size() == 1);
  }

  TEST_CASE("add_without_artboard") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    REQUIRE_THROWS_AS(history.execute(std::make_unique<AddInteractionCommand>(
      make_default_interaction(scene.m_ellipse, scene.m_rectangle))),
      std::invalid_argument);
    REQUIRE(history.get_undo_count() == 0);
  }

  TEST_CASE("multiple") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto hover = Interaction(NodeId(), scene.m_rectangle, HoverTrigger(),
      StateTransition(generate_node_id(),
        Animation(Transition::AUTO_ANIMATE, Easing::SNAP, 0.2)));
    require_undoable(
      history, std::make_unique<AddInteractionCommand>(hover));
    auto interactions = get_interactions(document, scene.m_rectangle);
    REQUIRE(interactions.size() == 2);
    REQUIRE(interactions[0]->m_id == scene.m_interaction);
    REQUIRE(std::holds_alternative<HoverTrigger>(interactions[1]->m_trigger));
  }

  TEST_CASE("set") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto interaction = *get_interactions(document, scene.m_rectangle).front();
    interaction.m_trigger = TimeTrigger(2);
    auto& transition = std::get<ArtboardTransition>(interaction.m_action);
    transition.m_animation =
      Animation(Transition::SLIDE_LEFT, Easing::EASE_IN_OUT, 0.6);
    require_undoable(
      history, std::make_unique<SetInteractionCommand>(interaction));
    REQUIRE(*get_interactions(document, scene.m_rectangle).front() ==
      interaction);
    REQUIRE(!history.execute(
      std::make_unique<SetInteractionCommand>(interaction)));
    interaction.m_id = generate_node_id();
    REQUIRE_THROWS_AS(history.execute(
      std::make_unique<SetInteractionCommand>(interaction)),
      std::invalid_argument);
  }

  TEST_CASE("remove") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history,
      std::make_unique<RemoveInteractionCommand>(scene.m_interaction));
    REQUIRE(document.get_prototype().m_interactions.empty());
    REQUIRE_THROWS_AS(history.execute(
      std::make_unique<RemoveInteractionCommand>(scene.m_interaction)),
      std::invalid_argument);
  }

  TEST_CASE("flow_name") {
    auto prototype = Prototype();
    REQUIRE(make_flow_name(prototype) == "Flow 1");
    prototype.m_flows.push_back(Flow(generate_node_id(), "Flow 1"));
    prototype.m_flows.push_back(Flow(generate_node_id(), "Flow 3"));
    prototype.m_flows.push_back(Flow(generate_node_id(), "Flow 2x"));
    REQUIRE(make_flow_name(prototype) == "Flow 2");
  }

  TEST_CASE("flows") {
    auto document = Document();
    auto scene = build_scene(document);
    auto second = add_artboard(document, "Second");
    auto history = History(document);
    require_undoable(history,
      std::make_unique<AddFlowCommand>(scene.m_artboard, std::string()));
    require_undoable(
      history, std::make_unique<AddFlowCommand>(second, "Checkout"));
    REQUIRE(document.get_prototype().m_flows == std::vector<Flow>(
      {Flow(scene.m_artboard, "Flow 1"), Flow(second, "Checkout")}));
    REQUIRE(find_flow(document, second)->m_name == "Checkout");
    REQUIRE(!history.execute(
      std::make_unique<AddFlowCommand>(second, "Other")));
    require_undoable(
      history, std::make_unique<RenameFlowCommand>(second, "Pay"));
    REQUIRE(find_flow(document, second)->m_name == "Pay");
    require_undoable(
      history, std::make_unique<RemoveFlowCommand>(scene.m_artboard));
    REQUIRE(!find_flow(document, scene.m_artboard));
    REQUIRE_THROWS_AS(history.execute(std::make_unique<RenameFlowCommand>(
      scene.m_artboard, "Gone")), std::invalid_argument);
    REQUIRE_THROWS_AS(history.execute(std::make_unique<AddFlowCommand>(
      scene.m_rectangle, "Shape")), std::invalid_argument);
  }

  TEST_CASE("home_artboard") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history,
      std::make_unique<SetHomeArtboardCommand>(scene.m_artboard));
    REQUIRE(document.get_prototype().m_home_artboard == scene.m_artboard);
    require_undoable(history, std::make_unique<AddFlowCommand>(
      scene.m_artboard, std::string()));
    require_undoable(history,
      std::make_unique<RemoveFlowCommand>(scene.m_artboard));
    REQUIRE(document.get_prototype().m_home_artboard.is_nil());
  }

  TEST_CASE("wire_with_flow") {
    auto document = Document();
    auto scene = build_scene(document);
    auto second = add_artboard(document, "Second");
    auto history = History(document);
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(std::make_unique<AddInteractionCommand>(
      make_default_interaction(scene.m_ellipse, second)));
    commands.push_back(
      std::make_unique<AddFlowCommand>(scene.m_artboard, std::string()));
    require_undoable(history, std::make_unique<CompositeCommand>(
      "Add Interaction", std::move(commands)));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get_prototype().m_flows.size() == 1);
    REQUIRE(get_interactions(document, scene.m_ellipse).size() == 1);
  }

  TEST_CASE("empty_interaction") {
    auto owner = generate_node_id();
    auto interaction = make_empty_interaction(owner, false);
    REQUIRE(interaction.m_owner == owner);
    REQUIRE(std::holds_alternative<TapTrigger>(interaction.m_trigger));
    REQUIRE(interaction.m_action == Action(ArtboardTransition(NodeId(),
      Animation(Transition::NONE, Easing::EASE_IN, 0.2))));
    REQUIRE(make_empty_interaction(owner, true).m_trigger ==
      Trigger(TimeTrigger(DEFAULT_DELAY)));
  }

  TEST_CASE("inherited_id") {
    auto source = generate_node_id();
    auto owner = generate_node_id();
    REQUIRE(derive_inherited_id(source, owner) == derive_node_id(
      QString::fromStdString(to_string(source)),
      QString::fromStdString(to_string(owner))));
  }

  TEST_CASE("main_component_interactions") {
    auto document = Document();
    auto scene = build_scene(document);
    auto second = add_artboard(document, "Second");
    auto history = History(document);
    auto command = history.execute(
      std::make_unique<MakeComponentCommand>(std::vector{scene.m_group}));
    auto& made = static_cast<const MakeComponentCommand&>(*command);
    auto main = made.get_main();
    auto master =
      document.get_resources().m_components.at(made.get_component()).m_master;
    history.execute(std::make_unique<AddInteractionCommand>(
      make_default_interaction(main, second)));
    auto sources = get_interactions(document, master);
    REQUIRE(sources.size() == 1);
    auto source = sources.front()->m_id;
    auto copies = get_interactions(document, main);
    REQUIRE(copies.size() == 1);
    REQUIRE(copies.front()->m_id == derive_inherited_id(source, main));
    auto changed = *copies.front();
    auto instance = static_cast<const InsertInstanceCommand&>(
      *history.execute(std::make_unique<InsertInstanceCommand>(
        made.get_component(), scene.m_artboard, make_translation(0, 0)))).
      get_instance();
    REQUIRE(get_interactions(document, instance).size() == 1);
    changed.m_trigger = DragTrigger();
    history.execute(std::make_unique<SetInteractionCommand>(changed));
    REQUIRE(get_interactions(document, master).front()->m_trigger ==
      Trigger(DragTrigger()));
    REQUIRE(get_interactions(document, instance).front()->m_trigger ==
      Trigger(DragTrigger()));
    history.execute(std::make_unique<RemoveInteractionCommand>(
      derive_inherited_id(source, main)));
    REQUIRE(get_interactions(document, master).empty());
    REQUIRE(get_interactions(document, instance).empty());
  }
}

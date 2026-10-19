#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StackCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Parts {
    NodeId m_artboard;
    NodeId m_group;
    NodeId m_rectangle;
    NodeId m_label;
  };

  Parts build_parts(Document& document) {
    auto parts = Parts();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 300);
    parts.m_artboard =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    parts.m_group = transaction.append(parts.m_artboard,
      make_header("Button", make_translation(20, 20)), Group());
    parts.m_rectangle = transaction.append(parts.m_group,
      make_header("Background"), make_rectangle_shape(80, 30));
    parts.m_label = transaction.append(parts.m_group,
      make_header("Label", make_translation(10, 5)), make_text(u"OK"));
    transaction.commit();
    document.clear_history();
    return parts;
  }

  const ComponentInstance& get_role(const Document& document,
      const NodeId& id) {
    return *get_instance_role(document.get(id));
  }

  ComponentId make_component(History& history, NodeId& group) {
    auto command = history.execute(
      std::make_unique<MakeComponentCommand>(std::vector{group}));
    auto& made = static_cast<const MakeComponentCommand&>(*command);
    group = made.get_main();
    return made.get_component();
  }

  NodeId add_instance(History& history, const ComponentId& component,
      const NodeId& parent, double x, double y) {
    auto command = history.execute(std::make_unique<InsertInstanceCommand>(
      component, parent, make_translation(x, y)));
    return static_cast<const InsertInstanceCommand*>(command)->get_instance();
  }

  NodeId find_copy(const Document& document, const NodeId& instance,
      const NodeId& source) {
    for(auto& entry : get_role(document, instance).m_sync_map) {
      if(entry.second == source) {
        return entry.first;
      }
    }
    return NodeId();
  }

  NodeId get_master_node(const Document& document, const NodeId& main,
      const NodeId& id) {
    return get_role(document, main).m_sync_map.at(id);
  }

  const Node& resolve_node(const Document& document, const NodeId& id) {
    if(auto resolution = resolve_reference(document.get_nodes(), id)) {
      return document.get(resolution->m_source);
    }
    return document.get(id);
  }

  Color get_fill(const Document& document, const NodeId& id) {
    auto& fill = resolve_node(document, id).get_shape()->m_style.m_fill;
    return std::get<SolidFill>(fill).m_color;
  }

  const Command* edit(History& history, const NodeId& id,
      std::unique_ptr<Command> command) {
    return history.execute(make_instance_edit(
      history.get_document(), std::vector{id}, std::move(command)));
  }

  std::unique_ptr<Command> make_fill(const NodeId& id, const Color& color) {
    return std::make_unique<SetFillCommand>(
      std::vector{id}, Fill(SolidFill(color)));
  }

  std::vector<std::string> get_overridden(const Document& document,
      const NodeId& instance, const NodeId& id) {
    auto& properties = get_role(document, instance).m_overrides.m_properties;
    if(auto entry = properties.find(id); entry != properties.end()) {
      return entry->second;
    }
    return std::vector<std::string>();
  }
}

TEST_SUITE("ComponentCommands") {
  TEST_CASE("make_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto command = require_undoable(history,
      std::make_unique<MakeComponentCommand>(std::vector{parts.m_group}));
    REQUIRE(history.get_undo_label() == "Make Component");
    auto& made = static_cast<const MakeComponentCommand&>(*command);
    REQUIRE(made.get_main() != parts.m_group);
    REQUIRE(!document.find(parts.m_group));
    REQUIRE(made.get_selection() == std::vector{made.get_main()});
    auto& main = document.get(made.get_main());
    REQUIRE(main.m_parent == parts.m_artboard);
    REQUIRE(main.m_header.m_name == "Button");
    REQUIRE(main.m_header.m_transform == make_translation(20, 20));
    REQUIRE(main.m_children == std::vector{parts.m_rectangle, parts.m_label});
    auto& role = get_role(document, made.get_main());
    REQUIRE(role.m_is_main);
    REQUIRE(role.m_component == made.get_component());
    REQUIRE(role.m_sync_map.size() == 2);
    auto& component =
      document.get_resources().m_components.at(made.get_component());
    REQUIRE(component.m_name == "Button");
    auto& master = document.get(component.m_master);
    REQUIRE(master.m_parent.is_nil());
    REQUIRE(!document.is_on_canvas(component.m_master));
    REQUIRE(master.m_children.size() == 2);
    REQUIRE(master.m_children.front() == role.m_sync_map.at(parts.m_rectangle));
    REQUIRE(get_fill(document, master.m_children.front()) ==
      make_color(255, 0, 0));
    REQUIRE(component.m_size == role.m_size);
    REQUIRE(component.m_size.m_width > 0);
    REQUIRE(find_main_component(document, made.get_component()) ==
      made.get_main());
  }

  TEST_CASE("make_component_from_nodes") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto command = history.execute(std::make_unique<MakeComponentCommand>(
      std::vector{parts.m_rectangle, parts.m_label}));
    auto& made = static_cast<const MakeComponentCommand&>(*command);
    auto& main = document.get(made.get_main());
    REQUIRE(main.m_header.m_name == "Component 1 \xE2\x80\x93 1");
    REQUIRE(document.get_resources().m_components.at(made.get_component()).
      m_name == "Component 1");
    REQUIRE(main.m_parent == parts.m_group);
    REQUIRE(main.m_children == std::vector{parts.m_rectangle, parts.m_label});
    REQUIRE(get_role(document, made.get_main()).m_is_main);
    REQUIRE_THROWS_AS(history.execute(std::make_unique<MakeComponentCommand>(
      std::vector{parts.m_artboard})), std::invalid_argument);
  }

  TEST_CASE("insert_instance") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto& master = document.get_resources().m_components.at(component).
      m_master;
    auto command = require_undoable(history,
      std::make_unique<InsertInstanceCommand>(
        component, parts.m_artboard, make_translation(200, 100)));
    REQUIRE(history.get_undo_label() == "Add Component");
    auto instance =
      static_cast<const InsertInstanceCommand&>(*command).get_instance();
    auto& node = document.get(instance);
    REQUIRE(node.m_header.m_name == "Button");
    REQUIRE(node.m_header.m_transform == make_translation(200, 100));
    REQUIRE(node.m_children.size() == 2);
    auto& role = get_role(document, instance);
    REQUIRE(!role.m_is_main);
    REQUIRE(role.m_size.m_width > 0);
    for(auto i = std::size_t(0); i != 2; ++i) {
      auto& child = document.get(node.m_children[i]);
      REQUIRE(child.get_reference());
      REQUIRE(child.get_reference()->m_source ==
        document.get(master).m_children[i]);
      REQUIRE(role.m_sync_map.at(node.m_children[i]) ==
        document.get(master).m_children[i]);
    }
  }

  TEST_CASE("main_propagation") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto plain = add_instance(history, component, parts.m_artboard, 200, 20);
    auto edited = add_instance(history, component, parts.m_artboard, 200, 100);
    auto master_rectangle =
      get_master_node(document, parts.m_group, parts.m_rectangle);
    auto copy = find_copy(document, edited, master_rectangle);
    edit(history, copy, std::make_unique<SetOpacityCommand>(
      std::vector{copy}, 0.5));
    REQUIRE(!document.get(copy).get_reference());
    REQUIRE(get_overridden(document, edited, copy) ==
      std::vector<std::string>{"opacity"});
    require_undoable(
      history, make_fill(parts.m_rectangle, make_color(0, 0, 255)));
    REQUIRE(get_fill(document, master_rectangle) == make_color(0, 0, 255));
    REQUIRE(get_fill(document, find_copy(document, plain, master_rectangle)) ==
      make_color(0, 0, 255));
    REQUIRE(get_fill(document, copy) == make_color(0, 0, 255));
    require_undoable(history, std::make_unique<SetOpacityCommand>(
      std::vector{parts.m_rectangle}, 0.25));
    REQUIRE(document.get(master_rectangle).m_header.m_opacity == 0.25);
    REQUIRE(document.get(copy).m_header.m_opacity == 0.5);
    REQUIRE(get_overridden(document, edited, copy) ==
      std::vector<std::string>{"opacity"});
  }

  TEST_CASE("instance_override") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto plain = add_instance(history, component, parts.m_artboard, 200, 20);
    auto edited = add_instance(history, component, parts.m_artboard, 200, 100);
    auto master_rectangle =
      get_master_node(document, parts.m_group, parts.m_rectangle);
    auto copy = find_copy(document, edited, master_rectangle);
    auto& before = document.get(copy);
    REQUIRE(before.get_reference());
    edit(history, copy, make_fill(copy, make_color(0, 255, 0)));
    REQUIRE(history.get_undo_label() == "Change Fill");
    REQUIRE(get_overridden(document, edited, copy) ==
      std::vector<std::string>{"fill"});
    REQUIRE(get_fill(document, copy) == make_color(0, 255, 0));
    REQUIRE(get_fill(document, master_rectangle) == make_color(255, 0, 0));
    REQUIRE(get_fill(document, parts.m_rectangle) == make_color(255, 0, 0));
    REQUIRE(get_fill(document, find_copy(document, plain, master_rectangle)) ==
      make_color(255, 0, 0));
    history.execute(make_fill(parts.m_rectangle, make_color(0, 0, 255)));
    REQUIRE(get_fill(document, copy) == make_color(0, 255, 0));
    history.execute(std::make_unique<SetStrokeCommand>(
      std::vector{parts.m_rectangle}, StrokeEdit(make_color(1, 2, 3), 6.0)));
    REQUIRE(resolve_node(document, copy).get_shape()->m_style.m_stroke->
      m_width == 6);
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(document.get(copy).get_reference());
    REQUIRE(get_role(document, edited).m_overrides == Overrides());
  }

  TEST_CASE("text_override") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    auto master_label =
      get_master_node(document, parts.m_group, parts.m_label);
    auto copy = find_copy(document, instance, master_label);
    auto text = make_text(u"Cancel");
    edit(history, copy, std::make_unique<EditTextCommand>(copy, text));
    auto overridden = get_overridden(document, instance, copy);
    REQUIRE(std::find(overridden.begin(), overridden.end(), "text") !=
      overridden.end());
    history.execute(make_fill(parts.m_label, make_color(0, 0, 255)));
    auto& label = *document.get(copy).get_text();
    REQUIRE(*label.m_content == u"Cancel");
    REQUIRE(label.m_runs.front().m_style.m_color == make_color(0, 0, 255));
    REQUIRE(*resolve_node(document, master_label).get_text()->m_content ==
      u"OK");
  }

  TEST_CASE("main_structure") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    auto& master = document.get_resources().m_components.at(component).
      m_master;
    auto command = require_undoable(history,
      std::make_unique<InsertNodeCommand>("Rectangle", parts.m_group,
        make_header("Dot"), make_rectangle_shape(4, 4)));
    auto dot = static_cast<const InsertNodeCommand&>(*command).get_node();
    REQUIRE(document.get(master).m_children.size() == 3);
    auto master_dot = get_master_node(document, parts.m_group, dot);
    REQUIRE(document.get(master).m_children.back() == master_dot);
    auto& children = document.get(instance).m_children;
    REQUIRE(children.size() == 3);
    REQUIRE(document.get(children.back()).get_reference()->m_source ==
      master_dot);
    require_undoable(
      history, std::make_unique<DeleteCommand>(std::vector{dot}));
    REQUIRE(document.get(master).m_children.size() == 2);
    REQUIRE(!document.find(master_dot));
    REQUIRE(document.get(instance).m_children.size() == 2);
  }

  TEST_CASE("instance_structure") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    auto master_rectangle =
      get_master_node(document, parts.m_group, parts.m_rectangle);
    auto copy = find_copy(document, instance, master_rectangle);
    history.execute(std::make_unique<DeleteCommand>(std::vector{copy}));
    auto& overrides = get_role(document, instance).m_overrides;
    REQUIRE(overrides.m_removed == std::vector{master_rectangle});
    auto command = history.execute(std::make_unique<InsertNodeCommand>(
      "Rectangle", instance, make_header("Extra"),
      make_rectangle_shape(4, 4)));
    auto extra = static_cast<const InsertNodeCommand&>(*command).get_node();
    REQUIRE(get_role(document, instance).m_overrides.m_added ==
      std::vector{extra});
    history.execute(std::make_unique<InsertNodeCommand>("Rectangle",
      parts.m_group, make_header("Dot"), make_rectangle_shape(4, 4)));
    auto& children = document.get(instance).m_children;
    REQUIRE(children.size() == 3);
    REQUIRE(std::find(children.begin(), children.end(), extra) !=
      children.end());
    history.execute(make_fill(parts.m_rectangle, make_color(0, 0, 255)));
    REQUIRE(find_copy(document, instance, master_rectangle).is_nil());
  }

  TEST_CASE("ungroup_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto commands = CommandRegistry();
    add_standard_commands(commands);
    add_component_commands(commands);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    auto other = add_instance(history, component, parts.m_artboard, 200, 100);
    REQUIRE(commands.make("object.ungroup", document,
      std::vector{instance, other})->get_label() == "Ungroup Components");
    auto command = commands.make("object.ungroup", document,
      std::vector{instance});
    REQUIRE(command);
    REQUIRE(command->get_label() == "Ungroup Component");
    auto children = document.get(instance).m_children;
    auto recorded = require_undoable(history, std::move(command));
    REQUIRE(recorded->get_selection() == children);
    REQUIRE(!document.find(instance));
    for(auto& child : children) {
      auto& node = document.get(child);
      REQUIRE(node.m_parent == parts.m_artboard);
      REQUIRE(!node.get_reference());
    }
    REQUIRE(get_fill(document, children.front()) == make_color(255, 0, 0));
    REQUIRE(document.get(children.front()).m_header.m_transform ==
      make_translation(200, 20));
    REQUIRE(document.get(children.back()).m_header.m_transform ==
      make_translation(210, 25));
    REQUIRE(!commands.make("object.ungroup_component", document, children));
  }

  TEST_CASE("reset_overrides") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto commands = CommandRegistry();
    add_component_commands(commands);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    REQUIRE(!commands.make("object.reset_main_state", document,
      std::vector{instance}));
    history.execute(
      std::make_unique<SetOpacityCommand>(std::vector{instance}, 0.5));
    history.execute(std::make_unique<RenameCommand>(instance, "Renamed"));
    REQUIRE(!commands.make("object.reset_main_state", document,
      std::vector{instance}));
    auto master_rectangle =
      get_master_node(document, parts.m_group, parts.m_rectangle);
    auto copy = find_copy(document, instance, master_rectangle);
    edit(history, copy, make_fill(copy, make_color(0, 255, 0)));
    REQUIRE(!commands.make("object.reset_main_state", document,
      std::vector{instance, parts.m_group}));
    auto command = commands.make("object.reset_main_state", document,
      std::vector{instance});
    REQUIRE(command);
    require_undoable(history, std::move(command));
    REQUIRE(history.get_undo_label() == "Reset to Main State");
    REQUIRE(get_role(document, instance).m_overrides == Overrides());
    REQUIRE(document.get(instance).m_header.m_opacity == 0.5);
    REQUIRE(document.get(instance).m_header.m_name == "Button");
    REQUIRE(document.get(copy).get_reference());
    REQUIRE(get_fill(document, copy) == make_color(255, 0, 0));
    REQUIRE(!commands.make("object.reset_main_state", document,
      std::vector{parts.m_group}));
  }

  TEST_CASE("states") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    auto command = require_undoable(history, std::make_unique<AddStateCommand>(
      component, "Hover State", StateKind::HOVER));
    auto state = static_cast<const AddStateCommand&>(*command).get_state();
    auto& definition = document.get_resources().m_components.at(component);
    REQUIRE(definition.m_default_state == component);
    REQUIRE(definition.m_states.size() == 1);
    auto& added = definition.m_states.front();
    REQUIRE(added.m_id == state);
    REQUIRE(added.m_name == "Hover State");
    REQUIRE(added.m_size == definition.m_size);
    REQUIRE(document.get(added.m_tree).m_header.m_name == "Hover State");
    REQUIRE(added.m_sync_map.size() == 2);
    auto& interactions = document.get_prototype().m_interactions;
    REQUIRE(std::any_of(interactions.begin(), interactions.end(),
      [&] (const auto& interaction) {
        auto transition = std::get_if<StateTransition>(&interaction.m_action);
        return interaction.m_owner == definition.m_master && transition &&
          transition->m_state == state &&
          std::holds_alternative<HoverTrigger>(interaction.m_trigger);
      }));
    REQUIRE(get_role(document, parts.m_group).m_state == state);
    auto state_rectangle = document.get(added.m_tree).m_children.front();
    auto main_rectangle = find_copy(document, parts.m_group, state_rectangle);
    REQUIRE(document.get(main_rectangle).get_reference());
    edit(history, main_rectangle,
      make_fill(main_rectangle, make_color(0, 0, 255)));
    auto& hover = document.get_resources().m_components.at(component).
      m_states.front();
    REQUIRE(get_fill(document, state_rectangle) == make_color(0, 0, 255));
    REQUIRE(hover.m_overrides.m_properties.at(state_rectangle) ==
      std::vector<std::string>{"fill"});
    auto master_rectangle = hover.m_sync_map.at(state_rectangle);
    REQUIRE(get_fill(document, master_rectangle) == make_color(255, 0, 0));
    REQUIRE(get_fill(document, document.get(instance).m_children.front()) ==
      make_color(255, 0, 0));
    require_undoable(history, std::make_unique<SetInstanceStateCommand>(
      std::vector{instance}, state));
    REQUIRE(history.get_undo_label() == "Change State");
    REQUIRE(get_role(document, instance).m_state == state);
    REQUIRE(get_fill(document, document.get(instance).m_children.front()) ==
      make_color(0, 0, 255));
    require_undoable(history, std::make_unique<SetInstanceStateCommand>(
      std::vector{instance}, component));
    REQUIRE(get_role(document, instance).m_state.is_nil());
    REQUIRE(get_fill(document, document.get(instance).m_children.front()) ==
      make_color(255, 0, 0));
    require_undoable(history, std::make_unique<RenameStateCommand>(
      component, state, "Pressed"));
    REQUIRE(document.get_resources().m_components.at(component).m_states.
      front().m_name == "Pressed");
    auto tree = document.get_resources().m_components.at(component).m_states.
      front().m_tree;
    require_undoable(
      history, std::make_unique<RemoveStateCommand>(component, state));
    REQUIRE(document.get_resources().m_components.at(component).m_states.
      empty());
    REQUIRE(!document.find(tree));
    REQUIRE(get_role(document, parts.m_group).m_state.is_nil());
    REQUIRE(std::none_of(interactions.begin(), interactions.end(),
      [&] (const auto& interaction) {
        auto transition = std::get_if<StateTransition>(&interaction.m_action);
        return transition && transition->m_state == state;
      }));
  }

  TEST_CASE("state_override_carry") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    auto master_label =
      get_master_node(document, parts.m_group, parts.m_label);
    auto command = history.execute(std::make_unique<AddStateCommand>(
      component, "Toggle State", StateKind::TOGGLE));
    auto state = static_cast<const AddStateCommand&>(*command).get_state();
    auto copy = find_copy(document, instance, master_label);
    edit(history, copy, std::make_unique<EditTextCommand>(
      copy, make_text(u"Cancel")));
    history.execute(std::make_unique<SetInstanceStateCommand>(
      std::vector{instance}, state));
    auto& role = get_role(document, instance);
    auto& tree = document.get_resources().m_components.at(component).m_states.
      front();
    auto state_label = document.get(tree.m_tree).m_children.back();
    auto label = find_copy(document, instance, state_label);
    REQUIRE(label == copy);
    REQUIRE(*document.get(label).get_text()->m_content == u"Cancel");
    REQUIRE(role.m_overrides.m_properties.contains(label));
  }

  TEST_CASE("swap_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto first = make_component(history, parts.m_group);
    auto loose = NodeId();
    {
      auto transaction = Transaction(document);
      loose = transaction.append(NodeId(), make_header("Loose"),
        make_rectangle_shape(10, 10));
      transaction.commit();
    }
    auto second = make_component(history, loose);
    auto instance = add_instance(history, first, parts.m_artboard, 200, 20);
    history.execute(
      std::make_unique<SetOpacityCommand>(std::vector{instance}, 0.5));
    history.execute(
      std::make_unique<SetVisibilityCommand>(std::vector{instance}, false));
    history.execute(std::make_unique<RenameCommand>(instance, "Custom"));
    auto command = require_undoable(history,
      std::make_unique<SwapComponentCommand>(std::vector{instance}, second));
    REQUIRE(history.get_undo_label() == "Replace Component");
    auto swapped = command->get_selection()->front();
    REQUIRE(!document.find(instance));
    REQUIRE(get_role(document, swapped).m_component == second);
    auto& node = document.get(swapped);
    REQUIRE(node.m_parent == parts.m_artboard);
    REQUIRE(node.m_header.m_transform == make_translation(200, 20));
    REQUIRE(node.m_header.m_opacity == 0.5);
    REQUIRE(!node.m_header.m_is_visible);
    REQUIRE(node.m_header.m_name == "Custom");
  }

  TEST_CASE("swap_main_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto first = make_component(history, parts.m_group);
    auto loose = NodeId();
    {
      auto transaction = Transaction(document);
      loose = transaction.append(NodeId(), make_header("Loose"),
        make_rectangle_shape(10, 10));
      transaction.commit();
    }
    auto second = make_component(history, loose);
    auto instance = add_instance(history, first, parts.m_artboard, 200, 20);
    auto command = history.execute(std::make_unique<SwapComponentCommand>(
      std::vector{parts.m_group}, second));
    REQUIRE(command->get_selection()->size() == 1);
    REQUIRE(!document.find(parts.m_group));
    REQUIRE(!document.find(instance));
    REQUIRE(find_instances(document, first).empty());
    REQUIRE(find_instances(document, second).size() == 3);
  }

  TEST_CASE("root_override") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    require_undoable(history,
      std::make_unique<SetOpacityCommand>(std::vector{instance}, 0.5));
    REQUIRE(get_role(document, instance).m_overrides == Overrides());
    require_undoable(history, std::make_unique<SetVisibilityCommand>(
      std::vector{parts.m_group}, false));
    auto master = document.get_resources().m_components.at(component).
      m_master;
    REQUIRE(document.get(master).m_header.m_is_visible);
    REQUIRE(document.get(instance).m_header.m_is_visible);
    REQUIRE(document.get(instance).m_header.m_opacity == 0.5);
  }

  TEST_CASE("state_sizes") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    history.execute(std::make_unique<AddStateCommand>(
      component, "Hover", StateKind::HOVER));
    history.execute(std::make_unique<InsertNodeCommand>("Rectangle",
      parts.m_group, make_header("Dot", make_translation(100, 100)),
      make_rectangle_shape(50, 50)));
    auto& definition = document.get_resources().m_components.at(component);
    REQUIRE(definition.m_states.front().m_size == Size(150, 150));
    REQUIRE(definition.m_size != Size(150, 150));
  }

  TEST_CASE("duplicate_main") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto command = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{parts.m_group}));
    auto copy = command->get_selection()->front();
    REQUIRE(!get_role(document, copy).m_is_main);
    REQUIRE(get_role(document, parts.m_group).m_is_main);
    REQUIRE(find_main_component(document, component) == parts.m_group);
  }

  TEST_CASE("rename_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    require_undoable(
      history, std::make_unique<RenameComponentCommand>(component, "Primary"));
    auto& definition = document.get_resources().m_components.at(component);
    REQUIRE(definition.m_name == "Primary");
    REQUIRE(document.get(definition.m_master).m_header.m_name == "Primary");
    REQUIRE(document.get(parts.m_group).m_header.m_name == "Primary");
    require_undoable(history,
      std::make_unique<RenameCommand>(parts.m_group, "Secondary"));
    REQUIRE(document.get_resources().m_components.at(component).m_name ==
      "Secondary");
  }

  TEST_CASE("restore_main_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    history.execute(
      std::make_unique<DeleteCommand>(std::vector{parts.m_group}));
    REQUIRE(find_main_component(document, component).is_nil());
    auto command = require_undoable(history,
      std::make_unique<RestoreMainComponentCommand>(
        component, make_translation(500, 0)));
    auto main =
      static_cast<const RestoreMainComponentCommand&>(*command).get_main();
    REQUIRE(document.is_on_canvas(main));
    REQUIRE(get_role(document, main).m_is_main);
    REQUIRE(find_main_component(document, component) == main);
    REQUIRE_THROWS_AS(history.execute(
      std::make_unique<RestoreMainComponentCommand>(component, Matrix())),
      std::invalid_argument);
  }

  TEST_CASE("linked_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    {
      auto transaction = Transaction(document);
      auto definition = document.get_resources().m_components.at(component);
      definition.m_link = ComponentLink("cloud-asset://library", "Library");
      transaction.replace_component(definition);
      transaction.commit();
    }
    REQUIRE(is_linked(document, component));
    REQUIRE_THROWS_AS(history.execute(std::make_unique<AddStateCommand>(
      component, "New State", StateKind::NEW)), std::invalid_argument);
  }

  TEST_CASE("unlink_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto commands = CommandRegistry();
    add_standard_commands(commands);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    {
      auto transaction = Transaction(document);
      auto definition = document.get_resources().m_components.at(component);
      definition.m_link = ComponentLink("cloud-asset://library", "Library");
      transaction.replace_component(definition);
      transaction.commit();
    }
    auto command = commands.make("object.ungroup", document,
      std::vector{instance});
    REQUIRE(command->get_label() == "Unlink Component");
    auto recorded = require_undoable(history, std::move(command));
    auto unlinked = recorded->get_selection()->front();
    REQUIRE(!document.find(instance));
    auto& node = document.get(unlinked);
    REQUIRE(node.m_parent == parts.m_artboard);
    REQUIRE(!get_instance_role(node));
    REQUIRE(node.m_header.m_transform == make_translation(200, 20));
    REQUIRE(node.m_children.size() == 2);
    REQUIRE(!document.get(node.m_children.front()).get_reference());
    REQUIRE(get_fill(document, node.m_children.front()) ==
      make_color(255, 0, 0));
    auto other = add_instance(history, component, parts.m_artboard, 0, 0);
    REQUIRE(make_unlink_component(document,
      std::vector{other, parts.m_group})->get_label() == "Unlink Components");
    REQUIRE(!make_unlink_component(document,
      std::vector{other, parts.m_artboard}));
  }

  TEST_CASE("nested_make_component") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto commands = CommandRegistry();
    add_component_commands(commands);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    REQUIRE(!commands.make("object.make_component", document,
      std::vector{instance}));
    auto master_rectangle =
      get_master_node(document, parts.m_group, parts.m_rectangle);
    auto copy = find_copy(document, instance, master_rectangle);
    auto command = commands.make("object.make_component", document,
      std::vector{copy});
    REQUIRE(command);
    auto recorded = require_undoable(history, std::move(command));
    auto main = static_cast<const MakeComponentCommand&>(*recorded).get_main();
    REQUIRE(document.get(main).m_parent == instance);
    REQUIRE(get_role(document, main).m_is_main);
    REQUIRE(document.get(main).m_children == std::vector{copy});
    REQUIRE(!document.get(copy).get_reference());
    auto& role = get_role(document, instance);
    REQUIRE(!role.m_sync_map.contains(copy));
    REQUIRE(role.m_overrides.m_added == std::vector{main});
    REQUIRE(role.m_overrides.m_removed == std::vector{master_rectangle});
  }

  TEST_CASE("registry") {
    auto document = Document();
    auto parts = build_parts(document);
    auto commands = CommandRegistry();
    add_component_commands(commands);
    REQUIRE(commands.contains("object.make_component"));
    REQUIRE(commands.contains("object.ungroup_component"));
    REQUIRE(commands.contains("object.reset_main_state"));
    REQUIRE(commands.make(
      "object.make_component", document, std::vector{parts.m_rectangle}));
    REQUIRE(!commands.make(
      "object.make_component", document, std::vector{parts.m_artboard}));
    REQUIRE(!commands.make(
      "object.make_component", document, std::vector<NodeId>()));
    REQUIRE(!commands.make(
      "object.ungroup_component", document, std::vector{parts.m_group}));
  }

  TEST_CASE("inherited_interactions") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    auto command = history.execute(std::make_unique<AddStateCommand>(
      component, "Hover State", StateKind::HOVER));
    auto state = static_cast<const AddStateCommand&>(*command).get_state();
    auto& master =
      document.get_resources().m_components.at(component).m_master;
    auto source = InteractionId();
    for(auto& interaction : document.get_prototype().m_interactions) {
      if(interaction.m_owner == master) {
        source = interaction.m_id;
      }
    }
    REQUIRE(!source.is_nil());
    auto find = [&] (const InteractionId& id) -> const Interaction* {
      for(auto& interaction : document.get_prototype().m_interactions) {
        if(interaction.m_id == id) {
          return &interaction;
        }
      }
      return nullptr;
    };
    auto copy = find(derive_inherited_id(source, instance));
    REQUIRE(copy);
    REQUIRE(copy->m_owner == instance);
    REQUIRE(copy->m_trigger == Trigger(HoverTrigger()));
    REQUIRE(std::get<StateTransition>(copy->m_action).m_state == state);
    REQUIRE(!find(derive_inherited_id(source, parts.m_group)));
    auto added = add_instance(history, component, parts.m_artboard, 200, 100);
    REQUIRE(find(derive_inherited_id(source, added)));
    history.execute(std::make_unique<SetInstanceStateCommand>(
      std::vector{added}, state));
    REQUIRE(!find(derive_inherited_id(source, added)));
    history.execute(
      std::make_unique<RemoveStateCommand>(component, state));
    REQUIRE(!find(derive_inherited_id(source, instance)));
    REQUIRE(history.undo());
    REQUIRE(find(derive_inherited_id(source, instance)));
  }

  TEST_CASE("nested_override") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto inner = make_component(history, parts.m_group);
    auto nested = add_instance(history, inner, parts.m_artboard, 200, 20);
    auto outer_main = nested;
    auto outer = make_component(history, outer_main);
    auto instance = add_instance(history, outer, parts.m_artboard, 0, 150);
    history.execute(std::make_unique<MakeEditableCommand>(
      std::vector{instance}, "Edit"));
    auto copy = document.get(instance).m_children.front();
    history.execute(std::make_unique<MakeEditableCommand>(
      std::vector{copy}, "Edit"));
    REQUIRE(get_instance_role(document.get(copy)));
    auto rectangle = document.get(copy).m_children.front();
    edit(history, rectangle, make_fill(rectangle, make_color(0, 255, 0)));
    REQUIRE(get_overridden(document, copy, rectangle) ==
      std::vector<std::string>{"fill"});
    REQUIRE(get_overridden(document, instance, rectangle) ==
      std::vector<std::string>{"fill"});
    history.execute(make_fill(parts.m_rectangle, make_color(0, 0, 255)));
    REQUIRE(get_fill(document, rectangle) == make_color(0, 255, 0));
    auto main_rectangle = document.get(nested).m_children.front();
    edit(history, main_rectangle,
      make_fill(main_rectangle, make_color(9, 9, 9)));
    REQUIRE(get_fill(document, rectangle) == make_color(0, 255, 0));
  }

  TEST_CASE("layout_roles") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    history.execute(std::make_unique<SetStackCommand>(std::vector{
      parts.m_group}, Orientation::VERTICAL, estimate_text_bounds));
    auto& master =
      document.get_resources().m_components.at(component).m_master;
    REQUIRE(document.get(master).get_group()->m_stack);
    auto& stack = document.get(instance).get_group()->m_stack;
    REQUIRE(stack);
    REQUIRE(stack->m_orientation == Orientation::VERTICAL);
    history.execute(std::make_unique<SetStackSpacingCommand>(
      std::vector{parts.m_group}, 12, estimate_text_bounds));
    auto& spacing =
      document.get(instance).get_group()->m_stack->m_spacing;
    REQUIRE(spacing.size() == 1);
    REQUIRE(spacing.begin()->second == 12);
    REQUIRE(std::ranges::contains(
      document.get(instance).m_children, spacing.begin()->first));
  }

  TEST_CASE("sizes") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    auto instance = add_instance(history, component, parts.m_artboard, 200, 20);
    history.execute(std::make_unique<InsertNodeCommand>("Rectangle",
      parts.m_group, make_header("Dot", make_translation(100, 100)),
      make_rectangle_shape(50, 50)));
    REQUIRE(document.get_resources().m_components.at(component).m_size ==
      Size(150, 150));
    REQUIRE(get_role(document, parts.m_group).m_size == Size(150, 150));
    REQUIRE(get_role(document, instance).m_size == Size(150, 150));
  }

  TEST_CASE("instance_names") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto command = history.execute(std::make_unique<MakeComponentCommand>(
      std::vector{parts.m_rectangle, parts.m_label}));
    auto component =
      static_cast<const MakeComponentCommand&>(*command).get_component();
    auto first = add_instance(history, component, parts.m_artboard, 200, 20);
    REQUIRE(document.get(first).m_header.m_name ==
      "Component 1 \xE2\x80\x93 2");
    auto duplicate =
      history.execute(std::make_unique<DuplicateCommand>(std::vector{first}));
    REQUIRE(document.get(duplicate->get_selection()->front()).m_header.
      m_name == "Component 1 \xE2\x80\x93 3");
  }

  TEST_CASE("copied_artboard_names") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto command = history.execute(std::make_unique<MakeComponentCommand>(
      std::vector{parts.m_rectangle, parts.m_label}));
    auto component =
      static_cast<const MakeComponentCommand&>(*command).get_component();
    add_instance(history, component, parts.m_artboard, 200, 20);
    auto duplicate = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{parts.m_artboard}));
    auto copy = duplicate->get_selection()->front();
    auto names = std::vector<std::string>();
    for(auto& instance : find_instances(document, component)) {
      if(find_artboard(document, instance) == copy) {
        names.push_back(document.get(instance).m_header.m_name);
      }
    }
    std::ranges::sort(names);
    REQUIRE(names == std::vector<std::string>({"Component 1 \xE2\x80\x93 3",
      "Component 1 \xE2\x80\x93 4"}));
  }

  TEST_CASE("auto_named_group") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    history.execute(std::make_unique<RenameCommand>(parts.m_group, "Group 2"));
    auto component = make_component(history, parts.m_group);
    auto& main = document.get(parts.m_group);
    REQUIRE(main.m_header.m_name == "Component 1 \xE2\x80\x93 1");
    auto& made = document.get_resources().m_components.at(component);
    REQUIRE(made.m_name == "Component 1");
    REQUIRE(document.get(made.m_master).m_header.m_name == "Component 1");
    auto duplicate = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{parts.m_group}));
    auto copy = duplicate->get_selection()->front();
    REQUIRE(document.get(copy).m_header.m_name ==
      "Component 1 \xE2\x80\x93 2");
    duplicate =
      history.execute(std::make_unique<DuplicateCommand>(std::vector{copy}));
    REQUIRE(document.get(duplicate->get_selection()->front()).m_header.
      m_name == "Component 1 \xE2\x80\x93 3");
  }

  TEST_CASE("custom_named_group") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto component = make_component(history, parts.m_group);
    REQUIRE(document.get_resources().m_components.at(component).m_name ==
      "Button");
    auto duplicate = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{parts.m_group}));
    REQUIRE(document.get(duplicate->get_selection()->front()).m_header.
      m_name == "Button \xE2\x80\x93 1");
  }

  TEST_CASE("stack_item") {
    auto document = Document();
    auto artboard = NodeId();
    auto stack = NodeId();
    auto items = std::vector<NodeId>();
    {
      auto transaction = Transaction(document);
      auto board = Artboard();
      board.m_frame = Rect(0, 0, 400, 300);
      artboard = transaction.append(NodeId(), make_header("Artboard"), board);
      stack = transaction.append(artboard, make_header("Group 1"), Group());
      for(auto i = 0; i != 3; ++i) {
        items.push_back(transaction.append(stack, make_header(
          "Rectangle " + std::to_string(i + 1), make_translation(0, i * 92)),
          make_rectangle_shape(50, 30)));
      }
      transaction.commit();
      document.clear_history();
    }
    auto history = History(document);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{stack}, Orientation::VERTICAL, estimate_text_bounds));
    auto command = history.execute(std::make_unique<MakeComponentCommand>(
      std::vector{items[1]}));
    auto& made = static_cast<const MakeComponentCommand&>(*command);
    auto& spacing = document.get(stack).get_group()->m_stack->m_spacing;
    REQUIRE(spacing.size() == 2);
    REQUIRE(spacing.at(made.get_main()) == 62);
    REQUIRE(spacing.at(items[0]) == 62);
    REQUIRE(!spacing.contains(items[2]));
  }

  TEST_CASE("measurer") {
    auto document = Document();
    auto parts = build_parts(document);
    auto history = History(document);
    auto measurer = [] (const Text&) { return Rect(0, 0, 200, 50); };
    history.set_text_measurer(measurer);
    auto command = history.execute(std::make_unique<MakeComponentCommand>(
      std::vector{parts.m_group}, measurer));
    auto component =
      static_cast<const MakeComponentCommand&>(*command).get_component();
    REQUIRE(document.get_resources().m_components.at(component).m_size ==
      Size(210, 55));
    auto inserted = history.execute(std::make_unique<InsertInstanceCommand>(
      component, parts.m_artboard, make_translation(0, 0), measurer));
    REQUIRE(get_role(document, static_cast<const InsertInstanceCommand&>(
      *inserted).get_instance()).m_size == Size(210, 55));
  }
}

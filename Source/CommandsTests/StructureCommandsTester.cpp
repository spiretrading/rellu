#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <doctest/doctest.h>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  void set_opacity(Document& document, const NodeId& id, double opacity) {
    auto transaction = Transaction(document);
    auto header = document.get(id).m_header;
    header.m_opacity = opacity;
    transaction.replace_header(header);
    transaction.commit();
  }

  void rename(Document& document, const NodeId& id, std::string name) {
    auto transaction = Transaction(document);
    auto header = document.get(id).m_header;
    header.m_name = std::move(name);
    header.m_has_custom_name = false;
    transaction.replace_header(header);
    transaction.commit();
  }
}

TEST_SUITE("StructureCommands") {
  TEST_CASE("arrange") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto& rectangle = scene.m_rectangle;
    auto& group = scene.m_group;
    auto& mask = scene.m_mask;
    auto& instance = scene.m_instance;
    auto arrange = [&] (std::vector<NodeId> nodes, Arrangement arrangement) {
      return std::make_unique<ArrangeCommand>(std::move(nodes), arrangement);
    };
    auto require_order = [&] (const std::vector<NodeId>& order) {
      REQUIRE(document.get_children(scene.m_artboard) == order);
    };
    require_undoable(history, arrange({rectangle}, Arrangement::BRING_FORWARD));
    REQUIRE(history.get_undo_label() == "Bring Forward");
    require_order({group, rectangle, mask, instance});
    require_undoable(history, arrange({instance}, Arrangement::SEND_BACKWARD));
    REQUIRE(history.get_undo_label() == "Send Backward");
    require_order({group, rectangle, instance, mask});
    require_undoable(
      history, arrange({group, rectangle}, Arrangement::BRING_TO_FRONT));
    REQUIRE(history.get_undo_label() == "Bring to Front");
    require_order({instance, mask, group, rectangle});
    require_undoable(
      history, arrange({mask, rectangle}, Arrangement::SEND_TO_BACK));
    REQUIRE(history.get_undo_label() == "Send to Back");
    require_order({mask, rectangle, instance, group});
    require_undoable(
      history, arrange({rectangle, instance}, Arrangement::BRING_FORWARD));
    require_order({mask, group, rectangle, instance});
    REQUIRE(!history.execute(arrange({mask}, Arrangement::SEND_TO_BACK)));
    REQUIRE(!history.execute(arrange({instance}, Arrangement::BRING_FORWARD)));
  }

  TEST_CASE("arrange_canvas") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<ArrangeCommand>(
      std::vector{scene.m_loose, scene.m_ellipse}, Arrangement::SEND_TO_BACK));
    REQUIRE(document.get_canvas() ==
      std::vector{scene.m_loose, scene.m_artboard});
    REQUIRE(document.get_children(scene.m_group) ==
      std::vector{scene.m_ellipse, scene.m_text});
  }

  TEST_CASE("reparent") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto rectangle = get_world_transform(document, scene.m_rectangle);
    auto loose = get_world_transform(document, scene.m_loose);
    require_undoable(history, std::make_unique<ReparentCommand>(
      std::vector{scene.m_loose, scene.m_rectangle}, scene.m_group, 1));
    REQUIRE(history.get_undo_label() == "Move");
    REQUIRE(document.get_children(scene.m_group) == std::vector{scene.m_ellipse,
      scene.m_rectangle, scene.m_loose, scene.m_text});
    require_near(get_world_transform(document, scene.m_rectangle), rectangle);
    require_near(get_world_transform(document, scene.m_loose), loose);
    REQUIRE_THROWS_AS(history.execute(std::make_unique<ReparentCommand>(
      std::vector{scene.m_group}, scene.m_ellipse, 0)),
      std::invalid_argument);
    REQUIRE_THROWS_AS(history.execute(std::make_unique<ReparentCommand>(
      std::vector{scene.m_group}, generate_node_id(), 0)),
      std::invalid_argument);
  }

  TEST_CASE("group") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto rectangle = get_world_transform(document, scene.m_rectangle);
    auto mask = get_world_transform(document, scene.m_mask);
    auto command = require_undoable(history, std::make_unique<GroupCommand>(
      std::vector{scene.m_mask, scene.m_rectangle}));
    REQUIRE(history.get_undo_label() == "Group");
    auto& group = static_cast<const GroupCommand&>(*command).get_group();
    REQUIRE(command->get_selection() == std::vector{group});
    REQUIRE(document.get_children(scene.m_artboard) ==
      std::vector{scene.m_group, group, scene.m_instance});
    REQUIRE(document.get_children(group) ==
      std::vector{scene.m_rectangle, scene.m_mask});
    REQUIRE(document.get(group).m_header.m_name == "Group 1");
    REQUIRE(document.get(group).m_header.m_transform ==
      make_translation(10, 10));
    require_near(get_world_transform(document, scene.m_rectangle), rectangle);
    require_near(get_world_transform(document, scene.m_mask), mask);
  }

  TEST_CASE("group_across_parents") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto ellipse = get_world_transform(document, scene.m_ellipse);
    auto reference = get_world_transform(document, scene.m_reference);
    auto command = require_undoable(history, std::make_unique<GroupCommand>(
      std::vector{scene.m_loose, scene.m_ellipse, scene.m_reference},
      "Mixed"));
    auto& group = static_cast<const GroupCommand&>(*command).get_group();
    REQUIRE(document.get_canvas() == std::vector{scene.m_artboard, group});
    REQUIRE(document.get_children(group) ==
      std::vector{scene.m_ellipse, scene.m_reference, scene.m_loose});
    REQUIRE(document.get(group).m_header.m_name == "Mixed");
    require_near(get_world_transform(document, scene.m_ellipse), ellipse);
    require_near(get_world_transform(document, scene.m_reference), reference);
  }

  TEST_CASE("group_artboard") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto before = take_snapshot(document);
    REQUIRE_THROWS_AS(history.execute(std::make_unique<GroupCommand>(
      std::vector{scene.m_artboard})), std::invalid_argument);
    REQUIRE_THROWS_AS(history.execute(std::make_unique<GroupCommand>(
      std::vector<NodeId>())), std::invalid_argument);
    REQUIRE(take_snapshot(document) == before);
    REQUIRE(!history.can_undo());
  }

  TEST_CASE("ungroup") {
    auto document = Document();
    auto scene = build_scene(document);
    set_opacity(document, scene.m_group, 0.5);
    auto history = History(document);
    auto ellipse = get_world_transform(document, scene.m_ellipse);
    auto text = get_world_transform(document, scene.m_text);
    auto command = require_undoable(history, std::make_unique<UngroupCommand>(
      std::vector{scene.m_group, scene.m_loose}));
    REQUIRE(history.get_undo_label() == "Ungroup");
    REQUIRE(!document.find(scene.m_group));
    REQUIRE(document.get_children(scene.m_artboard) ==
      std::vector{scene.m_rectangle, scene.m_ellipse, scene.m_text,
        scene.m_mask, scene.m_instance});
    require_near(get_world_transform(document, scene.m_ellipse), ellipse);
    require_near(get_world_transform(document, scene.m_text), text);
    REQUIRE(document.get(scene.m_ellipse).m_header.m_opacity == 0.5);
    REQUIRE(command->get_selection() ==
      std::vector{scene.m_ellipse, scene.m_text, scene.m_loose});
  }

  TEST_CASE("ungroup_rotation") {
    auto document = Document();
    auto group = NodeId();
    auto turned = NodeId();
    auto other = NodeId();
    {
      auto transaction = Transaction(document);
      auto header = make_header("Group",
        make_translation(50, 50) * make_rotation(std::numbers::pi / 6));
      header.m_rotation = 30;
      group = transaction.append(NodeId(), header, Group());
      header = make_header("Turned", make_rotation(-std::numbers::pi / 2));
      header.m_rotation = -90;
      turned = transaction.append(group, header, make_rectangle_shape(40, 20));
      header = make_header("Other", make_rotation(-std::numbers::pi / 2));
      header.m_rotation = -90;
      other = transaction.append(group, header, make_rectangle_shape(40, 20));
      transaction.commit();
    }
    auto history = History(document);
    require_undoable(history, std::make_unique<ReparentCommand>(
      std::vector{other}, group, 0));
    REQUIRE(document.get(other).m_header.m_rotation == -90);
    require_undoable(history, std::make_unique<ReparentCommand>(
      std::vector{other}, NodeId(), 0));
    REQUIRE(document.get(other).m_header.m_rotation == doctest::Approx(-60));
    require_undoable(
      history, std::make_unique<UngroupCommand>(std::vector{group}));
    REQUIRE(document.get(turned).m_header.m_rotation == doctest::Approx(-60));
    REQUIRE(is_congruent(-60, document.get(turned).m_header.m_transform));
  }

  TEST_CASE("ungroupable") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    REQUIRE(is_ungroupable(document, scene.m_group));
    REQUIRE(!is_ungroupable(document, scene.m_mask));
    REQUIRE(!is_ungroupable(document, scene.m_instance));
    REQUIRE(!is_ungroupable(document, scene.m_master));
    REQUIRE(!is_ungroupable(document, scene.m_rectangle));
    REQUIRE(!history.execute(std::make_unique<UngroupCommand>(
      std::vector{scene.m_mask, scene.m_instance})));
  }

  TEST_CASE("group_ungroup") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle, scene.m_group, scene.m_mask};
    auto transforms = std::vector<Matrix>();
    for(auto& id : std::vector{scene.m_rectangle, scene.m_ellipse,
        scene.m_text, scene.m_path}) {
      transforms.push_back(get_world_transform(document, id));
    }
    auto group = history.execute(std::make_unique<GroupCommand>(nodes));
    REQUIRE(group);
    REQUIRE(document.get(group->get_selection()->front()).m_header.m_name ==
      "Group 1");
    REQUIRE(history.execute(std::make_unique<GroupCommand>(
      std::vector{scene.m_instance})));
    REQUIRE(document.get(document.get_children(scene.m_artboard).back()).
      m_header.m_name == "Group 2");
    history.undo();
    REQUIRE(history.execute(std::make_unique<UngroupCommand>(
      *group->get_selection())));
    REQUIRE(document.get_children(scene.m_artboard) == std::vector{
      scene.m_rectangle, scene.m_group, scene.m_mask, scene.m_instance});
    auto i = std::size_t(0);
    for(auto& id : std::vector{scene.m_rectangle, scene.m_ellipse,
        scene.m_text, scene.m_path}) {
      require_near(get_world_transform(document, id), transforms[i]);
      ++i;
    }
  }

  TEST_CASE("delete") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto command = require_undoable(history, std::make_unique<DeleteCommand>(
      std::vector{scene.m_ellipse, scene.m_rectangle, scene.m_group}));
    REQUIRE(history.get_undo_label() == "Delete");
    REQUIRE(!document.find(scene.m_rectangle));
    REQUIRE(!document.find(scene.m_ellipse));
    REQUIRE(document.get_prototype().m_interactions.empty());
    REQUIRE(command->get_selection() == std::vector<NodeId>());
  }

  TEST_CASE("duplicate") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto command = require_undoable(history,
      std::make_unique<DuplicateCommand>(
        std::vector{scene.m_instance, scene.m_rectangle}, Point(0, 0),
        DuplicateMode::PROTOTYPE));
    REQUIRE(history.get_undo_label() == "Duplicate");
    auto& duplicate = static_cast<const DuplicateCommand&>(*command);
    auto& mapping = duplicate.get_mapping();
    REQUIRE(mapping.size() == 5);
    auto rectangle = mapping.at(scene.m_rectangle);
    auto instance = mapping.at(scene.m_instance);
    auto reference = mapping.at(scene.m_reference);
    auto tree = mapping.at(scene.m_state_tree);
    REQUIRE(duplicate.get_copies() == std::vector{rectangle, instance});
    REQUIRE(command->get_selection() == duplicate.get_copies());
    REQUIRE(document.get_children(scene.m_artboard) == std::vector{
      scene.m_rectangle, rectangle, scene.m_group, scene.m_mask,
      scene.m_instance, instance});
    REQUIRE(document.get(rectangle).m_header.m_name == "Rectangle");
    REQUIRE(document.get(rectangle).m_header.m_transform ==
      make_translation(10, 10));
    REQUIRE(document.get_children(instance) == std::vector{reference});
    REQUIRE(document.get(reference).get_reference()->m_source ==
      scene.m_background);
    auto& copy =
      *document.get(instance).get_group()->m_component_instance;
    REQUIRE(copy.m_sync_map == std::map<NodeId, NodeId>{
      {instance, scene.m_master}, {reference, scene.m_background}});
    REQUIRE(copy.m_states.front().m_tree == tree);
    REQUIRE(document.find(tree));
    REQUIRE(document.get(tree).m_parent.is_nil());
    REQUIRE(document.get_children(tree).size() == 1);
    auto& interactions = document.get_prototype().m_interactions;
    REQUIRE(interactions.size() == 2);
    REQUIRE(interactions.back().m_owner == rectangle);
    REQUIRE(interactions.back().m_id != scene.m_interaction);
    REQUIRE(interactions.back().m_action ==
      Action(ArtboardTransition(scene.m_artboard)));
    history.execute(
      std::make_unique<DuplicateCommand>(std::vector{scene.m_rectangle}));
    REQUIRE(document.get_prototype().m_interactions.size() == 2);
  }

  TEST_CASE("duplicate_names") {
    auto document = Document();
    auto scene = build_scene(document);
    rename(document, scene.m_rectangle, "Rectangle 1");
    rename(document, scene.m_group, "Group 4");
    rename(document, scene.m_ellipse, "Ellipse 1");
    rename(document, scene.m_mask, "Mask Group 2");
    rename(document, scene.m_path, "Path 1");
    auto history = History(document);
    auto command = history.execute(std::make_unique<DuplicateCommand>(
      std::vector{scene.m_rectangle, scene.m_group, scene.m_mask,
        scene.m_instance, scene.m_loose}));
    auto& mapping = static_cast<const DuplicateCommand&>(*command).
      get_mapping();
    auto get_name = [&] (const NodeId& id) {
      return document.get(mapping.at(id)).m_header.m_name;
    };
    REQUIRE(get_name(scene.m_rectangle) == "Rectangle 2");
    REQUIRE(get_name(scene.m_group) == "Group 5");
    REQUIRE(get_name(scene.m_ellipse) == "Ellipse 1");
    REQUIRE(get_name(scene.m_text) == "Text");
    REQUIRE(get_name(scene.m_mask) == "Mask Group 3");
    REQUIRE(get_name(scene.m_path) == "Path 2");
    REQUIRE(get_name(scene.m_instance) == "Button \xE2\x80\x93 1");
    REQUIRE(get_name(scene.m_loose) == "Loose");
    auto unnamed = history.execute(std::make_unique<DuplicateCommand>(
      std::vector{scene.m_rectangle}, Point(0, 0), DuplicateMode::CLONE));
    REQUIRE(document.get(unnamed->get_selection()->front()).m_header.
      m_name == "Rectangle 1");
  }

  TEST_CASE("duplicate_disabled_naming") {
    auto document = Document();
    auto scene = build_scene(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_rectangle).m_header;
      header.m_name = "Rectangle 1";
      header.m_has_custom_name = false;
      header.m_is_naming_disabled = true;
      transaction.replace_header(header);
      transaction.commit();
    }
    auto history = History(document);
    auto command = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{scene.m_rectangle}));
    auto& copy = document.get(command->get_selection()->front()).m_header;
    REQUIRE(copy.m_name == "Rectangle 1");
    REQUIRE(copy.m_is_naming_disabled);
  }

  TEST_CASE("duplicate_mask_names") {
    auto document = Document();
    auto scene = build_scene(document);
    rename(document, scene.m_mask, "Group 7");
    auto history = History(document);
    auto command = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{scene.m_mask}));
    REQUIRE(document.get(command->get_selection()->front()).m_header.
      m_name == "Group 8");
    rename(document, scene.m_mask, "Mask Group");
    command = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{scene.m_mask}));
    REQUIRE(document.get(command->get_selection()->front()).m_header.
      m_name == "Mask Group 1");
  }

  TEST_CASE("duplicate_artboards") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto command = require_undoable(history, std::make_unique<DuplicateCommand>(
      std::vector{scene.m_artboard}, Rect(0, 0, 2000, 1000),
      DuplicateMode::DESIGN));
    auto copy = command->get_selection()->front();
    auto& node = document.get(copy);
    REQUIRE(node.m_header.m_name == "Artboard \xE2\x80\x93 1");
    REQUIRE(node.get_artboard()->m_frame == Rect(570, 50, 400, 300));
    REQUIRE(node.get_artboard()->m_layout_grid ==
      make_initial_grid(*node.get_artboard()));
    REQUIRE(!document.get(scene.m_artboard).get_artboard()->m_layout_grid);
    REQUIRE(document.get_canvas() ==
      std::vector{scene.m_artboard, copy, scene.m_loose});
    REQUIRE(document.get(node.m_children.front()).m_header.m_name ==
      "Rectangle");
    REQUIRE(get_world_transform(document, node.m_children.front()) ==
      make_translation(580, 60));
    auto over = history.execute(
      std::make_unique<DuplicateCommand>(std::vector{scene.m_artboard}));
    auto& overlapping = document.get(over->get_selection()->front());
    REQUIRE(overlapping.m_header.m_name == "Artboard \xE2\x80\x93 2");
    REQUIRE(overlapping.get_artboard()->m_frame == Rect(100, 50, 400, 300));
    auto prototype = history.execute(std::make_unique<DuplicateCommand>(
      std::vector{scene.m_artboard}, Point(0, 0), DuplicateMode::PROTOTYPE));
    REQUIRE(!document.get(prototype->get_selection()->front()).
      get_artboard()->m_layout_grid);
  }

  TEST_CASE("duplicate_references") {
    auto document = Document();
    auto group = NodeId();
    auto shape = NodeId();
    auto reference = NodeId();
    {
      auto transaction = Transaction(document);
      group = transaction.append(NodeId(), make_header("Group"), Group());
      shape = transaction.append(
        group, make_header("Shape"), make_rectangle_shape(5, 5));
      reference = transaction.append(group, make_header("Reference",
        make_translation(10, 0)), Reference(shape, make_translation(10, 0)));
      transaction.commit();
    }
    auto history = History(document);
    auto command = require_undoable(history,
      std::make_unique<DuplicateCommand>(std::vector{group}, Point(0, 20)));
    auto& duplicate = static_cast<const DuplicateCommand&>(*command);
    auto& mapping = duplicate.get_mapping();
    auto copy = mapping.at(group);
    REQUIRE(document.get_canvas() == std::vector{group, copy});
    REQUIRE(document.get(mapping.at(reference)).get_reference()->m_source ==
      mapping.at(shape));
    REQUIRE(document.get(reference).get_reference()->m_source == shape);
    REQUIRE(get_world_transform(document, copy) == make_translation(0, 20));
    REQUIRE(get_world_bounds(document, mapping.at(reference)) ==
      Rect(10, 20, 5, 5));
  }
}

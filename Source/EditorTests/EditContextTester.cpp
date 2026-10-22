#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Editor/EditContext.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId nest(Document& document, const NodeId& id) {
    auto command = GroupCommand(std::vector{id}, "Nested");
    auto transaction = Transaction(document);
    command.apply(document, transaction);
    transaction.commit();
    return command.get_group();
  }

  void remove(Document& document, const NodeId& id) {
    auto transaction = Transaction(document);
    transaction.remove(id);
    transaction.commit();
  }
}

TEST_SUITE("EditContext") {
  TEST_CASE("enter_exit") {
    auto document = Document();
    auto scene = build_scene(document);
    auto nested = nest(document, scene.m_ellipse);
    auto context = EditContext(document);
    auto count = 0;
    auto subscription = context.subscribe([&] { ++count; });
    REQUIRE(context.get_container().is_nil());
    REQUIRE(!context.enter(scene.m_rectangle));
    REQUIRE(!context.enter(generate_node_id()));
    REQUIRE(count == 0);
    REQUIRE(context.enter(nested));
    REQUIRE(context.get_container() == nested);
    REQUIRE(context.get_artboard() == scene.m_artboard);
    REQUIRE(count == 1);
    REQUIRE(context.enter(nested));
    REQUIRE(count == 1);
    context.exit();
    REQUIRE(context.get_container() == scene.m_group);
    context.exit();
    REQUIRE(context.get_container().is_nil());
    REQUIRE(context.get_artboard() == scene.m_artboard);
    REQUIRE(count == 3);
    context.exit();
    REQUIRE(count == 3);
  }

  TEST_CASE("scope") {
    auto document = Document();
    auto scene = build_scene(document);
    auto context = EditContext(document);
    REQUIRE(context.get_scope().is_nil());
    context.set_artboard(scene.m_rectangle);
    REQUIRE(context.get_artboard().is_nil());
    context.set_artboard(scene.m_artboard);
    REQUIRE(context.get_scope() == scene.m_artboard);
    context.enter(scene.m_group);
    REQUIRE(context.get_scope() == scene.m_group);
    context.set_artboard(NodeId());
    REQUIRE(context.get_artboard().is_nil());
  }

  TEST_CASE("resolve") {
    auto document = Document();
    auto scene = build_scene(document);
    auto context = EditContext(document);
    REQUIRE(context.resolve(scene.m_ellipse) == scene.m_group);
    REQUIRE(context.resolve(scene.m_rectangle) == scene.m_rectangle);
    REQUIRE(context.resolve(scene.m_artboard) == scene.m_artboard);
    REQUIRE(context.resolve(scene.m_loose) == scene.m_loose);
    REQUIRE(context.resolve(scene.m_reference) == scene.m_instance);
    REQUIRE(context.resolve(generate_node_id()).is_nil());
    context.enter(scene.m_group);
    REQUIRE(context.resolve(scene.m_ellipse) == scene.m_ellipse);
    REQUIRE(context.resolve(scene.m_rectangle) == scene.m_rectangle);
    context.exit();
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_group).m_header;
      header.m_is_locked = true;
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(context.resolve(scene.m_ellipse) == scene.m_group);
  }

  TEST_CASE("hover") {
    auto document = Document();
    auto scene = build_scene(document);
    auto context = EditContext(document);
    auto count = 0;
    auto subscription = context.subscribe([&] { ++count; });
    context.set_hover(scene.m_rectangle);
    context.set_hover(scene.m_rectangle);
    context.set_hover(generate_node_id());
    REQUIRE(context.get_hover() == scene.m_rectangle);
    REQUIRE(count == 1);
    context.set_hover(NodeId());
    REQUIRE(context.get_hover().is_nil());
    REQUIRE(count == 2);
  }

  TEST_CASE("interactions") {
    auto document = Document();
    auto scene = build_scene(document);
    auto context = EditContext(document);
    auto count = 0;
    auto subscription = context.subscribe([&] { ++count; });
    auto second = generate_node_id();
    context.set_interaction(scene.m_interaction);
    REQUIRE(context.get_interaction() == scene.m_interaction);
    REQUIRE(context.get_interactions() ==
      std::vector({scene.m_interaction}));
    REQUIRE(context.get_interaction_owner() == scene.m_rectangle);
    context.set_interactions(
      scene.m_rectangle, {scene.m_interaction, second});
    REQUIRE(context.get_interaction() == scene.m_interaction);
    REQUIRE(context.get_interactions() ==
      std::vector({scene.m_interaction, second}));
    context.set_interactions(
      scene.m_rectangle, {scene.m_interaction, second});
    REQUIRE(count == 2);
    context.set_interactions(scene.m_rectangle, {});
    REQUIRE(context.get_interaction().is_nil());
    REQUIRE(context.get_interaction_owner() == scene.m_rectangle);
    REQUIRE(count == 3);
    context.set_interaction(InteractionId());
    REQUIRE(context.get_interaction_owner().is_nil());
    REQUIRE(count == 4);
  }

  TEST_CASE("pending") {
    auto document = Document();
    auto scene = build_scene(document);
    auto context = EditContext(document);
    auto count = 0;
    auto subscription = context.subscribe([&] { ++count; });
    auto pending = Interaction(generate_node_id(), scene.m_rectangle,
      TapTrigger(), ArtboardTransition());
    context.set_pending({pending});
    REQUIRE(context.get_pending().empty());
    REQUIRE(count == 0);
    context.set_interactions(scene.m_rectangle, {pending.m_id});
    context.set_pending({pending});
    REQUIRE(context.get_pending() == std::vector({pending}));
    REQUIRE(count == 2);
    context.set_interactions(scene.m_rectangle, {scene.m_interaction});
    REQUIRE(context.get_pending().empty());
  }

  TEST_CASE("restore_interactions") {
    auto document = Document();
    auto scene = build_scene(document);
    auto context = EditContext(document);
    auto kept = Interaction(generate_node_id(), scene.m_rectangle,
      TapTrigger(), ArtboardTransition());
    auto orphan = Interaction(generate_node_id(), generate_node_id(),
      TapTrigger(), ArtboardTransition());
    auto missing = generate_node_id();
    auto selection = InteractionSelection(
      {scene.m_interaction, kept.m_id, orphan.m_id, missing}, {kept, orphan});
    context.restore_interactions(scene.m_rectangle, selection);
    REQUIRE(context.get_interactions().empty());
    REQUIRE(context.get_interaction_owner().is_nil());
    context.set_prototyping(true);
    REQUIRE(context.get_interaction_selection() == InteractionSelection());
    context.restore_interactions(scene.m_rectangle, selection);
    REQUIRE(context.get_interactions() ==
      std::vector({scene.m_interaction, kept.m_id}));
    REQUIRE(context.get_pending() == std::vector{kept});
    REQUIRE(context.get_interaction_owner() == scene.m_rectangle);
    context.restore_interactions(scene.m_ellipse, std::nullopt);
    REQUIRE(context.get_interactions().empty());
    REQUIRE(context.get_pending().empty());
    REQUIRE(context.get_interaction_owner() == scene.m_ellipse);
  }

  TEST_CASE("removal") {
    auto document = Document();
    auto scene = build_scene(document);
    auto nested = nest(document, scene.m_ellipse);
    auto context = EditContext(document);
    context.enter(nested);
    context.set_hover(scene.m_ellipse);
    remove(document, nested);
    REQUIRE(context.get_container() == scene.m_group);
    REQUIRE(context.get_hover().is_nil());
    REQUIRE(document.undo());
    context.enter(nested);
    remove(document, scene.m_group);
    REQUIRE(context.get_container().is_nil());
    REQUIRE(context.get_artboard() == scene.m_artboard);
    remove(document, scene.m_artboard);
    REQUIRE(context.get_artboard().is_nil());
  }
}

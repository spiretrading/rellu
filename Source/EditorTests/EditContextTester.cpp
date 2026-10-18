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
    REQUIRE(context.resolve(scene.m_ellipse).is_nil());
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

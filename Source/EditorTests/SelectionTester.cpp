#include <doctest/doctest.h>
#include "Rellu/Editor/Selection.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  void edit_header(Document& document, const NodeId& id, auto edit) {
    auto transaction = Transaction(document);
    auto header = document.get(id).m_header;
    edit(header);
    transaction.replace_header(header);
    transaction.commit();
  }
}

TEST_SUITE("Selection") {
  TEST_CASE("select") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = Selection(document);
    auto count = 0;
    auto subscription = selection.subscribe([&] { ++count; });
    REQUIRE(selection.is_empty());
    REQUIRE(selection.get_primary().is_nil());
    selection.select(scene.m_rectangle);
    REQUIRE(selection.get_nodes() == std::vector{scene.m_rectangle});
    REQUIRE(selection.get_primary() == scene.m_rectangle);
    REQUIRE(count == 1);
    selection.select(scene.m_rectangle);
    REQUIRE(count == 1);
    selection.select({scene.m_group, generate_node_id(), scene.m_loose,
      scene.m_group});
    REQUIRE(selection.get_nodes() == std::vector{scene.m_group, scene.m_loose});
    REQUIRE(selection.get_primary() == scene.m_loose);
    REQUIRE(selection.get_size() == 2);
    REQUIRE(selection.contains(scene.m_group));
    REQUIRE(!selection.contains(scene.m_rectangle));
    selection.select({scene.m_group, scene.m_loose}, scene.m_group);
    REQUIRE(selection.get_primary() == scene.m_group);
    REQUIRE(count == 3);
    selection.select(NodeId());
    REQUIRE(selection.is_empty());
    REQUIRE(selection.get_primary().is_nil());
    REQUIRE(count == 4);
    selection.clear();
    REQUIRE(count == 4);
  }

  TEST_CASE("add_remove_toggle") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = Selection(document);
    selection.add(scene.m_rectangle);
    selection.add(scene.m_group);
    selection.add(generate_node_id());
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_group});
    REQUIRE(selection.get_primary() == scene.m_group);
    selection.add(scene.m_rectangle);
    REQUIRE(selection.get_primary() == scene.m_rectangle);
    REQUIRE(selection.get_size() == 2);
    selection.remove(scene.m_rectangle);
    REQUIRE(selection.get_nodes() == std::vector{scene.m_group});
    REQUIRE(selection.get_primary() == scene.m_group);
    selection.toggle(scene.m_loose);
    REQUIRE(selection.contains(scene.m_loose));
    REQUIRE(selection.get_primary() == scene.m_loose);
    selection.toggle(scene.m_loose);
    REQUIRE(!selection.contains(scene.m_loose));
    REQUIRE(selection.get_primary() == scene.m_group);
  }

  TEST_CASE("extend") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = Selection(document);
    selection.extend(scene.m_rectangle);
    REQUIRE(selection.get_nodes() == std::vector{scene.m_rectangle});
    selection.extend(scene.m_mask);
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_group, scene.m_mask});
    REQUIRE(selection.get_primary() == scene.m_rectangle);
    selection.extend(scene.m_group);
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_group});
    selection.select(scene.m_instance);
    selection.extend(scene.m_group);
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_group, scene.m_mask, scene.m_instance});
    REQUIRE(selection.get_primary() == scene.m_instance);
    selection.extend(scene.m_ellipse);
    REQUIRE(selection.get_nodes() == std::vector{scene.m_group, scene.m_mask,
      scene.m_instance, scene.m_ellipse});
    REQUIRE(selection.get_primary() == scene.m_ellipse);
    selection.select(scene.m_artboard);
    selection.extend(scene.m_loose);
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_artboard, scene.m_loose});
  }

  TEST_CASE("select_all") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = Selection(document);
    selection.select_all(scene.m_artboard);
    REQUIRE(selection.get_nodes() == std::vector{scene.m_rectangle,
      scene.m_group, scene.m_mask, scene.m_instance});
    REQUIRE(selection.get_primary() == scene.m_instance);
    edit_header(document, scene.m_mask, [] (auto& header) {
      header.m_is_locked = true;
    });
    edit_header(document, scene.m_group, [] (auto& header) {
      header.m_is_visible = false;
    });
    selection.select_all(scene.m_artboard);
    REQUIRE(selection.get_nodes() == std::vector{scene.m_rectangle,
      scene.m_group, scene.m_mask, scene.m_instance});
    selection.select_all(NodeId());
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_artboard, scene.m_loose});
  }

  TEST_CASE("removal") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = Selection(document);
    auto count = 0;
    auto subscription = selection.subscribe([&] { ++count; });
    selection.select({scene.m_rectangle, scene.m_ellipse, scene.m_loose},
      scene.m_ellipse);
    {
      auto transaction = Transaction(document);
      transaction.remove(scene.m_group);
      transaction.commit();
    }
    REQUIRE(count == 2);
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_loose});
    REQUIRE(selection.get_primary() == scene.m_loose);
    REQUIRE(document.undo());
    REQUIRE(count == 2);
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_loose});
    {
      auto transaction = Transaction(document);
      transaction.remove(scene.m_artboard);
      transaction.remove(scene.m_loose);
      transaction.commit();
    }
    REQUIRE(selection.is_empty());
    REQUIRE(selection.get_primary().is_nil());
    REQUIRE(count == 3);
  }
}

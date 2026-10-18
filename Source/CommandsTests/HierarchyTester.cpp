#include <doctest/doctest.h>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("Hierarchy") {
  TEST_CASE("ancestors") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(is_ancestor(document, scene.m_artboard, scene.m_ellipse));
    REQUIRE(is_ancestor(document, scene.m_group, scene.m_ellipse));
    REQUIRE(!is_ancestor(document, scene.m_group, scene.m_rectangle));
    REQUIRE(!is_ancestor(document, scene.m_ellipse, scene.m_ellipse));
    REQUIRE(!is_ancestor(document, NodeId(), scene.m_ellipse));
  }

  TEST_CASE("artboard") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(find_artboard(document, scene.m_ellipse) == scene.m_artboard);
    REQUIRE(find_artboard(document, scene.m_artboard) == scene.m_artboard);
    REQUIRE(find_artboard(document, scene.m_loose).is_nil());
    REQUIRE(find_artboard(document, scene.m_background).is_nil());
  }

  TEST_CASE("index") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(get_index(document, scene.m_group) == 1);
    REQUIRE(get_index(document, scene.m_loose) == 1);
    REQUIRE(get_index(document, scene.m_text) == 1);
  }

  TEST_CASE("top_level") {
    auto document = Document();
    auto scene = build_scene(document);
    REQUIRE(get_top_level(document, {scene.m_ellipse, scene.m_group,
      generate_node_id(), scene.m_rectangle, scene.m_group}) ==
      std::vector{scene.m_group, scene.m_rectangle});
  }

  TEST_CASE("z_order") {
    auto document = Document();
    auto scene = build_scene(document);
    auto nodes = std::vector{scene.m_loose, scene.m_ellipse,
      scene.m_background, scene.m_artboard, scene.m_text, scene.m_rectangle};
    sort_by_z_order(document, nodes);
    REQUIRE(nodes == std::vector{scene.m_artboard, scene.m_rectangle,
      scene.m_ellipse, scene.m_text, scene.m_loose, scene.m_background});
  }
}

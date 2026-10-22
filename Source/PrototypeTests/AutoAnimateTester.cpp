#include <doctest/doctest.h>
#include "Rellu/Prototype/AutoAnimate.hpp"
#include "Stage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const Rect& get_bounds(const Document& document, const NodeId& id) {
    return std::get<RectangleShape>(
      document.get(id).get_shape()->m_geometry).m_bounds;
  }

  struct Trees {
    NodeId m_from;
    NodeId m_to;
    NodeId m_from_box;
    NodeId m_to_box;
    NodeId m_only_from;
    NodeId m_only_to;
    NodeId m_from_group;
    NodeId m_to_group;
    NodeId m_from_inner;
    NodeId m_to_inner;
  };

  Trees build_trees(Document& document) {
    auto trees = Trees();
    auto transaction = Transaction(document);
    trees.m_from = add_board(transaction, "A", Rect(0, 0, 100, 100),
      make_color(0, 0, 0));
    trees.m_to = add_board(transaction, "B", Rect(200, 0, 100, 100),
      make_color(255, 255, 255));
    trees.m_from_box = add_box(transaction, trees.m_from, "Box",
      Rect(0, 0, 10, 10), make_color(255, 0, 0));
    trees.m_only_from = add_box(transaction, trees.m_from, "Gone",
      Rect(50, 50, 10, 10), make_color(0, 0, 0));
    trees.m_from_group = transaction.append(trees.m_from,
      make_stage_header("Group", 0, 0), Group());
    trees.m_from_inner = add_box(transaction, trees.m_from_group, "Box",
      Rect(0, 0, 5, 5), make_color(0, 0, 0));
    trees.m_to_group = transaction.append(trees.m_to,
      make_stage_header("Group", 20, 20), Group());
    trees.m_to_inner = add_box(transaction, trees.m_to_group, "Box",
      Rect(0, 0, 5, 5), make_color(0, 0, 0));
    trees.m_to_box = add_box(transaction, trees.m_to, "Box",
      Rect(40, 20, 30, 10), make_color(0, 0, 255));
    trees.m_only_to = add_box(transaction, trees.m_to, "New",
      Rect(0, 80, 10, 10), make_color(0, 0, 0));
    transaction.commit();
    document.clear_history();
    return trees;
  }
}

TEST_SUITE("AutoAnimate") {
  TEST_CASE("matching") {
    auto document = Document();
    auto trees = build_trees(document);
    auto matches = match_layers(document, trees.m_from, trees.m_to);
    REQUIRE(matches == std::vector<LayerMatch>({
      LayerMatch(trees.m_from, trees.m_to),
      LayerMatch(trees.m_from_group, trees.m_to_group),
      LayerMatch(trees.m_from_box, trees.m_to_box),
      LayerMatch(trees.m_from_inner, trees.m_to_inner)}));
  }

  TEST_CASE("matching_by_kind_and_order") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto from = add_board(transaction, "A", Rect(0, 0, 10, 10),
      make_color(0, 0, 0));
    auto to = add_board(transaction, "B", Rect(20, 0, 10, 10),
      make_color(0, 0, 0));
    auto first = add_box(transaction, from, "Item", Rect(0, 0, 1, 1),
      make_color(0, 0, 0));
    auto second = add_box(transaction, from, "Item", Rect(0, 0, 1, 1),
      make_color(0, 0, 0));
    auto group = transaction.append(
      to, make_stage_header("Item", 0, 0), Group());
    auto third = add_box(transaction, to, "Item", Rect(0, 0, 1, 1),
      make_color(0, 0, 0));
    auto fourth = add_box(transaction, to, "Item", Rect(0, 0, 1, 1),
      make_color(0, 0, 0));
    transaction.commit();
    auto matches = match_layers(document, from, to);
    REQUIRE(matches == std::vector<LayerMatch>({LayerMatch(from, to),
      LayerMatch(first, third), LayerMatch(second, fourth)}));
    REQUIRE(!std::ranges::contains(matches, LayerMatch(first, group)));
  }

  TEST_CASE("interpolation") {
    auto document = Document();
    auto trees = build_trees(document);
    auto animation = AutoAnimation(document, trees.m_from, trees.m_to);
    REQUIRE(animation.get_departing() ==
      std::vector<NodeId>({trees.m_only_from}));
    REQUIRE(document.get(trees.m_only_from).m_parent == trees.m_to);
    REQUIRE(get_bounds(document, trees.m_to_box) == Rect(0, 0, 10, 10));
    REQUIRE(document.get(trees.m_to_box).m_header.m_transform ==
      make_translation(0, 0));
    REQUIRE(document.get(trees.m_only_to).m_header.m_opacity == 0);
    REQUIRE(document.get(trees.m_only_from).m_header.m_opacity == 1);
    animation.apply(0.5);
    REQUIRE(get_bounds(document, trees.m_to_box) == Rect(0, 0, 20, 10));
    REQUIRE(document.get(trees.m_to_box).m_header.m_transform ==
      make_translation(20, 10));
    REQUIRE(document.get(trees.m_to_group).m_header.m_transform ==
      make_translation(10, 10));
    auto& fill = std::get<SolidFill>(
      document.get(trees.m_to_box).get_shape()->m_style.m_fill);
    REQUIRE(fill.m_color.m_red == doctest::Approx(0.5));
    REQUIRE(fill.m_color.m_blue == doctest::Approx(0.5));
    auto& background = std::get<SolidFill>(
      document.get(trees.m_to).get_artboard()->m_background);
    REQUIRE(background.m_color.m_red == doctest::Approx(0.5));
    REQUIRE(document.get(trees.m_only_to).m_header.m_opacity ==
      doctest::Approx(0.5));
    REQUIRE(document.get(trees.m_only_from).m_header.m_opacity ==
      doctest::Approx(0.5));
    REQUIRE(document.get(trees.m_to).m_header.m_transform == Matrix());
    animation.finish();
    REQUIRE(document.get(trees.m_only_from).m_parent == trees.m_from);
    REQUIRE(document.get_children(trees.m_from) == std::vector<NodeId>(
      {trees.m_from_box, trees.m_only_from, trees.m_from_group}));
    REQUIRE(document.get(trees.m_only_from).m_header.m_opacity == 1);
    REQUIRE(get_bounds(document, trees.m_to_box) == Rect(0, 0, 30, 10));
    REQUIRE(document.get(trees.m_only_to).m_header.m_opacity == 1);
    REQUIRE(std::get<SolidFill>(document.get(trees.m_to).get_artboard()->
      m_background).m_color == make_color(255, 255, 255));
    animation.apply(0.5);
    REQUIRE(get_bounds(document, trees.m_to_box) == Rect(0, 0, 30, 10));
  }

  TEST_CASE("dissolve") {
    auto document = Document();
    auto trees = build_trees(document);
    auto animation =
      AutoAnimation(document, trees.m_from, trees.m_to, false);
    REQUIRE(animation.get_matches().size() == 1);
    REQUIRE(animation.get_departing().size() == 3);
    REQUIRE(document.get(trees.m_to_box).m_header.m_opacity == 0);
    animation.apply(0.25);
    REQUIRE(document.get(trees.m_to_box).m_header.m_opacity ==
      doctest::Approx(0.25));
    REQUIRE(document.get(trees.m_from_box).m_header.m_opacity ==
      doctest::Approx(0.75));
    animation.finish();
    REQUIRE(document.get_children(trees.m_from).size() == 3);
  }
  TEST_CASE("auto_animate_matched") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto white = make_color(255, 255, 255);
    auto red = make_color(255, 0, 0);
    auto a = add_board(transaction, "A", Rect(0, 0, 375, 667), white);
    auto b = add_board(transaction, "B", Rect(500, 0, 375, 667), white);
    auto c = add_board(transaction, "C", Rect(1000, 0, 375, 667), white);
    auto r = add_box(transaction, a, "R", Rect(10, 10, 50, 50), red);
    auto other = add_box(transaction, a, "Other", Rect(10, 100, 50, 50), red);
    add_box(transaction, b, "R", Rect(10, 10, 50, 50), red);
    add_box(transaction, c, "Other", Rect(10, 10, 50, 50), red);
    auto unlinked = add_box(transaction, c, "R2", Rect(10, 100, 50, 50), red);
    add_box(transaction, a, "R2", Rect(10, 200, 50, 50), red);
    transaction.commit();
    REQUIRE(!is_auto_animate_matched(document, r));
    {
      auto link = Transaction(document);
      link.add_interaction(Interaction(NodeId(), b, TapTrigger(),
        ArtboardTransition(a,
          Animation(Transition::AUTO_ANIMATE, Easing::EASE_OUT, 0.4),
          false)));
      link.add_interaction(Interaction(NodeId(), c, TapTrigger(),
        ArtboardTransition(b,
          Animation(Transition::DISSOLVE, Easing::EASE_OUT, 0.4), false)));
      link.commit();
    }
    REQUIRE(is_auto_animate_matched(document, r));
    REQUIRE(!is_auto_animate_matched(document, other));
    REQUIRE(!is_auto_animate_matched(document, unlinked));
    {
      auto link = Transaction(document);
      link.add_interaction(Interaction(NodeId(), other, TapTrigger(),
        ArtboardTransition(c,
          Animation(Transition::AUTO_ANIMATE, Easing::EASE_OUT, 0.4),
          false)));
      link.commit();
    }
    REQUIRE(is_auto_animate_matched(document, other));
    REQUIRE(is_auto_animate_matched(document, unlinked));
  }
}

#include <doctest/doctest.h>
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  void require_undoable(Document& document, auto operation) {
    auto before = take_snapshot(document);
    {
      auto transaction = Transaction(document);
      operation(transaction);
      transaction.commit();
    }
    auto after = take_snapshot(document);
    REQUIRE(after != before);
    REQUIRE(document.undo());
    REQUIRE(take_snapshot(document) == before);
    REQUIRE(document.redo());
    REQUIRE(take_snapshot(document) == after);
    REQUIRE(document.undo());
    REQUIRE(take_snapshot(document) == before);
  }
}

TEST_SUITE("Undo") {
  TEST_CASE("nodes") {
    auto document = Document();
    auto sample = build_sample(document);
    require_undoable(document, [&] (auto& transaction) {
      transaction.insert(sample.m_stack, 1, make_header("Inserted"),
        make_rectangle_shape(1, 1));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.insert_root(make_header("Root"), Group());
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove(sample.m_stack);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove(sample.m_artboard);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.move(sample.m_rectangle, sample.m_stack, 1);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.move(sample.m_stack, sample.m_master, 1);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.move(sample.m_artboard, NodeId(), 1);
    });
    require_undoable(document, [&] (auto& transaction) {
      auto header = document.get(sample.m_text).m_header;
      header.m_opacity = 0.25;
      transaction.replace_header(header);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.replace_payload(sample.m_text, make_text(u"Changed"));
    });
  }

  TEST_CASE("resources") {
    auto document = Document();
    auto sample = build_sample(document);
    require_undoable(document, [&] (auto& transaction) {
      auto component = Component();
      component.m_master =
        transaction.insert_root(make_header("Master"), Group());
      transaction.add_component(component);
    });
    require_undoable(document, [&] (auto& transaction) {
      auto component = document.get_resources().m_components.at(
        sample.m_component);
      component.m_states.clear();
      transaction.replace_component(component);
      transaction.remove(sample.m_state_tree);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove_component(sample.m_component);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.add_image(ImageResource(NodeId(), "image/png"));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove_image(sample.m_image);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.add_swatch(ColorSwatch(NodeId(), "Added"));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.replace_swatch(ColorSwatch(sample.m_swatch, "Replaced"));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove_swatch(sample.m_swatch);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.add_character_style(CharacterStyle(NodeId(), "Added"));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.replace_character_style(
        CharacterStyle(sample.m_character_style, "Replaced"));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove_character_style(sample.m_character_style);
    });
  }

  TEST_CASE("prototype") {
    auto document = Document();
    auto sample = build_sample(document);
    require_undoable(document, [&] (auto& transaction) {
      transaction.add_interaction(Interaction(NodeId(), sample.m_text,
        KeyTrigger("Ctrl+K"), Overlay(sample.m_artboard, Point(10, 10))));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.replace_interaction(Interaction(sample.m_interaction,
        sample.m_rectangle, DragTrigger(), VideoPlayback(sample.m_path)));
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove_interaction(sample.m_interaction);
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.set_home_artboard(NodeId());
    });
    require_undoable(document, [&] (auto& transaction) {
      transaction.set_flows(std::vector<Flow>());
    });
  }

  TEST_CASE("nested_structure") {
    auto document = Document();
    auto sample = build_sample(document);
    require_undoable(document, [&] (auto& transaction) {
      auto outer = transaction.insert(
        sample.m_artboard, 0, make_header("Outer"), Group());
      auto inner = transaction.append(outer, make_header("Inner"), Group());
      transaction.move(sample.m_stack, inner, 0);
      transaction.move(sample.m_rectangle, outer, 0);
      transaction.move(sample.m_compound, sample.m_stack, 1);
      transaction.remove(sample.m_ellipse);
      transaction.remove(sample.m_mask);
      auto header = document.get(sample.m_rectangle).m_header;
      header.m_name = "Moved";
      transaction.replace_header(header);
      transaction.move(inner, NodeId(), 0);
      transaction.append(sample.m_compound, make_header("Third"),
        make_rectangle_shape(2, 2));
    });
  }

  TEST_CASE("instance_states") {
    auto document = Document();
    auto sample = build_sample(document);
    {
      auto transaction = Transaction(document);
      auto tree = transaction.insert_root(make_header("Hover"), Group());
      transaction.append(
        tree, make_header("Shape"), make_rectangle_shape(1, 1));
      auto group = *document.get(sample.m_instance).get_group();
      group.m_component_instance->m_states.push_back(
        ComponentState(sample.m_state, "Hover", tree));
      transaction.replace_payload(sample.m_instance, group);
      transaction.commit();
    }
    require_undoable(document, [&] (auto& transaction) {
      transaction.remove(sample.m_instance);
    });
  }

  TEST_CASE("history") {
    auto document = Document();
    auto empty = take_snapshot(document);
    auto sample = build_sample(document);
    auto built = take_snapshot(document);
    {
      auto transaction = Transaction(document);
      transaction.remove(sample.m_stack);
      transaction.commit();
    }
    auto removed = take_snapshot(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(sample.m_text).m_header;
      header.m_name = "Edited";
      transaction.replace_header(header);
      transaction.commit();
    }
    auto edited = take_snapshot(document);
    REQUIRE(document.undo());
    REQUIRE(take_snapshot(document) == removed);
    REQUIRE(document.undo());
    REQUIRE(take_snapshot(document) == built);
    REQUIRE(document.undo());
    REQUIRE(take_snapshot(document) == empty);
    REQUIRE(!document.undo());
    REQUIRE(document.redo());
    REQUIRE(document.redo());
    REQUIRE(document.redo());
    REQUIRE(take_snapshot(document) == edited);
    REQUIRE(!document.redo());
    REQUIRE(document.undo());
    REQUIRE(document.can_redo());
    {
      auto transaction = Transaction(document);
      transaction.remove(sample.m_text);
      transaction.commit();
    }
    REQUIRE(!document.can_redo());
    document.clear_history();
    REQUIRE(!document.can_undo());
    REQUIRE(!document.can_redo());
  }

  TEST_CASE("published") {
    auto document = Document();
    auto sample = build_sample(document);
    auto changes = std::vector<ChangeSet>();
    auto subscription = document.subscribe([&] (const auto& change) {
      changes.push_back(change);
    });
    {
      auto transaction = Transaction(document);
      transaction.remove(sample.m_stack);
      transaction.move(sample.m_text, sample.m_mask, 0);
      transaction.commit();
    }
    document.undo();
    document.redo();
    REQUIRE(changes.size() == 3);
    REQUIRE(changes[1].m_inserted == changes[0].m_removed);
    REQUIRE(changes[1].m_removed == changes[0].m_inserted);
    REQUIRE(changes[1].m_moved == changes[0].m_moved);
    REQUIRE(changes[1].m_modified == changes[0].m_modified);
    REQUIRE(changes[2] == changes[0]);
  }
}

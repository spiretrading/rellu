#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  void require_invalid(Document& document, auto prepare, auto operation) {
    auto transaction = Transaction(document);
    prepare(transaction);
    auto snapshot = take_snapshot(document);
    REQUIRE_THROWS_AS(operation(transaction), std::invalid_argument);
    REQUIRE(take_snapshot(document) == snapshot);
  }

  void require_invalid(Document& document, auto operation) {
    require_invalid(document, [] (auto&) {}, operation);
  }

  Shape make_compound_shape() {
    auto shape = Shape();
    shape.m_geometry = CompoundShape(CompoundOperation::UNION,
      Shared(make_rectangle(Rect(0, 0, 1, 1))));
    return shape;
  }
}

TEST_SUITE("Transaction") {
  TEST_CASE("insert") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto first = transaction.insert(NodeId(), 0, make_header("First"),
      make_rectangle_shape(1, 1));
    auto second = transaction.insert(NodeId(), 0, NodeHeader(), Group());
    auto third = transaction.insert(NodeId(), 1, make_header("Third"),
      make_rectangle_shape(1, 1));
    REQUIRE(!second.is_nil());
    REQUIRE(document.get_canvas() ==
      std::vector<NodeId>({second, third, first}));
    auto child = transaction.insert(second, 0, make_header("Child"),
      make_rectangle_shape(1, 1));
    REQUIRE(document.get(child).m_parent == second);
    REQUIRE(document.get_children(second) == std::vector<NodeId>({child}));
    REQUIRE(transaction.commit());
    REQUIRE(document.get_nodes().get_size() == 4);
    REQUIRE(document.can_undo());
  }

  TEST_CASE("append") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(NodeId(), make_header("Group"), Group());
    auto first = transaction.append(group, make_header("First"),
      make_rectangle_shape(1, 1));
    auto second = transaction.append(group, make_header("Second"),
      make_rectangle_shape(1, 1));
    REQUIRE(document.get_children(group) ==
      std::vector<NodeId>({first, second}));
    transaction.commit();
  }

  TEST_CASE("insert_root") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto root = transaction.insert_root(make_header("Master"), Group());
    REQUIRE(document.find(root));
    REQUIRE(document.get_canvas().empty());
    REQUIRE(!document.is_on_canvas(root));
    transaction.commit();
  }

  TEST_CASE("remove") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    transaction.remove(sample.m_stack);
    REQUIRE(!document.find(sample.m_stack));
    REQUIRE(!document.find(sample.m_rectangle));
    REQUIRE(!document.find(sample.m_ellipse));
    REQUIRE(document.get_children(sample.m_artboard).front() ==
      sample.m_repeat_grid);
    REQUIRE(document.get_prototype().m_interactions.empty());
    REQUIRE(document.get_prototype().m_home_artboard == sample.m_artboard);
    transaction.remove(sample.m_artboard);
    REQUIRE(document.get_canvas() == std::vector<NodeId>({sample.m_loose}));
    REQUIRE(document.get_prototype().m_home_artboard.is_nil());
    REQUIRE(document.get_prototype().m_flows.empty());
    REQUIRE(document.get_nodes().get_size() == 4);
    transaction.commit();
  }

  TEST_CASE("instance_states") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    auto tree = transaction.insert_root(make_header("Hover"), Group());
    auto shape = transaction.append(
      tree, make_header("Shape"), make_rectangle_shape(1, 1));
    auto group = *document.get(sample.m_instance).get_group();
    group.m_component_instance->m_states.push_back(
      ComponentState(sample.m_state, "Hover", tree));
    transaction.replace_payload(sample.m_instance, group);
    transaction.remove(sample.m_artboard);
    REQUIRE(!document.find(tree));
    REQUIRE(!document.find(shape));
    REQUIRE(document.find(sample.m_state_tree));
    transaction.commit();
  }

  TEST_CASE("move") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    transaction.move(sample.m_rectangle, sample.m_stack, 1);
    REQUIRE(document.get_children(sample.m_stack) ==
      std::vector<NodeId>({sample.m_ellipse, sample.m_rectangle}));
    transaction.move(sample.m_rectangle, sample.m_mask, 0);
    REQUIRE(document.get_children(sample.m_stack) ==
      std::vector<NodeId>({sample.m_ellipse}));
    REQUIRE(document.get_children(sample.m_mask) ==
      std::vector<NodeId>({sample.m_rectangle, sample.m_path}));
    REQUIRE(document.get(sample.m_rectangle).m_parent == sample.m_mask);
    transaction.move(sample.m_mask, NodeId(), 0);
    REQUIRE(document.get_canvas() == std::vector<NodeId>(
      {sample.m_mask, sample.m_artboard, sample.m_loose}));
    REQUIRE(document.get(sample.m_mask).m_parent.is_nil());
    transaction.move(sample.m_artboard, NodeId(), 2);
    REQUIRE(document.get_canvas() == std::vector<NodeId>(
      {sample.m_mask, sample.m_loose, sample.m_artboard}));
    transaction.move(sample.m_loose, sample.m_master, 0);
    REQUIRE(document.get(sample.m_loose).m_parent == sample.m_master);
    transaction.commit();
  }

  TEST_CASE("replace_header") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    auto header = document.get(sample.m_rectangle).m_header;
    header.m_name = "Renamed";
    header.m_transform = make_translation(5, 6);
    header.m_is_visible = false;
    transaction.replace_header(header);
    REQUIRE(document.get(sample.m_rectangle).m_header == header);
    transaction.commit();
  }

  TEST_CASE("replace_payload") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    auto shape = make_rectangle_shape(30, 40);
    shape.m_style.m_effects.push_back(Blur(4));
    transaction.replace_payload(sample.m_rectangle, shape);
    REQUIRE(*document.get(sample.m_rectangle).get_shape() == shape);
    auto group = *document.get(sample.m_stack).get_group();
    group.m_stack = std::nullopt;
    transaction.replace_payload(sample.m_stack, group);
    REQUIRE(!document.get(sample.m_stack).get_group()->m_stack);
    transaction.commit();
  }

  TEST_CASE("stored_layout") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(u"Hello");
    auto layout = StoredLayout();
    layout.m_paragraphs.emplace_back();
    layout.m_paragraphs.back().m_lines.emplace_back();
    layout.m_paragraphs.back().m_lines.back().m_runs.push_back(
      StoredRun(0, 5, Point(0, 14)));
    text.m_layout = Shared(layout);
    auto id = transaction.append(NodeId(), make_header("Text"), text);
    auto recolored = text;
    recolored.m_style.m_fill = SolidFill(make_color(0, 255, 0));
    transaction.replace_payload(id, recolored);
    REQUIRE(document.get(id).get_text()->m_layout);
    auto restyled = recolored;
    restyled.m_runs[0].m_style.m_color = make_color(0, 0, 255);
    restyled.m_runs[0].m_style.m_is_underlined = true;
    restyled.m_runs[0].m_character_style = generate_node_id();
    transaction.replace_payload(id, restyled);
    REQUIRE(document.get(id).get_text()->m_layout == text.m_layout);
    auto respaced = restyled;
    respaced.m_runs[0].m_style.m_letter_spacing = 50;
    transaction.replace_payload(id, respaced);
    REQUIRE(!document.get(id).get_text()->m_layout);
    auto relaid = make_text(u"Goodbye");
    auto replacement = layout;
    replacement.m_paragraphs[0].m_lines[0].m_runs[0].m_to = 7;
    relaid.m_layout = Shared(replacement);
    transaction.replace_payload(id, relaid);
    REQUIRE(document.get(id).get_text()->m_layout == relaid.m_layout);
    auto edited = relaid;
    edited.m_frame = AutoHeightFrame(100);
    transaction.replace_payload(id, edited);
    REQUIRE(!document.get(id).get_text()->m_layout);
    transaction.commit();
  }

  TEST_CASE("stored_layout_colors") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(u"Hello World");
    text.m_runs[0].m_length = 5;
    text.m_runs.push_back(TextRun(6, text.m_base_style));
    auto layout = StoredLayout();
    layout.m_paragraphs.emplace_back();
    layout.m_paragraphs.back().m_lines.emplace_back();
    auto& runs = layout.m_paragraphs.back().m_lines.back().m_runs;
    runs.push_back(
      StoredRun(0, 5, Point(0, 14), std::nullopt, make_color(1, 2, 3)));
    runs.push_back(
      StoredRun(5, 11, Point(30, 14), std::nullopt, make_color(4, 5, 6)));
    text.m_layout = Shared(layout);
    auto id = transaction.append(NodeId(), make_header("Text"), text);
    auto recolored = text;
    recolored.m_runs[1].m_style.m_color = make_color(0, 255, 0);
    transaction.replace_payload(id, recolored);
    auto& stored = **document.get(id).get_text()->m_layout;
    auto& line = stored.m_paragraphs[0].m_lines[0];
    REQUIRE(line.m_runs[0] == runs[0]);
    REQUIRE(line.m_runs[1] == StoredRun(5, 11, Point(30, 14)));
    transaction.commit();
  }

  TEST_CASE("components") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    auto master = transaction.insert_root(make_header("Card"), Group());
    auto component = Component();
    component.m_name = "Card";
    component.m_master = master;
    auto id = transaction.add_component(component);
    REQUIRE(!id.is_nil());
    REQUIRE(document.get_resources().m_components.at(id).m_name == "Card");
    auto state_tree = transaction.insert_root(make_header("Pressed"), Group());
    component = document.get_resources().m_components.at(id);
    component.m_states.push_back(
      ComponentState(generate_node_id(), "Pressed", state_tree));
    transaction.replace_component(component);
    REQUIRE(document.get_resources().m_components.at(id).m_states.size() ==
      1);
    transaction.remove_component(sample.m_component);
    REQUIRE(!document.get_resources().m_components.contains(
      sample.m_component));
    REQUIRE(!document.find(sample.m_master));
    REQUIRE(!document.find(sample.m_master_shape));
    REQUIRE(!document.find(sample.m_state_tree));
    transaction.commit();
  }

  TEST_CASE("resources") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    auto image = transaction.add_image(ImageResource(NodeId(), "image/jpeg",
      Shared(std::vector<std::byte>(2))));
    REQUIRE(document.get_resources().m_images.contains(image));
    transaction.remove_image(sample.m_image);
    REQUIRE(!document.get_resources().m_images.contains(sample.m_image));
    auto swatch = transaction.add_swatch(
      ColorSwatch(NodeId(), "Secondary", make_color(0, 255, 0)));
    REQUIRE(document.get_resources().m_swatches.back().m_id == swatch);
    transaction.replace_swatch(
      ColorSwatch(sample.m_swatch, "Renamed", make_color(1, 2, 3)));
    REQUIRE(document.get_resources().m_swatches.front().m_name == "Renamed");
    transaction.remove_swatch(swatch);
    REQUIRE(document.get_resources().m_swatches.size() == 1);
    auto style = transaction.add_character_style(
      CharacterStyle(NodeId(), "Heading"));
    REQUIRE(document.get_resources().m_character_styles.size() == 2);
    transaction.replace_character_style(
      CharacterStyle(sample.m_character_style, "Caption"));
    REQUIRE(document.get_resources().m_character_styles.front().m_name ==
      "Caption");
    transaction.remove_character_style(style);
    REQUIRE(document.get_resources().m_character_styles.size() == 1);
    transaction.commit();
  }

  TEST_CASE("prototype") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    auto interaction = transaction.add_interaction(Interaction(NodeId(),
      sample.m_text, HoverTrigger(), StateTransition(sample.m_state)));
    REQUIRE(document.get_prototype().m_interactions.size() == 2);
    transaction.replace_interaction(Interaction(sample.m_interaction,
      sample.m_ellipse, TimeTrigger(2), PreviousArtboard()));
    REQUIRE(document.get_prototype().m_interactions.front().m_owner ==
      sample.m_ellipse);
    transaction.remove_interaction(interaction);
    REQUIRE(document.get_prototype().m_interactions.size() == 1);
    transaction.set_home_artboard(NodeId());
    REQUIRE(document.get_prototype().m_home_artboard.is_nil());
    transaction.set_flows(std::vector<Flow>());
    REQUIRE(document.get_prototype().m_flows.empty());
    transaction.commit();
  }

  TEST_CASE("invalid_nodes") {
    auto document = Document();
    auto sample = build_sample(document);
    auto unknown = generate_node_id();
    require_invalid(document, [&] (auto& transaction) {
      auto header = make_header("Duplicate");
      header.m_id = sample.m_rectangle;
      transaction.append(sample.m_stack, header, Group());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.append(unknown, make_header("Orphan"), Group());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.insert(sample.m_stack, 3, make_header("Far"), Group());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.append(sample.m_stack, make_header("Nested"), Artboard());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.append(sample.m_text, make_header("Child"), Group());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.append(sample.m_rectangle, make_header("Child"),
        make_rectangle_shape(1, 1));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.append(sample.m_compound, make_header("Child"), Group());
    });
    auto reference = NodeId();
    require_invalid(document, [&] (auto& transaction) {
      reference = transaction.append(sample.m_instance,
        make_header("Reference"), Reference(sample.m_master_shape));
    }, [&] (auto& transaction) {
      transaction.append(reference, make_header("Child"), Group());
    });
    require_invalid(document, [&] (auto& transaction) {
      auto text = make_text(u"Hello");
      text.m_runs[0].m_length = 3;
      transaction.append(sample.m_artboard, make_header("Text"), text);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.insert_root(make_header("Root"), Artboard());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove(unknown);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove(sample.m_master);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove(sample.m_state_tree);
    });
  }

  TEST_CASE("invalid_moves") {
    auto document = Document();
    auto sample = build_sample(document);
    auto unknown = generate_node_id();
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(unknown, sample.m_stack, 0);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_rectangle, unknown, 0);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_stack, sample.m_stack, 0);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_artboard, sample.m_stack, 0);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_mask, sample.m_mask, 0);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_rectangle, sample.m_stack, 2);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_rectangle, sample.m_mask, 2);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_stack, sample.m_text, 0);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.move(sample.m_master, NodeId(), 0);
    });
    auto group = NodeId();
    require_invalid(document, [&] (auto& transaction) {
      group = transaction.append(sample.m_stack, make_header("Inner"), Group());
    }, [&] (auto& transaction) {
      transaction.move(sample.m_artboard, group, 0);
    });
  }

  TEST_CASE("cycle") {
    auto document = Document();
    auto sample = build_sample(document);
    auto transaction = Transaction(document);
    auto outer = transaction.append(
      sample.m_artboard, make_header("Outer"), Group());
    auto inner = transaction.append(outer, make_header("Inner"), Group());
    auto snapshot = take_snapshot(document);
    REQUIRE_THROWS_AS(transaction.move(outer, inner, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(transaction.move(sample.m_artboard, inner, 0),
      std::invalid_argument);
    REQUIRE(take_snapshot(document) == snapshot);
    transaction.commit();
  }

  TEST_CASE("invalid_replacements") {
    auto document = Document();
    auto sample = build_sample(document);
    auto unknown = generate_node_id();
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_header(make_header("Unknown"));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_payload(unknown, Group());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_payload(sample.m_rectangle, Group());
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_payload(
        sample.m_compound, make_rectangle_shape(1, 1));
    });
    require_invalid(document, [&] (auto& transaction) {
      auto text = make_text(u"Hello");
      text.m_runs[0].m_length = 6;
      transaction.replace_payload(sample.m_text, text);
    });
  }

  TEST_CASE("invalid_resources") {
    auto document = Document();
    auto sample = build_sample(document);
    auto unknown = generate_node_id();
    auto make_component = [] (const NodeId& master) {
      auto component = Component();
      component.m_master = master;
      return component;
    };
    auto master = NodeId();
    auto prepare_master = [&] (auto& transaction) {
      master = transaction.insert_root(make_header("Master"), Group());
    };
    require_invalid(document, prepare_master, [&] (auto& transaction) {
      auto component = make_component(master);
      component.m_id = sample.m_component;
      transaction.add_component(component);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_component(make_component(unknown));
    });
    require_invalid(document, [&] (auto& transaction) {
      master = transaction.insert_root(
        make_header("Master"), make_rectangle_shape(1, 1));
    }, [&] (auto& transaction) {
      transaction.add_component(make_component(master));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_component(make_component(sample.m_stack));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_component(make_component(sample.m_loose));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_component(make_component(sample.m_master));
    });
    require_invalid(document, prepare_master, [&] (auto& transaction) {
      auto component = make_component(master);
      component.m_states.push_back(
        ComponentState(generate_node_id(), "State", sample.m_state_tree));
      transaction.add_component(component);
    });
    require_invalid(document, prepare_master, [&] (auto& transaction) {
      auto component = make_component(master);
      component.m_states.push_back(
        ComponentState(generate_node_id(), "State", master));
      transaction.add_component(component);
    });
    auto first = NodeId();
    auto second = NodeId();
    require_invalid(document, [&] (auto& transaction) {
      master = transaction.insert_root(make_header("Master"), Group());
      first = transaction.insert_root(make_header("First"), Group());
      second = transaction.insert_root(make_header("Second"), Group());
    }, [&] (auto& transaction) {
      auto component = make_component(master);
      auto state = generate_node_id();
      component.m_states.push_back(ComponentState(state, "First", first));
      component.m_states.push_back(ComponentState(state, "Second", second));
      transaction.add_component(component);
    });
    require_invalid(document, [&] (auto& transaction) {
      auto component = make_component(sample.m_master);
      component.m_id = unknown;
      transaction.replace_component(component);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove_component(unknown);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_image(ImageResource(sample.m_image, "image/png"));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove_image(unknown);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_swatch(ColorSwatch(sample.m_swatch));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_swatch(ColorSwatch(unknown));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove_swatch(unknown);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_character_style(
        CharacterStyle(sample.m_character_style));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_character_style(CharacterStyle(unknown));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove_character_style(unknown);
    });
  }

  TEST_CASE("invalid_prototype") {
    auto document = Document();
    auto sample = build_sample(document);
    auto unknown = generate_node_id();
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_interaction(Interaction(sample.m_interaction,
        sample.m_rectangle, TapTrigger(), PreviousArtboard()));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.add_interaction(
        Interaction(NodeId(), unknown, TapTrigger(), PreviousArtboard()));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_interaction(
        Interaction(unknown, sample.m_rectangle, TapTrigger(),
          PreviousArtboard()));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.replace_interaction(Interaction(
        sample.m_interaction, unknown, TapTrigger(), PreviousArtboard()));
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.remove_interaction(unknown);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.set_home_artboard(sample.m_stack);
    });
    require_invalid(document, [&] (auto& transaction) {
      transaction.set_flows({Flow(sample.m_artboard, "Main"),
        Flow(unknown, "Broken")});
    });
  }

  TEST_CASE("rollback") {
    auto document = Document();
    auto sample = build_sample(document);
    auto snapshot = take_snapshot(document);
    auto count = 0;
    auto subscription = document.subscribe([&] (const auto&) {
      ++count;
    });
    {
      auto transaction = Transaction(document);
      transaction.remove(sample.m_stack);
      transaction.move(sample.m_text, NodeId(), 0);
      transaction.append(sample.m_artboard, make_header("New"), Group());
      transaction.remove_component(sample.m_component);
      transaction.remove_swatch(sample.m_swatch);
      transaction.set_home_artboard(NodeId());
      REQUIRE(take_snapshot(document) != snapshot);
    }
    REQUIRE(take_snapshot(document) == snapshot);
    REQUIRE(count == 0);
    REQUIRE(document.can_undo());
    document.undo();
    REQUIRE(document.get_nodes().get_size() == 0);
  }

  TEST_CASE("exclusivity") {
    auto document = Document();
    {
      auto transaction = Transaction(document);
      REQUIRE_THROWS_AS(Transaction(document).commit(), std::logic_error);
      REQUIRE_THROWS_AS(document.undo(), std::logic_error);
      REQUIRE_THROWS_AS(document.redo(), std::logic_error);
      transaction.append(NodeId(), make_header("Group"), Group());
      transaction.commit();
      REQUIRE_THROWS_AS(transaction.append(
        NodeId(), make_header("Late"), Group()), std::logic_error);
      REQUIRE_THROWS_AS(transaction.commit(), std::logic_error);
      auto next = Transaction(document);
    }
    REQUIRE(document.get_canvas().size() == 1);
  }

  TEST_CASE("empty") {
    auto document = Document();
    auto count = 0;
    auto subscription = document.subscribe([&] (const auto&) {
      ++count;
    });
    auto transaction = Transaction(document);
    REQUIRE(!transaction.commit());
    REQUIRE(count == 0);
    REQUIRE(!document.can_undo());
  }

  TEST_CASE("compound_operands") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto compound =
      transaction.append(NodeId(), make_header("Compound"),
        make_compound_shape());
    auto operand = transaction.append(
      compound, make_header("Operand"), make_compound_shape());
    transaction.append(
      operand, make_header("Nested"), make_rectangle_shape(1, 1));
    REQUIRE(document.get_children(compound) ==
      std::vector<NodeId>({operand}));
    transaction.commit();
  }
}

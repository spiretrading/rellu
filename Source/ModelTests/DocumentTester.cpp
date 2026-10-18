#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Model/Document.hpp"
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("Document") {
  TEST_CASE("empty") {
    auto document = Document();
    REQUIRE(document.get_nodes().get_size() == 0);
    REQUIRE(document.get_canvas().empty());
    REQUIRE(document.get_resources() == Resources());
    REQUIRE(document.get_prototype() == Prototype());
    REQUIRE(!document.can_undo());
    REQUIRE(!document.can_redo());
    REQUIRE(!document.undo());
    REQUIRE(!document.redo());
  }

  TEST_CASE("structure") {
    auto document = Document();
    auto sample = build_sample(document);
    REQUIRE(document.get_nodes().get_size() == 20);
    REQUIRE(document.get_canvas() ==
      std::vector<NodeId>({sample.m_artboard, sample.m_loose}));
    REQUIRE(document.get_children(NodeId()) == document.get_canvas());
    REQUIRE(document.get_children(sample.m_artboard) ==
      std::vector<NodeId>({sample.m_stack, sample.m_repeat_grid,
        sample.m_scroll_group, sample.m_mask, sample.m_compound,
        sample.m_text, sample.m_instance}));
    REQUIRE(document.get_children(sample.m_stack) ==
      std::vector<NodeId>({sample.m_rectangle, sample.m_ellipse}));
    REQUIRE(document.get_children(sample.m_compound) == std::vector<NodeId>(
      {sample.m_first_operand, sample.m_second_operand}));
    REQUIRE(document.get(sample.m_rectangle).m_parent == sample.m_stack);
    REQUIRE(document.get(sample.m_artboard).m_parent.is_nil());
    REQUIRE(document.get(sample.m_master).m_parent.is_nil());
    REQUIRE(document.is_on_canvas(sample.m_artboard));
    REQUIRE(document.is_on_canvas(sample.m_loose));
    REQUIRE(!document.is_on_canvas(sample.m_master));
    REQUIRE(!document.is_on_canvas(sample.m_rectangle));
    REQUIRE(!document.find(generate_node_id()));
    REQUIRE_THROWS_AS(document.get(generate_node_id()), std::out_of_range);
    REQUIRE_THROWS_AS(
      document.get_children(generate_node_id()), std::out_of_range);
  }

  TEST_CASE("kinds") {
    auto document = Document();
    auto sample = build_sample(document);
    auto& artboard = document.get(sample.m_artboard);
    REQUIRE(artboard.get_kind() == NodeKind::ARTBOARD);
    REQUIRE(artboard.get_artboard());
    REQUIRE(!artboard.get_group());
    REQUIRE(artboard.get_artboard()->m_frame == Rect(0, 0, 375, 812));
    REQUIRE(artboard.get_artboard()->m_viewport_height == 600);
    REQUIRE(artboard.m_header.m_name == "Home");
    auto& stack = document.get(sample.m_stack);
    REQUIRE(stack.get_kind() == NodeKind::GROUP);
    REQUIRE(stack.get_group()->m_stack);
    REQUIRE(stack.get_group()->m_padding);
    REQUIRE(!stack.get_group()->m_mask);
    REQUIRE(document.get(sample.m_repeat_grid).get_group()->m_repeat_grid->
      m_column_count == 2);
    REQUIRE(document.get(sample.m_scroll_group).get_group()->
      m_scroll_group->m_direction == ScrollDirection::VERTICAL);
    REQUIRE(document.get(sample.m_mask).get_group()->m_mask->m_shapes.size() ==
      1);
    auto& instance = document.get(sample.m_instance).get_group()->
      m_component_instance;
    REQUIRE(instance);
    REQUIRE(instance->m_component == sample.m_component);
    auto& rectangle = document.get(sample.m_rectangle);
    REQUIRE(rectangle.get_kind() == NodeKind::SHAPE);
    REQUIRE(!rectangle.get_text());
    REQUIRE(std::holds_alternative<RectangleShape>(
      rectangle.get_shape()->m_geometry));
    REQUIRE(std::holds_alternative<EllipseShape>(
      document.get(sample.m_ellipse).get_shape()->m_geometry));
    REQUIRE(std::holds_alternative<LineShape>(
      document.get(sample.m_line).get_shape()->m_geometry));
    REQUIRE(std::holds_alternative<PolygonShape>(
      document.get(sample.m_polygon).get_shape()->m_geometry));
    REQUIRE(std::holds_alternative<PathShape>(
      document.get(sample.m_path).get_shape()->m_geometry));
    REQUIRE(std::holds_alternative<CompoundShape>(
      document.get(sample.m_compound).get_shape()->m_geometry));
    auto& text = document.get(sample.m_text);
    REQUIRE(text.get_kind() == NodeKind::TEXT);
    REQUIRE(*text.get_text()->m_content == u"Hello");
    auto& reference = document.get(sample.m_reference);
    REQUIRE(reference.get_kind() == NodeKind::REFERENCE);
    REQUIRE(reference.get_reference()->m_source == sample.m_master_shape);
    REQUIRE(!reference.get_shape());
  }

  TEST_CASE("resources") {
    auto document = Document();
    auto sample = build_sample(document);
    auto& resources = document.get_resources();
    REQUIRE(resources.m_components.size() == 1);
    auto& component = resources.m_components.at(sample.m_component);
    REQUIRE(component.m_master == sample.m_master);
    REQUIRE(component.m_states.size() == 1);
    REQUIRE(component.m_states[0].m_tree == sample.m_state_tree);
    REQUIRE(resources.m_images.at(sample.m_image).m_data->size() == 4);
    REQUIRE(resources.m_swatches.size() == 1);
    REQUIRE(resources.m_swatches[0].m_id == sample.m_swatch);
    REQUIRE(resources.m_character_styles.size() == 1);
    REQUIRE(resources.m_character_styles[0].m_id == sample.m_character_style);
    auto& prototype = document.get_prototype();
    REQUIRE(prototype.m_interactions.size() == 1);
    REQUIRE(prototype.m_interactions[0].m_owner == sample.m_rectangle);
    REQUIRE(prototype.m_home_artboard == sample.m_artboard);
    REQUIRE(prototype.m_flows == std::vector<Flow>(
      {Flow(sample.m_artboard, "Main")}));
  }
}

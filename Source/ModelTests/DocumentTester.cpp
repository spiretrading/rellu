#include <limits>
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

  TEST_CASE("not_a_number") {
    constexpr auto NAN_VALUE = std::numeric_limits<double>::quiet_NaN();
    auto document = Document();
    auto transaction = Transaction(document);
    auto header = make_header("Artboard");
    header.m_opacity = NAN_VALUE;
    header.m_transform = Matrix(NAN_VALUE, 0, 0, 1, NAN_VALUE, 0);
    auto artboard = Artboard();
    artboard.m_frame = Rect(NAN_VALUE, 0, 10, 10);
    artboard.m_viewport_height = NAN_VALUE;
    artboard.m_layout_grid = LayoutGrid();
    artboard.m_layout_grid->m_gutter = NAN_VALUE;
    artboard.m_layout_grid->m_margins.m_left = NAN_VALUE;
    artboard.m_guides.push_back(Guide(Orientation::VERTICAL, NAN_VALUE));
    artboard.m_background = SolidFill(Color(NAN_VALUE, 0, 0, 1));
    auto artboard_id = transaction.append(NodeId(), header, artboard);
    auto group = Group();
    group.m_repeat_grid = RepeatGrid(Size(NAN_VALUE, 1), NAN_VALUE);
    group.m_stack = Stack(Orientation::HORIZONTAL);
    group.m_stack->m_spacing.emplace(generate_node_id(), NAN_VALUE);
    auto group_id =
      transaction.append(artboard_id, make_header("Group"), group);
    auto shape = Shape();
    shape.m_geometry = PolygonShape(5, Size(1, 1), NAN_VALUE,
      std::vector<Point>({Point(NAN_VALUE, 0)}));
    shape.m_style.m_stroke = Stroke(Color(0, 0, 0, 1), NAN_VALUE);
    shape.m_style.m_stroke->m_dashes.push_back(NAN_VALUE);
    shape.m_style.m_effects.push_back(
      DropShadow(Point(0, 0), NAN_VALUE));
    shape.m_style.m_effects.push_back(Blur(NAN_VALUE));
    auto gradient = GradientFill();
    gradient.m_stops.push_back(GradientStop(NAN_VALUE));
    gradient.m_geometry = RadialGradient(Point(0, 0), NAN_VALUE);
    shape.m_style.m_fill = gradient;
    transaction.append(group_id, make_header("Polygon"), shape);
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(0, 0), NAN_VALUE, NAN_VALUE);
    auto image = ImageFill();
    image.m_scale = NAN_VALUE;
    ellipse.m_style.m_fill = image;
    transaction.append(group_id, make_header("Ellipse"), ellipse);
    auto text = make_text(u"Hello");
    text.m_frame = AutoHeightFrame(NAN_VALUE);
    text.m_runs[0].m_style.m_font.m_size = NAN_VALUE;
    text.m_runs[0].m_style.m_letter_spacing = NAN_VALUE;
    text.m_paragraph_style.m_line_height = NAN_VALUE;
    text.m_paragraph_style.m_paragraph_spacing = NAN_VALUE;
    auto text_id = transaction.append(artboard_id, make_header("Text"), text);
    auto style = CharacterStyle(NodeId(), "Style", text.m_runs[0].m_style);
    style.m_line_height = NAN_VALUE;
    transaction.add_character_style(style);
    auto master = transaction.insert_root(make_header("Master"), Group());
    auto component = Component();
    component.m_master = master;
    component.m_link = ComponentLink("reference", "source", NAN_VALUE);
    transaction.add_component(component);
    transaction.add_interaction(Interaction(NodeId(), text_id,
      TimeTrigger(NAN_VALUE), Overlay(artboard_id, Point(NAN_VALUE, 0),
        Animation(Transition::DISSOLVE, Easing::LINEAR, NAN_VALUE))));
    REQUIRE(transaction.commit());
    auto snapshot = take_snapshot(document);
    REQUIRE(snapshot == take_snapshot(document));
    for(auto& entry : snapshot.m_nodes) {
      REQUIRE(entry.second == Node(entry.second));
    }
    auto replacement = Transaction(document);
    replacement.replace_header(document.get(artboard_id).m_header);
    replacement.replace_payload(text_id, *document.get(text_id).get_text());
    REQUIRE(!replacement.commit());
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

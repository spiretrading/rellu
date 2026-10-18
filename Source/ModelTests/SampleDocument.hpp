#ifndef RELLU_MODEL_TESTS_SAMPLE_DOCUMENT_HPP
#define RELLU_MODEL_TESTS_SAMPLE_DOCUMENT_HPP
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "Rellu/Geometry/SvgPath.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu::Tests {

  /** The ids of the nodes and resources making up a sample document. */
  struct Sample {

    /** The artboard on the canvas. */
    NodeId m_artboard;

    /** A group in the artboard acting as a stack with padding. */
    NodeId m_stack;

    /** A rectangle in the stack. */
    NodeId m_rectangle;

    /** An ellipse in the stack. */
    NodeId m_ellipse;

    /** A repeat grid in the artboard. */
    NodeId m_repeat_grid;

    /** A line in the repeat grid. */
    NodeId m_line;

    /** A scroll group in the artboard. */
    NodeId m_scroll_group;

    /** A polygon in the scroll group. */
    NodeId m_polygon;

    /** A masked group in the artboard. */
    NodeId m_mask;

    /** A path in the masked group. */
    NodeId m_path;

    /** A compound shape in the artboard. */
    NodeId m_compound;

    /** The first operand of the compound shape. */
    NodeId m_first_operand;

    /** The second operand of the compound shape. */
    NodeId m_second_operand;

    /** A text node in the artboard. */
    NodeId m_text;

    /** A component instance in the artboard. */
    NodeId m_instance;

    /** A reference in the instance to the master's shape. */
    NodeId m_reference;

    /** A shape on the canvas outside any artboard. */
    NodeId m_loose;

    /** The root of the component's master tree. */
    NodeId m_master;

    /** A shape in the master tree. */
    NodeId m_master_shape;

    /** The root of the component's state tree. */
    NodeId m_state_tree;

    /** The component. */
    ComponentId m_component;

    /** The component's state. */
    StateId m_state;

    /** The image. */
    ImageId m_image;

    /** The color swatch. */
    SwatchId m_swatch;

    /** The character style. */
    CharacterStyleId m_character_style;

    /** The interaction owned by the rectangle. */
    InteractionId m_interaction;
  };

  /**
   * Makes a header with a fresh id.
   * @param name The node's name.
   */
  inline NodeHeader make_header(std::string name) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    return header;
  }

  /**
   * Makes a filled rectangle shape.
   * @param width The rectangle's width.
   * @param height The rectangle's height.
   */
  inline Shape make_rectangle_shape(double width, double height) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, width, height));
    shape.m_style.m_fill = SolidFill(make_color(255, 0, 0));
    return shape;
  }

  /**
   * Makes a text payload styled by a single run.
   * @param content The text.
   */
  inline Text make_text(const std::u16string& content) {
    auto text = Text();
    text.m_content = Shared(content);
    text.m_frame = AreaFrame(Size(200, 40));
    text.m_base_style.m_font = Font("Roboto", "Regular", "Roboto-Regular", 14);
    text.m_runs.push_back(TextRun(content.size(), text.m_base_style));
    return text;
  }

  /**
   * Builds a document holding every kind of node, group role, shape
   * geometry and resource in a single transaction.
   * @param document The empty document to build in.
   * @return The ids of what was built.
   */
  inline Sample build_sample(Document& document) {
    auto sample = Sample();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 375, 812);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    artboard.m_viewport_height = 600;
    artboard.m_layout_grid = LayoutGrid();
    artboard.m_guides.push_back(Guide(Orientation::VERTICAL, 16));
    sample.m_artboard =
      transaction.append(NodeId(), make_header("Home"), artboard);
    auto stack = Group();
    stack.m_stack = Stack(Orientation::HORIZONTAL);
    stack.m_padding = Padding(Margins(4, 4, 4, 4), true, true);
    sample.m_stack =
      transaction.append(sample.m_artboard, make_header("Stack"), stack);
    sample.m_rectangle = transaction.append(
      sample.m_stack, make_header("Rectangle"), make_rectangle_shape(10, 20));
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(5, 5), 5, 3);
    sample.m_ellipse =
      transaction.append(sample.m_stack, make_header("Ellipse"), ellipse);
    auto repeat_grid = Group();
    repeat_grid.m_repeat_grid = RepeatGrid(Size(50, 50), 10, 10, 2, 3);
    sample.m_repeat_grid = transaction.append(
      sample.m_artboard, make_header("Repeat Grid"), repeat_grid);
    auto line = Shape();
    line.m_geometry = LineShape(Point(0, 0), Point(50, 0));
    line.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
    sample.m_line =
      transaction.append(sample.m_repeat_grid, make_header("Line"), line);
    auto scroll_group = Group();
    scroll_group.m_scroll_group =
      ScrollGroup(ScrollDirection::VERTICAL, Size(100, 200));
    sample.m_scroll_group = transaction.append(
      sample.m_artboard, make_header("Scroll Group"), scroll_group);
    auto polygon = Shape();
    polygon.m_geometry = PolygonShape(5, Size(40, 40), 2);
    sample.m_polygon = transaction.append(
      sample.m_scroll_group, make_header("Polygon"), polygon);
    auto mask = Group();
    mask.m_mask = Mask();
    mask.m_mask->m_shapes.push_back(
      MaskShape(Matrix(), EllipseShape(Point(10, 10), 10, 10)));
    sample.m_mask =
      transaction.append(sample.m_artboard, make_header("Mask"), mask);
    auto path = Shape();
    path.m_geometry =
      PathShape(Shared(*parse_svg_path("M0 0 L10 0 L10 10 Z")));
    sample.m_path =
      transaction.append(sample.m_mask, make_header("Path"), path);
    auto compound = Shape();
    compound.m_geometry = CompoundShape(CompoundOperation::SUBTRACT,
      Shared(make_rectangle(Rect(0, 0, 20, 20))));
    sample.m_compound = transaction.append(
      sample.m_artboard, make_header("Compound"), compound);
    sample.m_first_operand = transaction.append(sample.m_compound,
      make_header("First"), make_rectangle_shape(20, 20));
    sample.m_second_operand = transaction.append(sample.m_compound,
      make_header("Second"), make_rectangle_shape(10, 10));
    sample.m_text = transaction.append(
      sample.m_artboard, make_header("Text"), make_text(u"Hello"));
    sample.m_loose =
      transaction.append(NodeId(), make_header("Loose"),
        make_rectangle_shape(5, 5));
    sample.m_master =
      transaction.insert_root(make_header("Button"), Group());
    sample.m_master_shape = transaction.append(sample.m_master,
      make_header("Background"), make_rectangle_shape(80, 30));
    sample.m_state_tree =
      transaction.insert_root(make_header("Hover"), Group());
    sample.m_state = generate_node_id();
    auto component = Component();
    component.m_name = "Button";
    component.m_master = sample.m_master;
    component.m_default_state = generate_node_id();
    component.m_states.push_back(
      ComponentState(sample.m_state, "Hover", sample.m_state_tree));
    sample.m_component = transaction.add_component(component);
    auto instance = Group();
    instance.m_component_instance =
      ComponentInstance(sample.m_component, StateId(), Size(80, 30));
    sample.m_instance = transaction.append(
      sample.m_artboard, make_header("Button"), instance);
    auto reference = Reference(sample.m_master_shape);
    sample.m_reference = transaction.append(
      sample.m_instance, make_header("Background"), reference);
    sample.m_image = transaction.add_image(ImageResource(NodeId(),
      "image/png", Shared(std::vector<std::byte>(4, std::byte(1)))));
    sample.m_swatch = transaction.add_swatch(
      ColorSwatch(NodeId(), "Primary", make_color(0, 0, 255)));
    sample.m_character_style = transaction.add_character_style(
      CharacterStyle(NodeId(), "Body", make_text(u"").m_base_style));
    sample.m_interaction = transaction.add_interaction(Interaction(NodeId(),
      sample.m_rectangle, TapTrigger(), ArtboardTransition(sample.m_artboard)));
    transaction.set_home_artboard(sample.m_artboard);
    transaction.set_flows({Flow(sample.m_artboard, "Main")});
    transaction.commit();
    return sample;
  }

  /** Captures the complete state of a document for comparison. */
  struct Snapshot {

    /** Every node by id. */
    std::map<NodeId, Node> m_nodes;

    /** The canvas. */
    std::vector<NodeId> m_canvas;

    /** The resources. */
    Resources m_resources;

    /** The prototype. */
    Prototype m_prototype;

    bool operator ==(const Snapshot&) const = default;
  };

  /**
   * Captures a document's state.
   * @param document The document to capture.
   */
  inline Snapshot take_snapshot(const Document& document) {
    auto snapshot = Snapshot();
    document.get_nodes().for_each([&] (const auto& node) {
      snapshot.m_nodes.emplace(node.get_id(), node);
    });
    snapshot.m_canvas = document.get_canvas();
    snapshot.m_resources = document.get_resources();
    snapshot.m_prototype = document.get_prototype();
    return snapshot;
  }
}

#endif

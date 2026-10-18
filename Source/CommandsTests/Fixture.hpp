#ifndef RELLU_COMMANDS_TESTS_FIXTURE_HPP
#define RELLU_COMMANDS_TESTS_FIXTURE_HPP
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu::Tests {

  /** The ids of the nodes and resources making up a test scene. */
  struct Scene {

    /** The artboard, framed at (100, 50) with size (400, 300). */
    NodeId m_artboard;

    /** A stroked 40 by 20 rectangle at (10, 10) in the artboard. */
    NodeId m_rectangle;

    /** A group in the artboard, rotated by 30 degrees about (200, 100). */
    NodeId m_group;

    /** An ellipse in the group. */
    NodeId m_ellipse;

    /** An area text in the group. */
    NodeId m_text;

    /** A masked group in the artboard holding a path. */
    NodeId m_mask;

    /** The path in the masked group. */
    NodeId m_path;

    /** A rectangle on the canvas outside the artboard. */
    NodeId m_loose;

    /** The root of the component's master tree. */
    NodeId m_master;

    /** The 80 by 30 rectangle in the master tree. */
    NodeId m_background;

    /** A component instance in the artboard at (300, 200). */
    NodeId m_instance;

    /** The reference in the instance to the master's rectangle. */
    NodeId m_reference;

    /** The instance's own state tree. */
    NodeId m_state_tree;

    /** The component. */
    ComponentId m_component;

    /** The interaction owned by the rectangle. */
    InteractionId m_interaction;
  };

  /**
   * Makes a header with a fresh id.
   * @param name The node's name.
   * @param transform The node's transform.
   */
  inline NodeHeader make_header(
      std::string name, const Matrix& transform = Matrix()) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_transform = transform;
    return header;
  }

  /**
   * Makes a rectangle shape filled red.
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
   * Makes a text payload in an area frame styled by a single run.
   * @param content The text.
   */
  inline Text make_text(const std::u16string& content) {
    auto text = Text();
    text.m_content = Shared(content);
    text.m_frame = AreaFrame(Size(100, 40));
    text.m_base_style.m_font = Font("Roboto", "Regular", "Roboto-Regular", 14);
    text.m_runs.push_back(TextRun(content.size(), text.m_base_style));
    return text;
  }

  /**
   * Builds the test scene in a single transaction.
   * @param document The empty document to build in.
   * @return The ids of what was built.
   */
  inline Scene build_scene(Document& document) {
    auto scene = Scene();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 400, 300);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    scene.m_artboard =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    auto rectangle = make_rectangle_shape(40, 20);
    rectangle.m_style.m_stroke = Stroke(make_color(0, 0, 0), 4);
    scene.m_rectangle = transaction.append(scene.m_artboard,
      make_header("Rectangle", make_translation(10, 10)), rectangle);
    scene.m_group = transaction.append(scene.m_artboard, make_header("Group",
      make_translation(200, 100) * make_rotation(std::numbers::pi / 6)),
      Group());
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(10, 10), 10, 5);
    scene.m_ellipse = transaction.append(scene.m_group,
      make_header("Ellipse", make_translation(5, 5)), ellipse);
    scene.m_text = transaction.append(scene.m_group,
      make_header("Text", make_translation(0, 30)), make_text(u"Hello"));
    auto mask = Group();
    mask.m_mask = Mask();
    mask.m_mask->m_shapes.push_back(
      MaskShape(Matrix(), RectangleShape(Rect(0, 0, 20, 20))));
    scene.m_mask = transaction.append(scene.m_artboard,
      make_header("Mask", make_translation(50, 150)), mask);
    auto path = Shape();
    auto outline = Path();
    outline.move_to(Point(0, 0));
    outline.line_to(Point(30, 0));
    outline.line_to(Point(30, 30));
    outline.close();
    path.m_geometry = PathShape(Shared(std::move(outline)));
    scene.m_path = transaction.append(scene.m_mask, make_header("Path"), path);
    scene.m_loose = transaction.append(NodeId(),
      make_header("Loose", make_translation(-50, -50)),
      make_rectangle_shape(10, 10));
    scene.m_master = transaction.insert_root(make_header("Button"), Group());
    scene.m_background = transaction.append(scene.m_master,
      make_header("Background"), make_rectangle_shape(80, 30));
    auto component = Component();
    component.m_name = "Button";
    component.m_master = scene.m_master;
    scene.m_component = transaction.add_component(component);
    scene.m_state_tree =
      transaction.insert_root(make_header("Hover"), Group());
    transaction.append(scene.m_state_tree, make_header("Hover Background"),
      make_rectangle_shape(80, 30));
    auto instance = Group();
    instance.m_component_instance =
      ComponentInstance(scene.m_component, StateId(), Size(80, 30));
    instance.m_component_instance->m_states.push_back(
      ComponentState(generate_node_id(), "Hover", scene.m_state_tree));
    scene.m_instance = transaction.append(scene.m_artboard,
      make_header("Button", make_translation(300, 200)), instance);
    scene.m_reference = transaction.append(scene.m_instance,
      make_header("Background"), Reference(scene.m_background));
    auto synced = *document.get(scene.m_instance).get_group();
    synced.m_component_instance->m_sync_map.emplace(
      scene.m_instance, scene.m_master);
    synced.m_component_instance->m_sync_map.emplace(
      scene.m_reference, scene.m_background);
    transaction.replace_payload(scene.m_instance, synced);
    scene.m_interaction = transaction.add_interaction(Interaction(NodeId(),
      scene.m_rectangle, TapTrigger(), ArtboardTransition(scene.m_artboard)));
    transaction.commit();
    document.clear_history();
    return scene;
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

  /**
   * Executes a command through a history, requiring that it changes the
   * document as one step and that undoing and redoing it restore the
   * document exactly, leaving the command applied.
   * @param history The history to execute through.
   * @param command The command to execute.
   * @return The command recorded.
   */
  inline const Command* require_undoable(
      History& history, std::unique_ptr<Command> command) {
    auto& document = history.get_document();
    auto before = take_snapshot(document);
    auto count = history.get_undo_count();
    auto label = command->get_label();
    auto recorded = history.execute(std::move(command));
    REQUIRE(recorded);
    auto after = take_snapshot(document);
    REQUIRE(after != before);
    REQUIRE(history.get_undo_count() == count + 1);
    REQUIRE(history.get_undo_label() == label);
    REQUIRE(history.undo());
    REQUIRE(take_snapshot(document) == before);
    REQUIRE(history.get_redo_label() == label);
    REQUIRE(history.redo());
    REQUIRE(take_snapshot(document) == after);
    return recorded;
  }

  /**
   * Requires two transforms to be equal within a tolerance.
   * @param actual The transform obtained.
   * @param expected The transform expected.
   */
  inline void require_near(const Matrix& actual, const Matrix& expected) {
    REQUIRE(actual.m_a == doctest::Approx(expected.m_a));
    REQUIRE(actual.m_b == doctest::Approx(expected.m_b));
    REQUIRE(actual.m_c == doctest::Approx(expected.m_c));
    REQUIRE(actual.m_d == doctest::Approx(expected.m_d));
    REQUIRE(actual.m_tx == doctest::Approx(expected.m_tx));
    REQUIRE(actual.m_ty == doctest::Approx(expected.m_ty));
  }

  /**
   * Requires two rects to be equal within a tolerance.
   * @param actual The rect obtained.
   * @param expected The rect expected.
   */
  inline void require_near(const Rect& actual, const Rect& expected) {
    REQUIRE(actual.m_x == doctest::Approx(expected.m_x));
    REQUIRE(actual.m_y == doctest::Approx(expected.m_y));
    REQUIRE(actual.m_width == doctest::Approx(expected.m_width));
    REQUIRE(actual.m_height == doctest::Approx(expected.m_height));
  }
}

#endif

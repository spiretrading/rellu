#ifndef RELLU_PROTOTYPE_TESTS_STAGE_HPP
#define RELLU_PROTOTYPE_TESTS_STAGE_HPP
#include <memory>
#include <string>
#include <utility>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Prototype/PreviewPlayer.hpp"
#include "Rellu/Prototype/StageDocument.hpp"

namespace Rellu::Tests {

  /** The ids of the nodes making up a prototype test document. */
  struct PrototypeScene {

    /** The 400 by 300 white artboard at the origin. */
    NodeId m_home;

    /** A 100 by 50 rectangle at (10, 10) in the home artboard. */
    NodeId m_button;

    /** A red 50 by 50 rectangle named Box at (200, 100) in the home. */
    NodeId m_box;

    /** The 400 by 300 blue artboard at (500, 0). */
    NodeId m_second;

    /** A green 100 by 100 rectangle named Box at (300, 200) in the second. */
    NodeId m_second_box;

    /** A 50 by 50 rectangle at (10, 10) in the second artboard. */
    NodeId m_back;

    /** A 200 by 100 artboard at (1000, 0) shown as an overlay. */
    NodeId m_menu;

    /** A 400 by 800 artboard at (0, 400) showing 300 of its height. */
    NodeId m_long;

    /** A 400 by 50 rectangle at the top of the long artboard. */
    NodeId m_header;

    /** A 100 by 100 rectangle at (0, 600) in the long artboard. */
    NodeId m_footer;

    /** The component instance at (50, 200) in the home artboard. */
    NodeId m_instance;

    /** The 20 by 20 rectangle named Knob the instance shows. */
    NodeId m_knob;

    /** The instance's hover state. */
    StateId m_hover;

    /** The root of the hover state's tree. */
    NodeId m_hover_tree;

    /** The 40 by 20 rectangle named Knob in the hover state. */
    NodeId m_hover_knob;

    /** The component. */
    ComponentId m_component;
  };

  /**
   * Makes a header with a fresh id.
   * @param name The node's name.
   * @param x The x translation.
   * @param y The y translation.
   */
  inline NodeHeader make_stage_header(std::string name, double x, double y) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_transform = make_translation(x, y);
    return header;
  }

  /**
   * Adds a filled rectangle.
   * @param transaction The transaction adding it.
   * @param parent The parent.
   * @param name The rectangle's name.
   * @param rect The rectangle's position and size in its parent.
   * @param color The fill.
   * @return The rectangle's id.
   */
  inline NodeId add_box(Transaction& transaction, const NodeId& parent,
      std::string name, const Rect& rect, const Color& color) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, rect.m_width, rect.m_height));
    shape.m_style.m_fill = SolidFill(color);
    return transaction.append(parent,
      make_stage_header(std::move(name), rect.m_x, rect.m_y), shape);
  }

  /**
   * Adds an artboard.
   * @param transaction The transaction adding it.
   * @param name The artboard's name.
   * @param frame The artboard's frame.
   * @param color The background.
   * @return The artboard's id.
   */
  inline NodeId add_board(Transaction& transaction, std::string name,
      const Rect& frame, const Color& color) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    artboard.m_background = SolidFill(color);
    return transaction.append(
      NodeId(), make_stage_header(std::move(name), 0, 0), artboard);
  }

  /**
   * Builds the prototype test document in a single transaction, without
   * interactions.
   * @param document The empty document to build in.
   * @return The ids of what was built.
   */
  inline PrototypeScene build_prototype_scene(Document& document) {
    auto scene = PrototypeScene();
    auto transaction = Transaction(document);
    scene.m_home = add_board(transaction, "Home", Rect(0, 0, 400, 300),
      make_color(255, 255, 255));
    scene.m_button = add_box(transaction, scene.m_home, "Button",
      Rect(10, 10, 100, 50), make_color(0, 0, 0));
    scene.m_box = add_box(transaction, scene.m_home, "Box",
      Rect(200, 100, 50, 50), make_color(255, 0, 0));
    scene.m_second = add_board(transaction, "Second", Rect(500, 0, 400, 300),
      make_color(0, 0, 255));
    scene.m_second_box = add_box(transaction, scene.m_second, "Box",
      Rect(300, 200, 100, 100), make_color(0, 255, 0));
    scene.m_back = add_box(transaction, scene.m_second, "Back",
      Rect(10, 10, 50, 50), make_color(0, 0, 0));
    scene.m_menu = add_board(transaction, "Menu", Rect(1000, 0, 200, 100),
      make_color(200, 200, 200));
    auto tall = Artboard();
    tall.m_frame = Rect(0, 400, 400, 800);
    tall.m_background = SolidFill(make_color(255, 255, 255));
    tall.m_viewport_height = 300;
    scene.m_long =
      transaction.append(NodeId(), make_stage_header("Long", 0, 0), tall);
    scene.m_header = add_box(transaction, scene.m_long, "Header",
      Rect(0, 0, 400, 50), make_color(0, 0, 0));
    scene.m_footer = add_box(transaction, scene.m_long, "Footer",
      Rect(0, 600, 100, 100), make_color(0, 0, 0));
    auto master = transaction.insert_root(
      make_stage_header("Toggle", 0, 0), Group());
    add_box(transaction, master, "Knob", Rect(0, 0, 20, 20),
      make_color(0, 0, 0));
    auto component = Component();
    component.m_name = "Toggle";
    component.m_master = master;
    scene.m_component = transaction.add_component(component);
    scene.m_hover_tree =
      transaction.insert_root(make_stage_header("Hover", 0, 0), Group());
    scene.m_hover_knob = add_box(transaction, scene.m_hover_tree, "Knob",
      Rect(0, 0, 40, 20), make_color(255, 0, 0));
    scene.m_hover = generate_node_id();
    auto instance = Group();
    instance.m_component_instance =
      ComponentInstance(scene.m_component, StateId(), Size(20, 20));
    instance.m_component_instance->m_states.push_back(
      ComponentState(scene.m_hover, "Hover", scene.m_hover_tree));
    scene.m_instance = transaction.append(
      scene.m_home, make_stage_header("Toggle", 50, 200), instance);
    scene.m_knob = add_box(transaction, scene.m_instance, "Knob",
      Rect(0, 0, 20, 20), make_color(0, 0, 0));
    transaction.commit();
    document.clear_history();
    return scene;
  }

  /**
   * Adds an interaction.
   * @param document The document.
   * @param owner The node the interaction is attached to.
   * @param trigger The trigger.
   * @param action The action.
   * @return The interaction's id.
   */
  inline InteractionId add_interaction(Document& document,
      const NodeId& owner, Trigger trigger, Action action) {
    auto transaction = Transaction(document);
    auto id = transaction.add_interaction(Interaction(
      NodeId(), owner, std::move(trigger), std::move(action)));
    transaction.commit();
    document.clear_history();
    return id;
  }

  /**
   * Makes a player of a copy of a document, started on an artboard.
   * @param document The document.
   * @param artboard The artboard to start on.
   */
  inline std::unique_ptr<PreviewPlayer> make_player(
      const Document& document, const NodeId& artboard) {
    auto player = std::make_unique<PreviewPlayer>(copy_document(document));
    player->start(artboard);
    return player;
  }
}

#endif

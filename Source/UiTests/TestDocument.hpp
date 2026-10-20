#ifndef RELLU_UI_TESTS_TEST_DOCUMENT_HPP
#define RELLU_UI_TESTS_TEST_DOCUMENT_HPP
#include <string>
#include <utility>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu::Tests {

  /** The ids of the nodes making up a test document. */
  struct TestDocument {

    /** The artboard on the canvas. */
    NodeId m_artboard;

    /** A group in the artboard. */
    NodeId m_card;

    /** A rectangle in the group. */
    NodeId m_background;

    /** An unnamed text node in the group. */
    NodeId m_label;

    /** An unnamed ellipse in the artboard. */
    NodeId m_ellipse;

    /** A component instance in the artboard. */
    NodeId m_instance;

    /** An unnamed reference in the instance to the master's shape. */
    NodeId m_reference;

    /** A shape on the canvas outside the artboard. */
    NodeId m_loose;

    /** The root of the component's master tree. */
    NodeId m_master;

    /** The shape in the master tree. */
    NodeId m_master_shape;

    /** The component. */
    ComponentId m_component;
  };

  /**
   * Makes a header with a fresh id.
   * @param name The node's name, a custom one unless empty.
   */
  inline NodeHeader make_header(std::string name) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_has_custom_name = !header.m_name.empty();
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
    text.m_base_style.m_font = Font("Roboto", "Medium", "Roboto-Medium", 14);
    text.m_runs.push_back(TextRun(content.size(), text.m_base_style));
    return text;
  }

  /**
   * Builds the test document in a single transaction.
   * @param document The empty document to build in.
   * @return The ids of what was built.
   */
  inline TestDocument build_test_document(Document& document) {
    auto ids = TestDocument();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 375, 812);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    ids.m_artboard =
      transaction.append(NodeId(), make_header("Home"), artboard);
    auto card = make_header("Card");
    card.m_transform = make_translation(10, 20);
    ids.m_card = transaction.append(ids.m_artboard, card, Group());
    ids.m_background = transaction.append(
      ids.m_card, make_header("Background"), make_rectangle_shape(100, 50));
    ids.m_label = transaction.append(
      ids.m_card, make_header(""), make_text(u"Hello world\nSecond line"));
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(5, 5), 5, 3);
    ids.m_ellipse =
      transaction.append(ids.m_artboard, make_header(""), ellipse);
    ids.m_master = transaction.insert_root(make_header("Button"), Group());
    auto fill = make_rectangle_shape(80, 30);
    fill.m_style.m_fill = SolidFill(make_color(0, 0, 255));
    fill.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
    ids.m_master_shape =
      transaction.append(ids.m_master, make_header("Fill"), fill);
    auto component = Component();
    component.m_name = "Button";
    component.m_master = ids.m_master;
    ids.m_component = transaction.add_component(component);
    auto instance = Group();
    instance.m_component_instance =
      ComponentInstance(ids.m_component, StateId(), Size(80, 30));
    auto instance_header = make_header("Button");
    instance_header.m_has_custom_name = false;
    ids.m_instance =
      transaction.append(ids.m_artboard, instance_header, instance);
    ids.m_reference = transaction.append(ids.m_instance, make_header(""),
      Reference(ids.m_master_shape));
    ids.m_loose = transaction.append(
      NodeId(), make_header("Loose"), make_rectangle_shape(5, 5));
    transaction.commit();
    return ids;
  }
}

#endif

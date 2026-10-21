#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Commands/CreationCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("CreationCommands") {
  TEST_CASE("default_style") {
    auto style = make_default_style();
    REQUIRE(style.m_fill == Fill(SolidFill(make_color(255, 255, 255))));
    REQUIRE(style.m_stroke ==
      Stroke(make_color(112, 112, 112), 1, StrokeAlignment::INSIDE));
    REQUIRE(style.m_effects.empty());
    auto line = make_default_line_style();
    REQUIRE(line.m_fill == Fill(NoFill()));
    REQUIRE(line.m_stroke == Stroke(make_color(112, 112, 112), 1));
  }

  TEST_CASE("node_name") {
    auto document = Document();
    REQUIRE(make_node_name(document, "Rectangle ") == "Rectangle 1");
    auto make_default_header = [] (const char* name) {
      auto header = make_header(name);
      header.m_has_custom_name = false;
      return header;
    };
    auto transaction = Transaction(document);
    transaction.append(NodeId(), make_default_header("Rectangle 7"),
      make_rectangle_shape(1, 1));
    transaction.append(NodeId(), make_default_header("Rectangle 12a"),
      make_rectangle_shape(1, 1));
    transaction.append(NodeId(), make_default_header("Rectangle 3"),
      make_rectangle_shape(1, 1));
    transaction.append(NodeId(), make_default_header("Ellipse 40"),
      make_rectangle_shape(1, 1));
    transaction.append(NodeId(), make_header("Rectangle 50"),
      make_rectangle_shape(1, 1));
    transaction.commit();
    REQUIRE(make_node_name(document, "Rectangle ") == "Rectangle 8");
    REQUIRE(make_node_name(document, "Ellipse ") == "Ellipse 41");
    REQUIRE(make_node_name(document, "Line ") == "Line 1");
  }

  TEST_CASE("insert") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto header = make_header("Rectangle 1", make_translation(5, 6));
    auto id = header.m_id;
    auto command = std::make_unique<InsertNodeCommand>("Rectangle",
      scene.m_artboard, header, make_rectangle_shape(10, 20));
    REQUIRE(command->get_node() == id);
    REQUIRE(command->get_selection() == std::vector{id});
    require_undoable(history, std::move(command));
    REQUIRE(history.get_undo_label() == "Rectangle");
    auto& node = document.get(id);
    REQUIRE(node.m_parent == scene.m_artboard);
    REQUIRE(document.get(scene.m_artboard).m_children.back() == id);
    REQUIRE(node.m_header == header);
    REQUIRE(*node.get_shape() == make_rectangle_shape(10, 20));
    auto generated = InsertNodeCommand(
      "Group", NodeId(), NodeHeader(), Group());
    REQUIRE(!generated.get_node().is_nil());
  }

  TEST_CASE("insert_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto before = take_snapshot(document);
    auto header = make_header("Ellipse 1");
    REQUIRE(history.merge(std::make_unique<InsertNodeCommand>("Ellipse",
      NodeId(), header, make_rectangle_shape(1, 1))));
    header.m_transform = make_translation(3, 4);
    REQUIRE(history.merge(std::make_unique<InsertNodeCommand>("Ellipse",
      NodeId(), header, make_rectangle_shape(8, 9))));
    REQUIRE(history.get_undo_count() == 1);
    auto& node = document.get(header.m_id);
    REQUIRE(node.m_header.m_transform == make_translation(3, 4));
    REQUIRE(*node.get_shape() == make_rectangle_shape(8, 9));
    REQUIRE(document.get_canvas().back() == header.m_id);
    REQUIRE_THROWS_AS(history.merge(std::make_unique<InsertNodeCommand>(
      "Ellipse", scene.m_artboard, header, make_rectangle_shape(2, 2))),
      std::invalid_argument);
    REQUIRE(history.get_undo_count() == 1);
    history.seal();
    REQUIRE(history.undo());
    REQUIRE(take_snapshot(document) == before);
  }
}

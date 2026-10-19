#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include <QKeySequence>
#include <QWidget>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto PATH_ACTIONS = std::vector<std::string>({"object.path.add",
    "object.path.subtract", "object.path.intersect", "object.path.exclude",
    "object.path.convert"});

  struct Shapes {
    NodeId m_artboard;
    NodeId m_bottom;
    NodeId m_top;
    NodeId m_line;
  };

  Shapes build_shapes(Document& document) {
    auto shapes = Shapes();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 300);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    shapes.m_artboard =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    auto bottom = make_rectangle_shape(40, 40);
    bottom.m_geometry = RectangleShape(Rect(0, 0, 40, 40));
    shapes.m_bottom = transaction.append(
      shapes.m_artboard, make_header("Bottom"), bottom);
    auto top_header = make_header("Top");
    top_header.m_transform = make_translation(20, 20);
    shapes.m_top =
      transaction.append(shapes.m_artboard, top_header, bottom);
    auto line = Shape(LineShape(Point(0, 0), Point(40, 20)),
      make_default_path_style());
    auto line_header = make_header("Line");
    line_header.m_transform = make_translation(200, 100);
    shapes.m_line = transaction.append(shapes.m_artboard, line_header, line);
    transaction.commit();
    document.clear_history();
    return shapes;
  }

  std::vector<std::string> filter(std::vector<std::string> differences) {
    std::erase(differences, "document opaque data differs");
    return differences;
  }
}

TEST_SUITE("PathCommands") {
  TEST_CASE("menu_actions") {
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto commands = CommandRegistry();
    add_standard_commands(commands);
    add_path_commands(commands);
    auto edit_actions = EditActions(actions, commands);
    REQUIRE(actions.get("object.path.add").shortcut() ==
      QKeySequence("Ctrl+Alt+U"));
    REQUIRE(actions.get("object.path.subtract").shortcut() ==
      QKeySequence("Ctrl+Alt+S"));
    REQUIRE(actions.get("object.path.intersect").shortcut() ==
      QKeySequence("Ctrl+Alt+I"));
    REQUIRE(actions.get("object.path.exclude").shortcut() ==
      QKeySequence("Ctrl+Alt+X"));
    REQUIRE(actions.get("object.path.convert").shortcut() ==
      QKeySequence("Ctrl+8"));
    auto document = Document();
    auto shapes = build_shapes(document);
    auto editor = Editor(document);
    edit_actions.set_editor(&editor);
    for(auto& name : PATH_ACTIONS) {
      REQUIRE(!actions.get(name).isEnabled());
    }
    editor.get_selection().select(shapes.m_bottom);
    REQUIRE(actions.get("object.path.convert").isEnabled());
    REQUIRE(!actions.get("object.path.add").isEnabled());
    editor.get_selection().select(std::vector{shapes.m_bottom, shapes.m_top});
    for(auto& name : PATH_ACTIONS) {
      REQUIRE(actions.get(name).isEnabled());
    }
    REQUIRE(!actions.get("object.path.outline_stroke").isEnabled());
    REQUIRE(actions.trigger("object.path.subtract"));
    auto& selection = editor.get_selection().get_nodes();
    REQUIRE(selection.size() == 1);
    auto compound = selection.front();
    REQUIRE(document.get(compound).m_header.m_name == "Subtraction 1");
    REQUIRE(document.get(compound).m_children ==
      std::vector{shapes.m_bottom, shapes.m_top});
    REQUIRE(actions.get("edit.undo").text() == "Undo Subtract");
    REQUIRE(actions.get("object.path.add").isEnabled());
    REQUIRE(actions.trigger("object.path.add"));
    REQUIRE(std::get<CompoundShape>(document.get(compound).get_shape()->
      m_geometry).m_operation == CompoundOperation::UNION);
    REQUIRE(editor.get_history().get_undo_count() == 2);
    REQUIRE(actions.trigger("object.path.convert"));
    REQUIRE(std::holds_alternative<PathShape>(
      document.get(compound).get_shape()->m_geometry));
    REQUIRE(!actions.get("object.path.convert").isEnabled());
    REQUIRE(actions.trigger("edit.undo"));
    REQUIRE(std::holds_alternative<CompoundShape>(
      document.get(compound).get_shape()->m_geometry));
  }

  TEST_CASE("saving") {
    auto document = Document();
    auto shapes = build_shapes(document);
    auto editor = Editor(document);
    auto commands = CommandRegistry();
    add_path_commands(commands);
    auto outline = Path();
    outline.move_to(Point(10, 200));
    outline.line_to(Point(60, 200));
    outline.cubic_to(Point(80, 200), Point(90, 220), Point(90, 240));
    outline.close();
    auto header = NodeHeader();
    header.m_name = "Path 1";
    REQUIRE(editor.execute(std::make_unique<InsertNodeCommand>("Pen",
      shapes.m_artboard, header,
      Shape(PathShape(Shared(outline)), make_default_path_style()))));
    auto open = Path();
    open.move_to(Point(300, 20));
    open.line_to(Point(350, 70));
    header.m_name = "Path 2";
    REQUIRE(editor.execute(std::make_unique<InsertNodeCommand>("Pen",
      shapes.m_artboard, header,
      Shape(PathShape(Shared(open)), make_default_path_style()))));
    editor.get_selection().select(std::vector{shapes.m_bottom, shapes.m_top});
    REQUIRE(editor.execute(commands, "object.path.exclude"));
    editor.get_selection().select(shapes.m_line);
    REQUIRE(editor.execute(commands, "object.path.convert"));
    auto imported = Document();
    auto report = import_xd(XdPackage(write_xd(document)), imported);
    REQUIRE(report.m_warnings.empty());
    auto differences = filter(find_differences(document, imported));
    auto text = std::string();
    for(auto& difference : differences) {
      text += difference + '\n';
    }
    REQUIRE(text == "");
    auto rewritten = Document();
    import_xd(XdPackage(write_xd(imported)), rewritten);
    REQUIRE(filter(find_differences(imported, rewritten)).empty());
  }
}

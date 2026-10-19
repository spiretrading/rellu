#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QJsonObject>
#include <QLineEdit>
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StackCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/LayoutProperties.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  struct Board {
    NodeId m_artboard;
    NodeId m_stack;
    NodeId m_first;
    NodeId m_second;
    NodeId m_cell;
  };

  NodeHeader make_header(std::string name, double x = 0, double y = 0) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_transform = make_translation(x, y);
    return header;
  }

  Shape make_box(double width, double height) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, width, height));
    shape.m_style.m_fill = SolidFill(make_color(255, 0, 0));
    return shape;
  }

  Board build_board(Document& document) {
    auto board = Board();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 400);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    board.m_artboard =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    board.m_stack = transaction.append(
      board.m_artboard, make_header("List", 20, 20), Group());
    board.m_first = transaction.append(
      board.m_stack, make_header("First"), make_box(100, 20));
    board.m_second = transaction.append(
      board.m_stack, make_header("Second", 0, 30), make_box(100, 20));
    board.m_cell = transaction.append(
      board.m_artboard, make_header("Cell", 200, 20), make_box(40, 40));
    transaction.commit();
    document.clear_history();
    return board;
  }

  PropertyEditors make_editors() {
    auto editors = make_standard_property_editors(estimate_text_bounds);
    add_layout_property_editors(editors, estimate_text_bounds);
    return editors;
  }

  void apply(History& history, const PropertyEditors& editors,
      const char* path, const std::vector<NodeId>& nodes,
      const PropertyValue& value) {
    auto editor = editors.find(path);
    REQUIRE(editor);
    auto command = editor->m_setter(history.get_document(), nodes, value);
    REQUIRE(command);
    history.execute(std::move(command));
  }

  const PropertyDescriptor& get_property(const Document& document,
      const NodeId& id, const char* path) {
    static auto properties = std::vector<PropertyDescriptor>();
    properties = extract_properties(document, id);
    auto property = find_property(properties, path);
    REQUIRE(property);
    return *property;
  }

  QJsonObject find_json(const QJsonValue& value, const std::string& id) {
    if(value.isObject()) {
      auto object = value.toObject();
      if(object.value("id"_L1).toString().toStdString() == id) {
        return object;
      }
      for(auto member : object) {
        if(auto found = find_json(member, id); !found.isEmpty()) {
          return found;
        }
      }
    } else if(value.isArray()) {
      for(auto element : value.toArray()) {
        if(auto found = find_json(element, id); !found.isEmpty()) {
          return found;
        }
      }
    }
    return QJsonObject();
  }

  QJsonObject find_written(const XdPackage& package, const NodeId& id) {
    for(auto& entry : package.get_entries()) {
      if(entry.ends_with(".agc")) {
        if(auto node = find_json(package.read_json(entry), to_string(id));
            !node.isEmpty()) {
          return node;
        }
      }
    }
    return QJsonObject();
  }

  QJsonObject get_ux(const QJsonObject& node) {
    return node.value("meta"_L1).toObject().value("ux"_L1).toObject();
  }
}

TEST_SUITE("LayoutProperties") {
  TEST_CASE("properties") {
    auto document = Document();
    auto board = build_board(document);
    REQUIRE(get_property(document, board.m_stack, "layout.stack").m_value ==
      PropertyValue(false));
    REQUIRE(get_property(document, board.m_stack, "layout.padding").m_value ==
      PropertyValue(false));
    auto properties = extract_properties(document, board.m_cell);
    REQUIRE(!find_property(properties, "layout.stack"));
    auto history = History(document);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{board.m_stack}, Orientation::VERTICAL, estimate_text_bounds));
    REQUIRE(get_property(document, board.m_stack, "layout.stack").m_value ==
      PropertyValue(true));
    REQUIRE(get_property(document, board.m_stack,
      "layout.stack.orientation").m_value == PropertyValue(
        Choice(1, QStringList({"Horizontal", "Vertical"}))));
    auto& spacing =
      get_property(document, board.m_stack, "layout.stack.spacing");
    REQUIRE(spacing.m_value == PropertyValue(10.0));
    REQUIRE(!spacing.m_is_mixed);
    history.execute(std::make_unique<RepeatGridCommand>(
      std::vector{board.m_cell}));
    auto grid = document.get(board.m_cell).m_parent;
    grid = document.get(grid).m_parent;
    REQUIRE(get_property(document, grid,
      "layout.repeat_grid.column_spacing").m_value == PropertyValue(20.0));
    properties = extract_properties(document, grid);
    REQUIRE(!find_property(properties, "layout.stack"));
  }

  TEST_CASE("editors") {
    auto document = Document();
    auto board = build_board(document);
    auto editors = make_editors();
    auto history = History(document);
    apply(history, editors, "layout.stack", {board.m_stack}, true);
    REQUIRE(document.get(board.m_stack).get_group()->m_stack->m_orientation ==
      Orientation::VERTICAL);
    apply(history, editors, "layout.stack.spacing", {board.m_stack}, 4.0);
    REQUIRE(get_transform(document, board.m_second) ==
      make_translation(0, 24));
    apply(history, editors, "layout.stack.orientation", {board.m_stack},
      Choice(0, QStringList({"Horizontal", "Vertical"})));
    REQUIRE(get_transform(document, board.m_second) ==
      make_translation(104, 24));
    REQUIRE(editors.find("layout.padding.top")->m_minimum == 0);
    history.execute(std::make_unique<RepeatGridCommand>(
      std::vector{board.m_cell}));
    auto grid = document.get(document.get(board.m_cell).m_parent).m_parent;
    apply(history, editors, "layout.repeat_grid.row_spacing", {grid}, 5.0);
    REQUIRE(document.get(grid).get_group()->m_repeat_grid->m_row_spacing ==
      5);
    apply(history, editors, "transform.height", {grid}, 130.0);
    REQUIRE(document.get(grid).get_group()->m_repeat_grid->m_row_count == 3);
    REQUIRE(document.get(grid).m_children.size() == 3);
  }

  TEST_CASE("inspector") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    editor.execute(std::make_unique<SetStackCommand>(
      std::vector{board.m_stack}, Orientation::VERTICAL, estimate_text_bounds));
    auto inspector = PropertyInspector();
    inspector.set_property_editors(make_editors());
    inspector.set_editor(&editor);
    editor.get_selection().select(board.m_stack);
    REQUIRE(inspector.find_field("layout.stack"));
    REQUIRE(inspector.find_field("layout.padding"));
    auto line =
      qobject_cast<QLineEdit*>(inspector.find_field("layout.stack.spacing"));
    REQUIRE(line);
    REQUIRE(line->text() == "10");
    line->setText("12");
    Q_EMIT line->editingFinished();
    REQUIRE(get_transform(document, board.m_second) ==
      make_translation(0, 32));
  }

  TEST_CASE("saving") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    history.execute(std::make_unique<SetStackCommand>(
      std::vector{board.m_stack}, Orientation::VERTICAL, estimate_text_bounds));
    auto made = history.execute(std::make_unique<RepeatGridCommand>(
      std::vector{board.m_cell}));
    auto grid = static_cast<const RepeatGridCommand&>(*made).get_grid();
    history.execute(
      std::make_unique<ResizeRepeatGridCommand>(grid, Size(100, 40)));
    auto package = XdPackage(write_xd(document));
    auto stack = get_ux(find_written(package, board.m_stack));
    REQUIRE(stack.value("isContentAware"_L1).toBool());
    REQUIRE(stack.value("contentStackType"_L1).toString() == "vertical"_L1);
    REQUIRE(get_ux(find_written(package, board.m_first)).value(
      "contentStackMarginAfter"_L1).toDouble() == 10);
    REQUIRE(!get_ux(find_written(package, board.m_second)).contains(
      "contentStackMarginAfter"_L1));
    auto repeat = get_ux(find_written(package, grid)).value(
      "repeatGrid"_L1).toObject();
    REQUIRE(repeat.value("columns"_L1).toInt() == 2);
    REQUIRE(repeat.value("width"_L1).toDouble() == 100);
    REQUIRE(repeat.value("cellWidth"_L1).toDouble() == 40);
    auto reopened = Document();
    import_xd(package, reopened);
    reopened.set_opaque(document.get_opaque());
    REQUIRE(is_equal(document, reopened));
  }
}

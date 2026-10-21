#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  NodeHeader make_header(std::string name, const Matrix& transform = Matrix()) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_has_custom_name = !header.m_name.empty();
    header.m_transform = transform;
    return header;
  }

  Shape make_rectangle(double width, double height, const Color& color) {
    auto shape = Shape();
    shape.m_geometry =
      RectangleShape(Rect(0, 0, width, height), CornerRadii(0, 0, 0, 0));
    shape.m_style.m_fill = SolidFill(color);
    return shape;
  }

  Text make_label(const std::u16string& content) {
    auto text = Text();
    text.m_content = Shared(content);
    text.m_frame = PositionedFrame();
    text.m_base_style.m_font = Font("Roboto", "Regular", "Roboto-Regular", 14);
    text.m_style.m_fill = SolidFill(text.m_base_style.m_color);
    text.m_runs.push_back(TextRun(content.size(), text.m_base_style));
    return text;
  }

  std::vector<std::string> filter(std::vector<std::string> differences) {
    std::erase(differences, "document opaque data differs");
    return differences;
  }

  std::string join(const std::vector<std::string>& differences) {
    auto text = std::string();
    for(auto& difference : differences) {
      text += difference + '\n';
    }
    return text;
  }

  NodeId find_copy(const Document& document, const NodeId& instance,
      const NodeId& source) {
    for(auto& entry : get_instance_role(document.get(instance))->m_sync_map) {
      if(entry.second == source) {
        return entry.first;
      }
    }
    return NodeId();
  }

  void require_round_trip(const Document& document) {
    auto imported = Document();
    auto report = import_xd(XdPackage(write_xd(document)), imported);
    REQUIRE(report.m_warnings.empty());
    REQUIRE(join(filter(find_differences(document, imported))) == "");
    auto rewritten = Document();
    import_xd(XdPackage(write_xd(imported)), rewritten);
    REQUIRE(join(find_differences(imported, rewritten)) == "");
  }
}

TEST_SUITE("ComponentRoundTrip") {
  TEST_CASE("edits") {
    auto document = Document();
    auto artboard = NodeId();
    auto group = NodeId();
    auto background = NodeId();
    auto label = NodeId();
    {
      auto transaction = Transaction(document);
      auto frame = Artboard();
      frame.m_frame = Rect(0, 0, 400, 300);
      frame.m_background = SolidFill(make_color(255, 255, 255));
      artboard = transaction.append(NodeId(), make_header("Home"), frame);
      group = transaction.append(
        artboard, make_header("Button", make_translation(20, 20)), Group());
      background = transaction.append(group, make_header("Background"),
        make_rectangle(80, 30, make_color(255, 0, 0)));
      label = transaction.append(group,
        make_header("Label", make_translation(10, 20)), make_label(u"OK"));
      transaction.commit();
    }
    auto editor = Editor(document);
    editor.get_selection().select(group);
    auto make = editor.execute(
      std::make_unique<MakeComponentCommand>(std::vector{group}));
    auto component =
      static_cast<const MakeComponentCommand&>(*make).get_component();
    group = static_cast<const MakeComponentCommand&>(*make).get_main();
    auto master = document.get_resources().m_components.at(component).m_master;
    auto master_background = document.get(master).m_children.front();
    auto master_label = document.get(master).m_children.back();
    auto plain = static_cast<const InsertInstanceCommand&>(*editor.execute(
      std::make_unique<InsertInstanceCommand>(component, artboard,
        make_translation(200, 20)))).get_instance();
    auto edited = static_cast<const InsertInstanceCommand&>(*editor.execute(
      std::make_unique<InsertInstanceCommand>(component, artboard,
        make_translation(200, 100)))).get_instance();
    auto edited_background = find_copy(document, edited, master_background);
    editor.get_selection().select(edited_background);
    editor.execute(std::make_unique<SetFillCommand>(
      std::vector{edited_background}, Fill(SolidFill(make_color(0, 255, 0)))));
    auto edited_label = find_copy(document, edited, master_label);
    editor.get_selection().select(edited_label);
    editor.execute(std::make_unique<EditTextCommand>(
      edited_label, make_label(u"Cancel")));
    editor.get_selection().select(background);
    editor.execute(std::make_unique<SetOpacityCommand>(
      std::vector{background}, 0.5));
    auto state = static_cast<const AddStateCommand&>(*editor.execute(
      std::make_unique<AddStateCommand>(
        component, "Hover State", StateKind::HOVER))).get_state();
    auto main_background = document.get(group).m_children.front();
    editor.get_selection().select(main_background);
    editor.execute(std::make_unique<SetFillCommand>(
      std::vector{main_background}, Fill(SolidFill(make_color(0, 0, 255)))));
    editor.execute(std::make_unique<SetInstanceStateCommand>(
      std::vector{plain, edited}, state));
    auto& role = *get_instance_role(document.get(edited));
    REQUIRE(role.m_state == state);
    REQUIRE(!role.m_overrides.m_properties.empty());
    REQUIRE(get_instance_role(document.get(group))->m_is_main);
    require_round_trip(document);
    auto written = XdPackage(write_xd(document));
    auto imported = Document();
    import_xd(written, imported);
    REQUIRE(get_instance_role(imported.get(group))->m_is_main);
    REQUIRE(get_instance_role(imported.get(edited))->m_overrides ==
      role.m_overrides);
    REQUIRE(imported.get_resources().m_components.at(component).
      m_states.size() == 1);
  }

  TEST_CASE("deleted") {
    auto document = Document();
    auto group = NodeId();
    {
      auto transaction = Transaction(document);
      group = transaction.append(NodeId(), make_header("Card"), Group());
      transaction.append(group, make_header("Fill"),
        make_rectangle(40, 40, make_color(10, 20, 30)));
      transaction.append(group, make_header("Label"), make_label(u"Card"));
      transaction.commit();
    }
    auto editor = Editor(document);
    auto make = editor.execute(
      std::make_unique<MakeComponentCommand>(std::vector{group}));
    auto inner =
      static_cast<const MakeComponentCommand&>(*make).get_component();
    group = static_cast<const MakeComponentCommand&>(*make).get_main();
    auto nested = static_cast<const InsertInstanceCommand&>(*editor.execute(
      std::make_unique<InsertInstanceCommand>(inner, NodeId(),
        make_translation(100, 0)))).get_instance();
    make = editor.execute(
      std::make_unique<MakeComponentCommand>(std::vector{nested}));
    auto outer =
      static_cast<const MakeComponentCommand&>(*make).get_component();
    auto outer_instance = static_cast<const InsertInstanceCommand&>(
      *editor.execute(std::make_unique<InsertInstanceCommand>(outer, NodeId(),
        make_translation(300, 0)))).get_instance();
    editor.get_selection().select(group);
    editor.execute(std::make_unique<DeleteComponentsCommand>(
      std::vector{inner}, editor.get_selection().get_nodes()));
    REQUIRE(!document.find(group));
    auto converted = editor.get_selection().get_primary();
    REQUIRE(!converted.is_nil());
    REQUIRE(converted != group);
    REQUIRE(!get_instance_role(document.get(converted)));
    auto outer_master =
      document.get_resources().m_components.at(outer).m_master;
    auto copy = document.get(outer_instance).m_children.front();
    auto resolution = resolve_reference(document.get_nodes(), copy);
    REQUIRE(resolution);
    REQUIRE(resolution->m_source ==
      document.get(outer_master).m_children.front());
    require_round_trip(document);
    auto imported = Document();
    import_xd(XdPackage(write_xd(document)), imported);
    REQUIRE(imported.find(converted));
    REQUIRE(!imported.find(group));
    REQUIRE(resolve_reference(imported.get_nodes(), copy));
    REQUIRE(editor.undo());
    REQUIRE(document.find(group));
    REQUIRE(get_instance_role(document.get(group))->m_component == inner);
    REQUIRE(!document.find(converted));
  }

  TEST_CASE("ungrouped") {
    auto document = Document();
    auto group = NodeId();
    {
      auto transaction = Transaction(document);
      group = transaction.append(NodeId(), make_header("Card"), Group());
      transaction.append(group, make_header("Fill"),
        make_rectangle(40, 40, make_color(10, 20, 30)));
      transaction.commit();
    }
    auto editor = Editor(document);
    auto make = editor.execute(
      std::make_unique<MakeComponentCommand>(std::vector{group}));
    auto component =
      static_cast<const MakeComponentCommand&>(*make).get_component();
    group = static_cast<const MakeComponentCommand&>(*make).get_main();
    auto instance = static_cast<const InsertInstanceCommand&>(*editor.execute(
      std::make_unique<InsertInstanceCommand>(component, NodeId(),
        make_translation(100, 0)))).get_instance();
    editor.execute(std::make_unique<UngroupComponentCommand>(
      std::vector{instance}));
    REQUIRE(!document.find(instance));
    require_round_trip(document);
  }
}

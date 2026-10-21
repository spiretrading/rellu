#include <memory>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Ui/ComponentPropertyEditors.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const PropertyDescriptor& get(
      const std::vector<PropertyDescriptor>& properties, const char* path) {
    auto property = find_property(properties, path);
    REQUIRE(property);
    return *property;
  }

  void apply(History& history, const PropertyEditors& editors,
      const char* path, const NodeId& node, const PropertyValue& value) {
    auto editor = editors.find(path);
    REQUIRE(editor);
    auto command = editor->m_setter(
      history.get_document(), std::vector{node}, value);
    REQUIRE(command);
    REQUIRE(history.execute(std::move(command)));
  }
}

TEST_SUITE("ComponentPropertyEditors") {
  TEST_CASE("instance") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_instance);
    REQUIRE(get(properties, "component").m_label == "Component (Instance)");
    REQUIRE(!get(properties, "component.name").m_is_read_only);
    auto& state = get(properties, "component.state");
    REQUIRE(std::get<Choice>(state.m_value) ==
      Choice(0, QStringList({"Default State"})));
    REQUIRE(!find_property(properties, "component.main"));
    REQUIRE(!find_property(properties, "component.state_name"));
    auto editors = PropertyEditors();
    add_component_property_editors(editors);
    auto history = History(document);
    auto& setter = editors.find("component.name")->m_setter;
    REQUIRE(!setter(document, std::vector{ids.m_instance}, QString("Button")));
    REQUIRE(!setter(document, std::vector{ids.m_instance}, QString(" ")));
    apply(history, editors, "component.name", ids.m_instance,
      QString("Renamed"));
    REQUIRE(history.get_undo_label() == "Rename");
    auto& header = document.get(ids.m_instance).m_header;
    REQUIRE(header.m_name == "Renamed");
    REQUIRE(header.m_has_custom_name);
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_name == "Button");
    REQUIRE(to_text(get(extract_properties(document, ids.m_instance),
      "component.name").m_value) == "Renamed");
  }

  TEST_CASE("grouped_main_name") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_background).m_header;
      header.m_name = "Forms / Button";
      transaction.replace_header(header);
      transaction.commit();
    }
    auto history = History(document);
    auto make = history.execute(std::make_unique<MakeComponentCommand>(
      std::vector{ids.m_background}));
    auto main = static_cast<const MakeComponentCommand&>(*make).get_main();
    REQUIRE(document.get(main).m_header.m_name == "Button");
    REQUIRE(document.get(ids.m_background).m_header.m_name ==
      "Forms / Button");
    auto properties = extract_properties(document, main);
    REQUIRE(get(properties, "component").m_label == "Component (Main)");
    REQUIRE(to_text(get(properties, "component.name").m_value) ==
      "Forms / Button");
  }

  TEST_CASE("several_instances") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto history = History(document);
    auto copy = history.execute(std::make_unique<InsertInstanceCommand>(
      ids.m_component, ids.m_artboard, make_translation(0, 100)));
    auto other = static_cast<const InsertInstanceCommand&>(*copy).
      get_instance();
    auto properties = extract_common_properties(
      document, std::vector{ids.m_instance, other});
    auto& section = get(properties, "component");
    REQUIRE(section.m_label == "Component");
    REQUIRE(section.m_children.empty());
  }

  TEST_CASE("main_and_states") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto history = History(document);
    auto make = history.execute(
      std::make_unique<MakeComponentCommand>(std::vector{ids.m_card}));
    auto component =
      static_cast<const MakeComponentCommand&>(*make).get_component();
    ids.m_card = static_cast<const MakeComponentCommand&>(*make).get_main();
    auto add = history.execute(std::make_unique<AddStateCommand>(
      component, "Pressed", StateKind::NEW));
    auto state = static_cast<const AddStateCommand&>(*add).get_state();
    auto properties = extract_properties(document, ids.m_card);
    REQUIRE(get(properties, "component").m_label == "Component (Main)");
    REQUIRE(!get(properties, "component.name").m_is_read_only);
    REQUIRE(get(properties, "component.main").m_is_read_only);
    REQUIRE(std::get<Choice>(get(properties, "component.state").m_value) ==
      Choice(1, QStringList({"Default State", "Pressed"})));
    REQUIRE(to_text(get(properties, "component.state_name").m_value) ==
      "Pressed");
    auto editors = PropertyEditors();
    add_component_property_editors(editors);
    apply(history, editors, "component.state_name", ids.m_card,
      QString("Active"));
    REQUIRE(document.get_resources().m_components.at(component).m_states.
      front().m_name == "Active");
    apply(history, editors, "component.name", ids.m_card, QString("Tile"));
    REQUIRE(document.get_resources().m_components.at(component).m_name ==
      "Tile");
    REQUIRE(document.get(ids.m_card).m_header.m_name == "Tile");
    apply(history, editors, "component.state", ids.m_card,
      Choice(0, QStringList({"Default State", "Active"})));
    REQUIRE(get_instance_role(document.get(ids.m_card))->m_state.is_nil());
    apply(history, editors, "component.state", ids.m_card,
      Choice(1, QStringList({"Default State", "Active"})));
    REQUIRE(get_instance_role(document.get(ids.m_card))->m_state == state);
  }
}

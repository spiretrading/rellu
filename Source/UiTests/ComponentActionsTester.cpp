#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QWidget>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Ui/ComponentActions.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    std::unique_ptr<ComponentActions> m_component_actions;
    std::vector<NodeId> m_revealed;
    std::vector<ComponentId> m_components;

    Fixture()
        : m_actions(m_window) {
      add_xd_menus(m_actions);
      m_component_actions = std::make_unique<ComponentActions>(m_actions);
      m_component_actions->set_reveal_handler([this] (const auto& id) {
        m_revealed.push_back(id);
      });
      m_component_actions->set_component_handler([this] (const auto& id) {
        m_components.push_back(id);
      });
    }

    bool is_enabled(const std::string& name) const {
      return m_actions.get(name).isEnabled();
    }
  };
}

TEST_SUITE("ComponentActions") {
  TEST_CASE("edit_main_component") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_component_actions->set_editor(&editor);
    REQUIRE(!fixture.is_enabled("object.edit_main_component"));
    editor.get_selection().select(ids.m_reference);
    REQUIRE(fixture.is_enabled("object.edit_main_component"));
    REQUIRE(fixture.is_enabled("object.reveal_component"));
    REQUIRE(!fixture.is_enabled("object.previous_instance"));
    REQUIRE(!fixture.is_enabled("component.add_state"));
    REQUIRE(fixture.m_actions.trigger("object.edit_main_component"));
    auto main = find_main_component(document, ids.m_component);
    REQUIRE(!main.is_nil());
    REQUIRE(document.is_on_canvas(main));
    REQUIRE(editor.get_selection().get_primary() == main);
    REQUIRE(fixture.m_revealed == std::vector{main});
    REQUIRE(!fixture.is_enabled("object.edit_main_component"));
    REQUIRE(fixture.is_enabled("object.previous_instance"));
    REQUIRE(fixture.is_enabled("component.add_state"));
    REQUIRE(fixture.m_actions.trigger("object.previous_instance"));
    REQUIRE(editor.get_selection().get_primary() == ids.m_instance);
    REQUIRE(!fixture.is_enabled("object.previous_instance"));
    REQUIRE(fixture.m_actions.trigger("object.edit_main_component"));
    REQUIRE(editor.get_selection().get_primary() == main);
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(fixture.m_actions.trigger("object.reveal_component"));
    REQUIRE(fixture.m_components == std::vector{ids.m_component});
    fixture.m_component_actions->set_editor(nullptr);
    REQUIRE(!fixture.is_enabled("object.reveal_component"));
  }

  TEST_CASE("states") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_component_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_instance);
    fixture.m_actions.trigger("object.edit_main_component");
    auto main = find_main_component(document, ids.m_component);
    REQUIRE(!fixture.is_enabled("component.delete_state"));
    REQUIRE(fixture.m_actions.trigger("component.add_hover_state"));
    auto& component = document.get_resources().m_components.at(
      ids.m_component);
    REQUIRE(component.m_states.size() == 1);
    REQUIRE(component.m_states.front().m_name == "Hover State");
    REQUIRE(get_instance_role(document.get(main))->m_state ==
      component.m_states.front().m_id);
    REQUIRE(fixture.is_enabled("component.delete_state"));
    REQUIRE(fixture.m_actions.trigger("component.add_hover_state"));
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_states.back().m_name == "Hover State 2");
    REQUIRE(fixture.m_actions.trigger("component.delete_state"));
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_states.size() == 1);
    REQUIRE(get_instance_role(document.get(main))->m_state.is_nil());
    REQUIRE(!fixture.is_enabled("component.delete_state"));
    REQUIRE(fixture.m_actions.trigger("component.add_state"));
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_states.back().m_name == "State 3");
    REQUIRE(fixture.m_actions.trigger("component.add_state"));
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_states.back().m_name == "State 4");
  }

  TEST_CASE("new_state_name") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_component_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_instance);
    fixture.m_actions.trigger("object.edit_main_component");
    REQUIRE(fixture.m_actions.trigger("component.add_state"));
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_states.back().m_name == "State 2");
  }

  TEST_CASE("linked") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto component =
        document.get_resources().m_components.at(ids.m_component);
      component.m_link = ComponentLink("cloud-asset://library", "Library");
      transaction.replace_component(component);
      transaction.commit();
    }
    auto editor = Editor(document);
    fixture.m_component_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_instance);
    REQUIRE(!fixture.is_enabled("object.edit_main_component"));
    REQUIRE(fixture.is_enabled("object.reveal_component"));
    REQUIRE(!fixture.m_component_actions->edit_main_component(
      ids.m_component));
  }
}

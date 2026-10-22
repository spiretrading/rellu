#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QKeySequence>
#include <QToolButton>
#include <QWidget>
#include "Rellu/Commands/AlignCommands.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto ALIGN_NAMES = std::vector<std::string>({"object.align.left",
    "object.align.center", "object.align.right", "object.align.top",
    "object.align.middle", "object.align.bottom"});
  const auto DISTRIBUTE_NAMES = std::vector<std::string>({
    "object.distribute.horizontally", "object.distribute.vertically"});

  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    CommandRegistry m_commands;
    std::unique_ptr<EditActions> m_edit_actions;

    Fixture()
        : m_actions(m_window) {
      add_xd_menus(m_actions);
      add_standard_commands(m_commands);
      add_align_commands(m_commands);
      m_edit_actions = std::make_unique<EditActions>(m_actions, m_commands);
    }

    bool is_enabled(const std::vector<std::string>& names) const {
      for(auto& name : names) {
        if(!m_actions.get(name).isEnabled()) {
          return false;
        }
      }
      return true;
    }

    bool is_disabled(const std::vector<std::string>& names) const {
      for(auto& name : names) {
        if(m_actions.get(name).isEnabled()) {
          return false;
        }
      }
      return true;
    }
  };

  QToolButton* find_button(QWidget& section, const QString& tip) {
    for(auto button : section.findChildren<QToolButton*>()) {
      if(button->accessibleName() == tip) {
        return button;
      }
    }
    return nullptr;
  }
}

TEST_SUITE("AlignActions") {
  TEST_CASE("enabled_states") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    REQUIRE(fixture.is_disabled(ALIGN_NAMES));
    REQUIRE(fixture.is_disabled(DISTRIBUTE_NAMES));
    auto& selection = editor.get_selection();
    selection.select(ids.m_ellipse);
    REQUIRE(fixture.is_enabled(ALIGN_NAMES));
    REQUIRE(fixture.is_disabled(DISTRIBUTE_NAMES));
    selection.select(ids.m_background);
    REQUIRE(fixture.is_disabled(ALIGN_NAMES));
    selection.select(ids.m_loose);
    REQUIRE(fixture.is_disabled(ALIGN_NAMES));
    selection.select(ids.m_artboard);
    REQUIRE(fixture.is_disabled(ALIGN_NAMES));
    selection.select(std::vector{ids.m_ellipse, ids.m_card, ids.m_instance});
    REQUIRE(fixture.is_enabled(ALIGN_NAMES));
    REQUIRE(fixture.is_enabled(DISTRIBUTE_NAMES));
  }

  TEST_CASE("menu_trigger") {
    auto fixture = Fixture();
    auto& actions = fixture.m_actions;
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(actions.get("object.align.right").shortcut() ==
      QKeySequence("Shift+Ctrl+Right"));
    REQUIRE(actions.trigger("object.align.right"));
    REQUIRE(get_world_bounds(document, ids.m_ellipse)->get_right() == 475);
    REQUIRE(actions.get("edit.undo").text() == "Undo Align");
    REQUIRE(actions.trigger("edit.undo"));
    REQUIRE(get_world_bounds(document, ids.m_ellipse)->get_right() == 110);
  }

  TEST_CASE("inspector_buttons") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_ellipse);
    auto inspector = PropertyInspector();
    inspector.set_actions(&fixture.m_actions);
    inspector.set_document(&document);
    inspector.set_current(ids.m_ellipse);
    auto align = inspector.find_section("align");
    REQUIRE(align);
    auto center = find_button(*align, "Align Center (Horizontally)");
    auto distribute = find_button(*align, "Distribute Horizontally");
    REQUIRE(center);
    REQUIRE(distribute);
    REQUIRE(center->isEnabled());
    REQUIRE(!distribute->isEnabled());
    center->click();
    REQUIRE(get_world_bounds(document, ids.m_ellipse)->get_center().m_x ==
      287.5);
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }
}

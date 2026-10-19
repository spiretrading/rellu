#include <memory>
#include <string>
#include <doctest/doctest.h>
#include <QWidget>
#include "Rellu/Ui/TextActions.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    std::unique_ptr<TextActions> m_text_actions;

    Fixture()
        : m_actions(m_window) {
      add_xd_menus(m_actions);
      m_text_actions = std::make_unique<TextActions>(
        m_actions, get_default_font_library());
    }

    bool is_enabled(const std::string& name) const {
      return m_actions.get(name).isEnabled();
    }

    bool is_checked(const std::string& name) const {
      return m_actions.get(name).isChecked();
    }
  };

  const TextStyle& get_style(const Document& document, const NodeId& id) {
    return document.get(id).get_text()->m_runs.front().m_style;
  }
}

TEST_SUITE("TextActions") {
  TEST_CASE("whole_text") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_text_actions->set_editor(&editor);
    REQUIRE(!fixture.is_enabled("object.text.bold"));
    editor.get_selection().select(ids.m_label);
    REQUIRE(fixture.is_enabled("object.text.bold"));
    REQUIRE(!fixture.is_checked("object.text.underline"));
    REQUIRE(fixture.m_actions.trigger("object.text.underline"));
    REQUIRE(get_style(document, ids.m_label).m_is_underlined);
    REQUIRE(fixture.is_checked("object.text.underline"));
    REQUIRE(editor.get_history().get_undo_label() == "Underline");
    REQUIRE(fixture.m_actions.trigger("object.text.underline"));
    REQUIRE(!get_style(document, ids.m_label).m_is_underlined);
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    REQUIRE(get_style(document, ids.m_label).m_font ==
      Font("Roboto", "Bold", "Roboto-Bold", 14));
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    REQUIRE(get_style(document, ids.m_label).m_font ==
      Font("Roboto", "Regular", "Roboto-Regular", 14));
    REQUIRE(fixture.m_actions.trigger("object.text.italic"));
    REQUIRE(get_style(document, ids.m_label).m_font ==
      Font("Roboto", "Italic", "Roboto-Italic", 14));
    REQUIRE(fixture.m_actions.trigger("object.text.increase_size"));
    REQUIRE(get_style(document, ids.m_label).m_font.m_size == 15);
    REQUIRE(fixture.m_actions.trigger("object.text.decrease_size"));
    REQUIRE(fixture.m_actions.trigger("object.text.decrease_size"));
    REQUIRE(get_style(document, ids.m_label).m_font.m_size == 13);
    REQUIRE(fixture.m_actions.trigger("object.text.title_case"));
    REQUIRE(get_style(document, ids.m_label).m_transform ==
      TextTransform::TITLE_CASE);
    REQUIRE(fixture.is_checked("object.text.title_case"));
    REQUIRE(!fixture.is_checked("object.text.uppercase"));
    REQUIRE(fixture.m_actions.trigger("object.text.uppercase"));
    REQUIRE(get_style(document, ids.m_label).m_transform ==
      TextTransform::UPPERCASE);
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(!fixture.is_enabled("object.text.bold"));
    REQUIRE(!fixture.is_checked("object.text.uppercase"));
  }

  TEST_CASE("editing") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_text_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_label);
    auto& selection = editor.get_text_selection();
    selection.set(ids.m_label, 0, 5);
    REQUIRE(fixture.m_actions.trigger("object.text.superscript"));
    auto& runs = document.get(ids.m_label).get_text()->m_runs;
    REQUIRE(runs.size() == 2);
    REQUIRE(runs.front().m_style.m_script == TextScript::SUPERSCRIPT);
    REQUIRE(runs.back().m_style.m_script == TextScript::NORMAL);
    REQUIRE(fixture.is_checked("object.text.superscript"));
    selection.set(ids.m_label, 8, 8);
    REQUIRE(!fixture.is_checked("object.text.superscript"));
    REQUIRE(fixture.m_actions.trigger("object.text.subscript"));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(selection.get_pending_style()->m_script == TextScript::SUBSCRIPT);
    REQUIRE(fixture.is_checked("object.text.subscript"));
  }
}

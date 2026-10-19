#include <memory>
#include <string>
#include <utility>
#include <vector>
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

  void set_runs(Document& document, const NodeId& id,
      std::vector<TextRun> runs) {
    auto transaction = Transaction(document);
    auto text = *document.get(id).get_text();
    text.m_base_style = runs.front().m_style;
    text.m_runs = std::move(runs);
    transaction.replace_payload(id, std::move(text));
    transaction.commit();
  }

  void set_font(Document& document, const NodeId& id, Font font) {
    auto style = get_style(document, id);
    style.m_font = std::move(font);
    auto size = document.get(id).get_text()->m_content->size();
    set_runs(document, id, {TextRun(size, style)});
  }

  TextRun make_run(std::size_t length, Font font) {
    auto run = TextRun();
    run.m_length = length;
    run.m_style.m_font = std::move(font);
    return run;
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
    REQUIRE(editor.get_history().get_undo_label() == "Bold");
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    REQUIRE(get_style(document, ids.m_label).m_font ==
      Font("Roboto", "Regular", "Roboto-Regular", 14));
    REQUIRE(fixture.m_actions.trigger("object.text.italic"));
    REQUIRE(get_style(document, ids.m_label).m_font ==
      Font("Roboto", "Italic", "Roboto-Italic", 14));
    REQUIRE(editor.get_history().get_undo_label() == "Italic");
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    REQUIRE(get_style(document, ids.m_label).m_font ==
      Font("Roboto", "Bold Italic", "Roboto-BoldItalic", 14));
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    REQUIRE(get_style(document, ids.m_label).m_font ==
      Font("Roboto", "Italic", "Roboto-Italic", 14));
    REQUIRE(fixture.m_actions.trigger("object.text.increase_size"));
    REQUIRE(get_style(document, ids.m_label).m_font.m_size == 15);
    REQUIRE(editor.get_history().get_undo_label() == "Increase Font Size");
    REQUIRE(fixture.m_actions.trigger("object.text.decrease_size"));
    REQUIRE(fixture.m_actions.trigger("object.text.decrease_size"));
    REQUIRE(get_style(document, ids.m_label).m_font.m_size == 13);
    REQUIRE(editor.get_history().get_undo_label() == "Decrease Font Size");
    REQUIRE(fixture.m_actions.trigger("object.text.title_case"));
    REQUIRE(get_style(document, ids.m_label).m_transform ==
      TextTransform::TITLE_CASE);
    REQUIRE(editor.get_history().get_undo_label() == "Title Case");
    REQUIRE(fixture.is_checked("object.text.title_case"));
    REQUIRE(!fixture.is_checked("object.text.uppercase"));
    REQUIRE(fixture.m_actions.trigger("object.text.uppercase"));
    REQUIRE(get_style(document, ids.m_label).m_transform ==
      TextTransform::UPPERCASE);
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(!fixture.is_enabled("object.text.bold"));
    REQUIRE(!fixture.is_checked("object.text.uppercase"));
  }

  TEST_CASE("weights") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_text_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_label);
    auto bold = [&] (const char* style) {
      set_font(document, ids.m_label, Font("Segoe UI", style, "", 14));
      fixture.m_actions.trigger("object.text.bold");
      return get_style(document, ids.m_label).m_font.m_style;
    };
    REQUIRE(bold("Light") == "Bold");
    REQUIRE(bold("Semilight") == "Bold");
    REQUIRE(bold("Regular") == "Bold");
    REQUIRE(bold("Semibold") == "Semibold");
    REQUIRE(bold("Semibold Italic") == "Semibold Italic");
    REQUIRE(bold("Bold") == "Regular");
    REQUIRE(bold("Black") == "Regular");
    REQUIRE(bold("Black Italic") == "Italic");
    set_runs(document, ids.m_label, {
      make_run(5, Font("Segoe UI", "Semibold", "", 14)),
      make_run(18, Font("Segoe UI", "Regular", "", 14))});
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    auto& runs = document.get(ids.m_label).get_text()->m_runs;
    REQUIRE(runs.front().m_style.m_font.m_style == "Semibold");
    REQUIRE(runs.back().m_style.m_font.m_style == "Bold");
    set_runs(document, ids.m_label, {
      make_run(5, Font("Segoe UI", "Bold", "", 14)),
      make_run(18, Font("Segoe UI", "Black", "", 14))});
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    REQUIRE(document.get(ids.m_label).get_text()->m_runs.size() == 1);
    REQUIRE(get_style(document, ids.m_label).m_font.m_style == "Regular");
  }

  TEST_CASE("mixed_runs") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_text_actions->set_editor(&editor);
    set_runs(document, ids.m_label, {
      make_run(5, Font("Roboto", "Regular", "Roboto-Regular", 30)),
      make_run(18, Font("Roboto", "Medium", "Roboto-Medium", 14))});
    editor.get_selection().select(ids.m_label);
    REQUIRE(fixture.m_actions.trigger("object.text.increase_size"));
    auto& stepped = document.get(ids.m_label).get_text()->m_runs;
    REQUIRE(stepped.size() == 2);
    REQUIRE(stepped[0].m_style.m_font.m_size == 31);
    REQUIRE(stepped[1].m_style.m_font.m_size == 15);
    REQUIRE(editor.get_history().get_undo_label() == "Increase Font Size");
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    auto& bold = document.get(ids.m_label).get_text()->m_runs;
    REQUIRE(bold.size() == 2);
    REQUIRE(bold[0].m_style.m_font ==
      Font("Roboto", "Bold", "Roboto-Bold", 31));
    REQUIRE(bold[1].m_style.m_font ==
      Font("Roboto", "Bold", "Roboto-Bold", 15));
    REQUIRE(fixture.m_actions.trigger("object.text.bold"));
    REQUIRE(document.get(ids.m_label).get_text()->m_runs.front().m_style.
      m_font.m_style == "Regular");
    set_runs(document, ids.m_label, {
      make_run(5, Font("Roboto", "Italic", "Roboto-Italic", 14)),
      make_run(18, Font("Roboto", "Medium", "Roboto-Medium", 14))});
    REQUIRE(fixture.m_actions.trigger("object.text.italic"));
    auto& italic = document.get(ids.m_label).get_text()->m_runs;
    REQUIRE(italic.front().m_style.m_font.m_style == "Italic");
    REQUIRE(italic.back().m_style.m_font.m_style == "Medium Italic");
    REQUIRE(fixture.m_actions.trigger("object.text.italic"));
    auto& upright = document.get(ids.m_label).get_text()->m_runs;
    REQUIRE(upright.front().m_style.m_font.m_style == "Regular");
    REQUIRE(upright.back().m_style.m_font.m_style == "Medium");
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

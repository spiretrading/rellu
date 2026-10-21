#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeySequence>
#include <QWidget>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    CommandRegistry m_commands;
    std::unique_ptr<EditActions> m_edit_actions;

    Fixture()
        : m_actions(m_window) {
      QGuiApplication::clipboard()->clear();
      add_xd_menus(m_actions);
      add_standard_commands(m_commands);
      m_edit_actions = std::make_unique<EditActions>(m_actions, m_commands);
    }

    bool is_enabled(const std::string& name) const {
      return m_actions.get(name).isEnabled();
    }
  };
}

TEST_SUITE("EditActions") {
  TEST_CASE("enabled_states") {
    auto fixture = Fixture();
    auto& actions = fixture.m_actions;
    for(auto& name : actions.get_names()) {
      REQUIRE(!fixture.is_enabled(name));
    }
    auto document = Document();
    auto ids = build_test_document(document);
    auto video = Shape();
    video.m_geometry = RectangleShape(Rect(0, 0, 64, 48));
    video.m_video = Video(ImageFill(), generate_node_id(), false);
    auto video_id = NodeId();
    {
      auto transaction = Transaction(document);
      video_id =
        transaction.append(ids.m_artboard, make_header("Video"), video);
      transaction.commit();
    }
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    REQUIRE(!fixture.is_enabled("edit.undo"));
    REQUIRE(!fixture.is_enabled("edit.redo"));
    REQUIRE(!fixture.is_enabled("edit.delete"));
    REQUIRE(!fixture.is_enabled("edit.copy"));
    REQUIRE(!fixture.is_enabled("edit.paste"));
    REQUIRE(!fixture.is_enabled("object.group"));
    REQUIRE(fixture.is_enabled("edit.select_all"));
    REQUIRE(!fixture.is_enabled("edit.deselect_all"));
    REQUIRE(!fixture.is_enabled("object.path.add"));
    REQUIRE(actions.get("edit.delete").shortcuts().contains(
      QKeySequence(Qt::Key_Backspace)));
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(fixture.is_enabled("edit.deselect_all"));
    REQUIRE(fixture.is_enabled("edit.delete"));
    REQUIRE(fixture.is_enabled("edit.cut"));
    REQUIRE(fixture.is_enabled("edit.copy"));
    REQUIRE(fixture.is_enabled("edit.duplicate"));
    REQUIRE(fixture.is_enabled("object.group"));
    REQUIRE(!fixture.is_enabled("object.ungroup"));
    REQUIRE(fixture.is_enabled("arrange.bring_to_front"));
    REQUIRE(actions.get("object.toggle_lock").text() == "Lock");
    REQUIRE(actions.get("object.toggle_visibility").text() == "Hide");
    REQUIRE(fixture.is_enabled("edit.paste_appearance"));
    editor.get_selection().select(video_id);
    REQUIRE(!fixture.is_enabled("edit.paste_appearance"));
    editor.get_selection().select(ids.m_card);
    REQUIRE(fixture.is_enabled("object.ungroup"));
    editor.get_selection().select(ids.m_artboard);
    REQUIRE(!fixture.is_enabled("object.group"));
    REQUIRE(fixture.is_enabled("edit.delete"));
    fixture.m_edit_actions->set_editor(nullptr);
    REQUIRE(!fixture.is_enabled("edit.delete"));
    REQUIRE(!fixture.is_enabled("edit.select_all"));
  }

  TEST_CASE("localized_history") {
    auto get_texts = [] (Language language) {
      set_language(language);
      auto fixture = Fixture();
      auto document = Document();
      auto ids = build_test_document(document);
      auto editor = Editor(document);
      fixture.m_edit_actions->set_editor(&editor);
      auto shape = Shape();
      shape.m_geometry = RectangleShape(Rect(0, 0, 10, 10));
      editor.execute(std::make_unique<InsertNodeCommand>("Draw Rectangle",
        ids.m_artboard, make_header("Rectangle 1"), shape));
      auto undo = fixture.m_actions.get("edit.undo").text();
      editor.undo();
      auto redo = fixture.m_actions.get("edit.redo").text();
      set_language(Language::ENGLISH);
      return std::pair(undo, redo);
    };
    auto german = get_texts(Language::GERMAN);
    REQUIRE(german.first == QString::fromUtf8(
      "\xE2\x80\x9ERechteck zeichnen\xE2\x80\x9C r\xC3\xBC" "ckg\xC3\xA4"
      "ngig machen"));
    REQUIRE(german.second == QString::fromUtf8(
      "\xE2\x80\x9ERechteck zeichnen\xE2\x80\x9C wiederholen"));
    auto japanese = get_texts(Language::JAPANESE);
    REQUIRE(japanese.first == QString::fromUtf8("\xE9\x95\xB7\xE6\x96\xB9\xE5"
      "\xBD\xA2\xE3\x82\x92\xE6\x8F\x8F\xE7\x94\xBB\xE3\x81\xAE\xE5\x8F\x96"
      "\xE3\x82\x8A\xE6\xB6\x88\xE3\x81\x97"));
    auto english = get_texts(Language::ENGLISH);
    REQUIRE(english.first == "Undo Draw Rectangle");
    REQUIRE(english.second == "Redo Draw Rectangle");
  }

  TEST_CASE("history") {
    auto fixture = Fixture();
    auto& actions = fixture.m_actions;
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(actions.get("edit.undo").text() == "Undo");
    REQUIRE(actions.trigger("object.group"));
    REQUIRE(fixture.is_enabled("edit.undo"));
    REQUIRE(actions.get("edit.undo").text() == "Undo Group");
    REQUIRE(fixture.is_enabled("object.ungroup"));
    REQUIRE(actions.trigger("object.toggle_lock"));
    REQUIRE(document.get(editor.get_selection().get_primary()).m_header.
      m_is_locked);
    REQUIRE(actions.get("object.toggle_lock").text() == "Unlock");
    REQUIRE(actions.get("edit.undo").text() == "Undo Lock");
    REQUIRE(actions.trigger("edit.undo"));
    REQUIRE(actions.get("edit.undo").text() == "Undo Group");
    REQUIRE(actions.get("edit.redo").text() == "Redo Lock");
    REQUIRE(actions.trigger("edit.undo"));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_ellipse}));
    REQUIRE(!fixture.is_enabled("edit.undo"));
    REQUIRE(fixture.is_enabled("edit.redo"));
    REQUIRE(actions.trigger("arrange.send_to_back"));
    REQUIRE(document.get_children(ids.m_artboard).front() == ids.m_ellipse);
    REQUIRE(!fixture.is_enabled("edit.redo"));
  }

  TEST_CASE("clipboard") {
    auto fixture = Fixture();
    auto& actions = fixture.m_actions;
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    editor.get_selection().select(ids.m_card);
    REQUIRE(actions.trigger("edit.copy"));
    REQUIRE(fixture.is_enabled("edit.paste"));
    REQUIRE(actions.trigger("edit.paste"));
    auto copy = editor.get_selection().get_primary();
    REQUIRE(copy != ids.m_card);
    REQUIRE(document.get(copy).m_parent == ids.m_artboard);
    REQUIRE(document.get_children(ids.m_artboard).back() == copy);
    REQUIRE(document.get(copy).m_children.size() == 2);
    REQUIRE(actions.get("edit.undo").text() == "Undo Paste");
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(actions.trigger("edit.cut"));
    REQUIRE(!document.find(ids.m_ellipse));
    REQUIRE(actions.get("edit.undo").text() == "Undo Cut");
    REQUIRE(actions.trigger("edit.paste"));
    auto pasted = editor.get_selection().get_primary();
    REQUIRE(document.get(pasted).get_shape());
    REQUIRE(document.get(pasted).m_parent == ids.m_artboard);
    auto other = Document();
    auto other_editor = Editor(other);
    fixture.m_edit_actions->set_editor(&other_editor);
    REQUIRE(fixture.is_enabled("edit.paste"));
    REQUIRE(actions.trigger("edit.paste"));
    auto foreign = other_editor.get_selection().get_primary();
    REQUIRE(other.is_on_canvas(foreign));
    REQUIRE(other.get(foreign).get_shape());
  }

  TEST_CASE("duplicate") {
    auto fixture = Fixture();
    auto& actions = fixture.m_actions;
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    fixture.m_edit_actions->set_view_provider([] {
      return std::optional(Rect(0, 0, 2000, 1000));
    });
    auto revealed = std::optional<Rect>();
    fixture.m_edit_actions->set_reveal_handler([&] (const auto& region) {
      revealed = region;
    });
    editor.get_selection().select(ids.m_instance);
    REQUIRE(actions.trigger("edit.duplicate"));
    auto instance = editor.get_selection().get_primary();
    REQUIRE(document.get(instance).m_header.m_name == "Button \xE2\x80\x93 1");
    REQUIRE(!revealed);
    editor.get_selection().select(ids.m_artboard);
    REQUIRE(actions.trigger("edit.duplicate"));
    REQUIRE(actions.get("edit.undo").text() == "Undo Duplicate");
    auto& copy = document.get(editor.get_selection().get_primary());
    REQUIRE(copy.m_header.m_name == "Home \xE2\x80\x93 1");
    REQUIRE(copy.get_artboard()->m_frame == Rect(545, 50, 375, 812));
    REQUIRE(copy.get_artboard()->m_layout_grid ==
      make_initial_grid(*copy.get_artboard()));
    REQUIRE(revealed == copy.get_artboard()->m_frame);
    fixture.m_edit_actions->set_workspace(Workspace::PROTOTYPE);
    editor.get_selection().select(ids.m_artboard);
    REQUIRE(actions.trigger("edit.duplicate"));
    REQUIRE(!document.get(editor.get_selection().get_primary()).
      get_artboard()->m_layout_grid);
  }

  TEST_CASE("reset_main_state") {
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto commands = CommandRegistry();
    add_standard_commands(commands);
    add_component_commands(commands);
    auto edit_actions = EditActions(actions, commands);
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    edit_actions.set_editor(&editor);
    editor.get_selection().select(ids.m_instance);
    REQUIRE(actions.get("object.reset_main_state").isEnabled());
    editor.get_text_selection().set(ids.m_label, 0, 0);
    REQUIRE(!actions.get("object.reset_main_state").isEnabled());
    editor.get_text_selection().clear();
    REQUIRE(actions.get("object.reset_main_state").isEnabled());
  }

  TEST_CASE("flip_while_editing_text") {
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto commands = CommandRegistry();
    add_standard_commands(commands);
    add_header_commands(commands, estimate_text_bounds);
    auto edit_actions = EditActions(actions, commands);
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    edit_actions.set_editor(&editor);
    editor.get_selection().select(ids.m_label);
    for(auto name : {"object.transform.flip_horizontally",
        "object.transform.flip_vertically"}) {
      REQUIRE(actions.get(name).isEnabled());
    }
    editor.get_text_selection().set(ids.m_label, 0, 0);
    for(auto name : {"object.transform.flip_horizontally",
        "object.transform.flip_vertically"}) {
      REQUIRE(!actions.get(name).isEnabled());
    }
    editor.get_text_selection().clear();
    REQUIRE(actions.get("object.transform.flip_horizontally").isEnabled());
  }

  TEST_CASE("text_properties") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto text = *document.get(ids.m_label).get_text();
      text.m_style.m_stroke = Stroke(make_color(0, 128, 0), 3);
      transaction.replace_payload(ids.m_label, text);
      transaction.commit();
    }
    auto editor = Editor(document);
    fixture.m_edit_actions->set_editor(&editor);
    editor.get_selection().select(
      std::vector<NodeId>({ids.m_ellipse, ids.m_label}));
    REQUIRE(fixture.m_actions.trigger("edit.copy"));
    auto& selection = editor.get_text_selection();
    REQUIRE(selection.get_last_border() == Stroke(make_color(0, 128, 0), 3));
    REQUIRE(selection.get_last_style().m_font.m_family == "Roboto");
  }
}

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QToolButton>
#include <doctest/doctest.h>
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Ui/PrototypeInspector.hpp"
#include "../PrototypeTests/Stage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  template<typename T>
  T& get(PrototypeInspector& inspector, std::string_view name) {
    auto widget = dynamic_cast<T*>(inspector.find_field(name));
    REQUIRE(widget);
    return *widget;
  }

  void choose(QComboBox& box, const QString& text) {
    auto index = box.findText(text);
    REQUIRE(index >= 0);
    box.setCurrentIndex(index);
    box.activated(index);
    QApplication::processEvents();
  }

  void enter(QComboBox& box, const QString& text) {
    box.setEditText(text);
    box.lineEdit()->editingFinished();
    QApplication::processEvents();
  }

  const Interaction& get_first(const Document& document, const NodeId& id) {
    auto interactions = get_interactions(document, id);
    REQUIRE(!interactions.empty());
    return *interactions.front();
  }

  bool is_item_enabled(QComboBox& box, const QString& text) {
    auto model = qobject_cast<QStandardItemModel*>(box.model());
    return model->item(box.findText(text))->isEnabled();
  }
}

TEST_SUITE("PrototypeInspector") {
  TEST_CASE("placeholders") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    REQUIRE(inspector.get_fields() == std::vector<std::string>({"add",
      "trigger", "type", "destination", "animation", "easing", "duration"}));
    REQUIRE(!inspector.find_field("add")->isEnabled());
    auto& trigger = get<QComboBox>(inspector, "trigger");
    REQUIRE(trigger.currentText() == "Tap");
    REQUIRE(!trigger.isEnabled());
    REQUIRE(get<QComboBox>(inspector, "easing").currentText() == "Ease In");
    REQUIRE(get<QComboBox>(inspector, "duration").currentText() == "0.2 s");
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    REQUIRE(inspector.get_node() == scene.m_button);
    REQUIRE(inspector.find_field("add")->isEnabled());
    auto& fixed = get<QCheckBox>(inspector, "fixed");
    REQUIRE(!fixed.isChecked());
    REQUIRE(!fixed.isEnabled());
  }

  TEST_CASE("add") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    auto& interaction = get_first(document, scene.m_button);
    REQUIRE(inspector.get_current() == interaction.m_id);
    REQUIRE(interaction.m_action == Action(
      ArtboardTransition(NodeId(), make_empty_animation())));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(get<QComboBox>(inspector, "trigger").isEnabled());
    REQUIRE(get<QComboBox>(inspector, "destination").currentText() ==
      "None");
    REQUIRE(get<QComboBox>(inspector, "animation").currentText() == "None");
    REQUIRE(!get<QComboBox>(inspector, "animation").isEnabled());
    REQUIRE(get<QComboBox>(inspector, "easing").currentText() == "Ease In");
    REQUIRE(get<QComboBox>(inspector, "duration").currentText() == "0.2 s");
    choose(get<QComboBox>(inspector, "destination"), "Menu");
    REQUIRE(get_first(document, scene.m_button).m_action == Action(
      ArtboardTransition(scene.m_menu, make_default_animation())));
    REQUIRE(get<QComboBox>(inspector, "animation").currentText() ==
      "Dissolve");
    REQUIRE(get<QComboBox>(inspector, "animation").isEnabled());
    REQUIRE(get<QComboBox>(inspector, "easing").currentText() == "Ease Out");
    REQUIRE(get<QComboBox>(inspector, "duration").currentText() == "0.3 s");
    editor.get_selection().select(scene.m_home);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    REQUIRE(std::holds_alternative<TimeTrigger>(
      get_first(document, scene.m_home).m_trigger));
  }

  TEST_CASE("triggers") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_default_animation()));
    add_interaction(document, scene.m_home, TimeTrigger(1),
      ArtboardTransition(scene.m_second, make_default_animation()));
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    auto list = [&] {
      auto& box = get<QComboBox>(inspector, "trigger");
      auto items = QStringList();
      for(auto i = 0; i != box.count(); ++i) {
        items.push_back(box.itemText(i));
      }
      return items;
    };
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    REQUIRE(list() == QStringList({"Tap", "Drag", "Keys & Gamepad", "Voice"}));
    REQUIRE(!is_item_enabled(get<QComboBox>(inspector, "trigger"), "Voice"));
    editor.get_selection().select(scene.m_home);
    QApplication::processEvents();
    REQUIRE(list() == QStringList({"Time", "Keys & Gamepad", "Voice"}));
    enter(get<QComboBox>(inspector, "delay"), "2");
    REQUIRE(get_first(document, scene.m_home).m_trigger ==
      Trigger(TimeTrigger(2)));
  }

  TEST_CASE("edit") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_default_animation()));
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    auto& history = editor.get_history();
    choose(get<QComboBox>(inspector, "destination"), "Menu");
    REQUIRE(std::get<ArtboardTransition>(get_first(document,
      scene.m_button).m_action).m_destination == scene.m_menu);
    REQUIRE(history.get_undo_count() == 1);
    choose(get<QComboBox>(inspector, "animation"), "Slide Left");
    choose(get<QComboBox>(inspector, "easing"), "Ease In");
    enter(get<QComboBox>(inspector, "duration"), "0.6 s");
    REQUIRE(std::get<ArtboardTransition>(get_first(document,
      scene.m_button).m_action).m_animation ==
      Animation(Transition::SLIDE_LEFT, Easing::EASE_IN, 0.6));
    REQUIRE(history.get_undo_count() == 4);
    get<QCheckBox>(inspector, "preserve_scroll").click();
    QApplication::processEvents();
    REQUIRE(std::get<ArtboardTransition>(get_first(document,
      scene.m_button).m_action).m_preserve_scroll);
    choose(get<QComboBox>(inspector, "trigger"), "Drag");
    REQUIRE(get_first(document, scene.m_button).m_trigger ==
      Trigger(DragTrigger()));
    auto& type = get<QComboBox>(inspector, "type");
    REQUIRE(type.findText("Transition to State") < 0);
    REQUIRE(!is_item_enabled(type, "Scroll To"));
    REQUIRE(!is_item_enabled(type, "Audio Playback"));
    REQUIRE(!is_item_enabled(type, "Speech Playback"));
    choose(type, "Auto-Animate");
    REQUIRE(!inspector.find_field("animation"));
    REQUIRE(!inspector.find_field("preserve_scroll"));
    REQUIRE(get<QComboBox>(inspector, "easing").count() == 7);
    choose(get<QComboBox>(inspector, "type"), "Overlay");
    auto& x = get<QLineEdit>(inspector, "overlay_x");
    x.setText("25");
    x.editingFinished();
    QApplication::processEvents();
    auto& overlay = std::get<Overlay>(
      get_first(document, scene.m_button).m_action);
    REQUIRE(overlay.m_position == Point(25, 0));
    REQUIRE(overlay.m_destination == scene.m_menu);
    choose(get<QComboBox>(inspector, "type"), "Previous Artboard");
    REQUIRE(std::holds_alternative<PreviousArtboard>(
      get_first(document, scene.m_button).m_action));
    REQUIRE(!inspector.find_field("destination"));
    REQUIRE(editor.undo());
    REQUIRE(std::holds_alternative<Overlay>(
      get_first(document, scene.m_button).m_action));
    choose(get<QComboBox>(inspector, "type"), "Hyperlink");
    auto& url = get<QLineEdit>(inspector, "url");
    url.setText("https://www.example.com");
    url.editingFinished();
    QApplication::processEvents();
    REQUIRE(get_first(document, scene.m_button).m_action ==
      Action(Hyperlink("https://www.example.com")));
    REQUIRE(!inspector.find_field("animation"));
  }

  TEST_CASE("states") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_instance, HoverTrigger(),
      StateTransition(scene.m_hover, make_default_animation()));
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_instance);
    QApplication::processEvents();
    REQUIRE(get<QComboBox>(inspector, "trigger").currentText() == "Hover");
    auto& state = get<QComboBox>(inspector, "state");
    REQUIRE(state.count() == 2);
    REQUIRE(state.currentText() == "Hover");
    choose(state, "Default State");
    REQUIRE(std::get<StateTransition>(get_first(document,
      scene.m_instance).m_action).m_state == scene.m_component);
    auto& animation = get<QComboBox>(inspector, "animation");
    REQUIRE(animation.count() == 3);
    choose(animation, "Auto-Animate");
    REQUIRE(get<QComboBox>(inspector, "easing").count() == 7);
  }

  TEST_CASE("multiple") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_default_animation()));
    auto second = add_interaction(document, scene.m_button,
      KeyTrigger("Ctrl+K"), PreviousArtboard());
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    REQUIRE(get<QToolButton>(inspector, "interaction_0").text() ==
      "Tap - Transition");
    get<QToolButton>(inspector, "interaction_1").click();
    QApplication::processEvents();
    REQUIRE(inspector.get_current() == second);
    REQUIRE(get<QLineEdit>(inspector, "keys").text() == "Ctrl+K");
    auto& keys = get<QLineEdit>(inspector, "keys");
    auto event = QKeyEvent(QEvent::KeyPress, Qt::Key_B,
      Qt::ShiftModifier, "B");
    QApplication::sendEvent(&keys, &event);
    QApplication::processEvents();
    REQUIRE(get_interactions(document, scene.m_button)[1]->m_trigger ==
      Trigger(KeyTrigger("Shift+B")));
    get<QToolButton>(inspector, "remove").click();
    QApplication::processEvents();
    REQUIRE(get_interactions(document, scene.m_button).size() == 1);
    REQUIRE(!inspector.find_field("interaction_0"));
  }

  TEST_CASE("flows") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_second);
    QApplication::processEvents();
    REQUIRE(!inspector.find_field("fixed"));
    auto& flow = get<QCheckBox>(inspector, "flow");
    REQUIRE(!flow.isChecked());
    flow.click();
    QApplication::processEvents();
    REQUIRE(find_flow(document, scene.m_second)->m_name == "Flow 1");
    auto& name = get<QLineEdit>(inspector, "flow_name");
    name.setText("Checkout");
    name.editingFinished();
    QApplication::processEvents();
    REQUIRE(find_flow(document, scene.m_second)->m_name == "Checkout");
    get<QCheckBox>(inspector, "flow").click();
    QApplication::processEvents();
    REQUIRE(!find_flow(document, scene.m_second));
    REQUIRE(editor.get_history().get_undo_count() == 3);
  }

  TEST_CASE("fixed_position") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    auto requests = std::vector<std::pair<NodeId, bool>>();
    inspector.set_fixed_position_factory(
      [&] (const NodeId& node, bool is_fixed) -> std::unique_ptr<Command> {
        requests.emplace_back(node, is_fixed);
        return nullptr;
      });
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    auto& fixed = get<QCheckBox>(inspector, "fixed");
    REQUIRE(fixed.isEnabled());
    fixed.click();
    QApplication::processEvents();
    REQUIRE(requests ==
      std::vector<std::pair<NodeId, bool>>({{scene.m_button, true}}));
  }
}

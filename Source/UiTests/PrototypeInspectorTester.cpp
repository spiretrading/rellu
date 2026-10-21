#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QToolButton>
#include <QWheelEvent>
#include <doctest/doctest.h>
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Ui/PrototypeInspector.hpp"
#include "../PrototypeTests/Stage.hpp"
#include "FocusWindow.hpp"

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

  const ScrollTransition& get_scroll(
      const Document& document, const NodeId& id) {
    return std::get<ScrollTransition>(get_first(document, id).m_action);
  }

  QLineEdit& focus(PrototypeInspector& inspector, std::string_view name) {
    auto& field = get<QWidget>(inspector, name);
    field.setFocus();
    REQUIRE(field.hasFocus());
    auto line = qobject_cast<QLineEdit*>(&field);
    if(auto box = qobject_cast<QComboBox*>(&field)) {
      line = box->lineEdit();
    }
    REQUIRE(line);
    line->selectAll();
    return *line;
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
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Transition");
    REQUIRE(get<QComboBox>(inspector, "destination").currentText() ==
      "(Choose a destination)");
    REQUIRE(get<QComboBox>(inspector, "animation").currentText() == "None");
    REQUIRE(get<QComboBox>(inspector, "easing").currentText() == "Ease In");
    REQUIRE(get<QComboBox>(inspector, "duration").currentText() == "0.2 s");
    REQUIRE(!get<QComboBox>(inspector, "duration").isEnabled());
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
    REQUIRE(interaction.m_action == Action(ArtboardTransition(NodeId(),
      Animation(Transition::NONE, Easing::EASE_OUT, 0.30000001192092896))));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(get<QComboBox>(inspector, "trigger").isEnabled());
    auto& destination = get<QComboBox>(inspector, "destination");
    REQUIRE(destination.currentIndex() == -1);
    REQUIRE(destination.placeholderText() == "(Choose an artboard)");
    REQUIRE(destination.findText("Home") < 0);
    REQUIRE(destination.findText("None") < 0);
    REQUIRE(destination.count() == 3);
    REQUIRE(get<QComboBox>(inspector, "animation").currentText() == "None");
    REQUIRE(!get<QComboBox>(inspector, "animation").isEnabled());
    REQUIRE(get<QComboBox>(inspector, "easing").currentText() == "Ease Out");
    REQUIRE(!get<QComboBox>(inspector, "easing").isEnabled());
    REQUIRE(get<QComboBox>(inspector, "duration").currentText() == "0.3 s");
    REQUIRE(!get<QComboBox>(inspector, "duration").isEnabled());
    auto& easings = get<QComboBox>(inspector, "easing");
    auto items = QStringList();
    for(auto i = 0; i != easings.count(); ++i) {
      items.push_back(easings.itemText(i));
    }
    REQUIRE(items == QStringList({"None", "Ease Out", "Ease In", "Ease In-Out",
      "Snap", "Wind Up", "Bounce"}));
    choose(get<QComboBox>(inspector, "destination"), "Menu");
    REQUIRE(get_first(document, scene.m_button).m_action == Action(
      ArtboardTransition(scene.m_menu, make_empty_animation())));
    REQUIRE(get<QComboBox>(inspector, "animation").currentText() == "None");
    REQUIRE(get<QComboBox>(inspector, "animation").isEnabled());
    REQUIRE(get<QComboBox>(inspector, "easing").currentText() == "Ease Out");
    REQUIRE(get<QComboBox>(inspector, "duration").currentText() == "0.3 s");
    enter(get<QComboBox>(inspector, "duration"), "0.3 s");
    REQUIRE(editor.get_history().get_undo_count() == 2);
    editor.get_selection().select(scene.m_home);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    REQUIRE(get_first(document, scene.m_home).m_trigger ==
      Trigger(TapTrigger()));
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    choose(get<QComboBox>(inspector, "type"), "Overlay");
    editor.get_selection().select(scene.m_box);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    REQUIRE(get_first(document, scene.m_box).m_action ==
      Action(Overlay(NodeId(), Point(0, 0), make_empty_animation())));
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Overlay");
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    auto interactions = get_interactions(document, scene.m_button);
    REQUIRE(interactions.size() == 2);
    REQUIRE(interactions[1]->m_trigger == Trigger(KeyTrigger()));
  }

  TEST_CASE("wheel") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    choose(get<QComboBox>(inspector, "destination"), "Menu");
    choose(get<QComboBox>(inspector, "animation"), "Dissolve");
    auto count = editor.get_history().get_undo_count();
    auto& easing = get<QComboBox>(inspector, "easing");
    REQUIRE(easing.isEnabled());
    auto index = easing.currentIndex();
    auto event = QWheelEvent(QPointF(5, 5), QPointF(5, 5), QPoint(),
      QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
      false);
    QApplication::sendEvent(&easing, &event);
    QApplication::processEvents();
    REQUIRE(!event.isAccepted());
    REQUIRE(get<QComboBox>(inspector, "easing").currentIndex() == index);
    REQUIRE(editor.get_history().get_undo_count() == count);
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
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    auto second = get_interactions(document, scene.m_button)[1];
    REQUIRE(second->m_trigger == Trigger(DragTrigger()));
    REQUIRE(std::get<ArtboardTransition>(second->m_action).m_animation.
      m_transition == Transition::AUTO_ANIMATE);
    REQUIRE(list() == QStringList({"Drag", "Keys & Gamepad", "Voice"}));
    editor.get_selection().select(scene.m_home);
    QApplication::processEvents();
    REQUIRE(list() ==
      QStringList({"Tap", "Drag", "Time", "Keys & Gamepad", "Voice"}));
    enter(get<QComboBox>(inspector, "delay"), "2");
    REQUIRE(get_first(document, scene.m_home).m_trigger ==
      Trigger(TimeTrigger(2)));
    editor.get_selection().select(scene.m_instance);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    REQUIRE(list() ==
      QStringList({"Tap", "Drag", "Hover", "Keys & Gamepad", "Voice"}));
  }

  TEST_CASE("component_without_states") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    {
      auto transaction = Transaction(document);
      auto group = *document.get(scene.m_instance).get_group();
      group.m_component_instance->m_states.clear();
      transaction.replace_payload(scene.m_instance, group);
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_instance);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    auto& box = get<QComboBox>(inspector, "trigger");
    REQUIRE(box.findText("Hover") < 0);
    REQUIRE(box.count() == 4);
    REQUIRE(get<QComboBox>(inspector, "destination").placeholderText() ==
      "(Choose an artboard)");
    REQUIRE(inspector.find_field("secondary_action"));
    REQUIRE(!inspector.find_field("secondary_action")->isEnabled());
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
      Animation(Transition::SLIDE_LEFT, Easing::EASE_IN, 0.6000000238418579));
    REQUIRE(history.get_undo_count() == 4);
    get<QCheckBox>(inspector, "preserve_scroll").click();
    QApplication::processEvents();
    REQUIRE(std::get<ArtboardTransition>(get_first(document,
      scene.m_button).m_action).m_preserve_scroll);
    choose(get<QComboBox>(inspector, "trigger"), "Drag");
    REQUIRE(get_first(document, scene.m_button).m_trigger ==
      Trigger(DragTrigger()));
    REQUIRE(get_first(document, scene.m_button).m_action ==
      Action(ArtboardTransition(scene.m_menu, Animation(
        Transition::AUTO_ANIMATE, Easing::LINEAR, 0.6000000238418579), true)));
    REQUIRE(!inspector.find_field("duration"));
    REQUIRE(get<QComboBox>(inspector, "easing").currentText() == "None");
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
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Transition");
    REQUIRE(!inspector.find_field("state"));
    auto& destination = get<QComboBox>(inspector, "destination");
    REQUIRE(destination.placeholderText() == "(Choose a state)");
    REQUIRE(destination.currentText() == "Hover");
    REQUIRE(destination.currentIndex() == 2);
    REQUIRE(destination.itemText(0) == "None");
    REQUIRE(destination.findText("Default State") < 0);
    REQUIRE(destination.findText("Second") < 0);
    REQUIRE(destination.count() == 3);
    REQUIRE(get<QComboBox>(inspector, "animation").count() == 2);
    choose(get<QComboBox>(inspector, "destination"), "None");
    REQUIRE(get_first(document, scene.m_instance).m_action ==
      Action(StateTransition(StateId(), make_default_animation())));
    REQUIRE(get<QComboBox>(inspector, "destination").placeholderText() ==
      "(Choose a state)");
    REQUIRE(get<QComboBox>(inspector, "destination").count() == 1);
    choose(get<QComboBox>(inspector, "destination"), "Hover");
    REQUIRE(get_first(document, scene.m_instance).m_action ==
      Action(StateTransition(scene.m_hover, make_default_animation())));
    choose(get<QComboBox>(inspector, "type"), "Auto-Animate");
    REQUIRE(get_first(document, scene.m_instance).m_action ==
      Action(StateTransition(scene.m_hover, Animation(
        Transition::AUTO_ANIMATE, Easing::EASE_OUT, DEFAULT_DURATION))));
    REQUIRE(!inspector.find_field("animation"));
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Auto-Animate");
    REQUIRE(get<QComboBox>(inspector, "easing").count() == 7);
    REQUIRE(get<QComboBox>(inspector, "destination").currentText() ==
      "Hover");
  }

  TEST_CASE("artboard_order") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    {
      auto transaction = Transaction(document);
      add_board(transaction, "Below", Rect(-600, 400, 100, 100),
        make_color(255, 255, 255));
      add_board(transaction, "Above", Rect(2000, -100, 100, 100),
        make_color(255, 255, 255));
      transaction.commit();
      document.clear_history();
    }
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(NodeId(), make_empty_animation()));
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    auto& destination = get<QComboBox>(inspector, "destination");
    auto items = QStringList();
    for(auto i = 0; i != destination.count(); ++i) {
      items.push_back(destination.itemText(i));
    }
    REQUIRE(items ==
      QStringList({"Above", "Second", "Menu", "Below", "Long"}));
  }

  TEST_CASE("types") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_default_animation()));
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    auto& type = get<QComboBox>(inspector, "type");
    auto items = QStringList();
    for(auto i = 0; i != type.count(); ++i) {
      items.push_back(type.itemText(i));
    }
    REQUIRE(items == QStringList({"Transition", "Auto-Animate", "Hyperlink",
      "Overlay", "Scroll To", "Previous Artboard", "Audio Playback",
      "Speech Playback"}));
  }

  TEST_CASE("scroll_to") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_home);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    REQUIRE(!is_item_enabled(get<QComboBox>(inspector, "type"), "Scroll To"));
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    REQUIRE(is_item_enabled(get<QComboBox>(inspector, "type"), "Scroll To"));
    choose(get<QComboBox>(inspector, "type"), "Scroll To");
    REQUIRE(get_first(document, scene.m_button).m_action ==
      Action(ScrollTransition(NodeId(), 0,
        Animation(Transition::DISSOLVE, Easing::EASE_OUT, DEFAULT_DURATION))));
    REQUIRE(editor.get_history().get_undo_label() == "Change Interaction");
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Scroll To");
    REQUIRE(!inspector.find_field("animation"));
    REQUIRE(!inspector.find_field("preserve_scroll"));
    auto& destination = get<QComboBox>(inspector, "destination");
    REQUIRE(destination.placeholderText() == "(Choose a layer)");
    REQUIRE(destination.currentIndex() == -1);
    REQUIRE(destination.count() == 2);
    REQUIRE(destination.itemText(0) == "Toggle");
    REQUIRE(destination.itemText(1) == "Box");
    choose(destination, "Box");
    REQUIRE(get_first(document, scene.m_button).m_action ==
      Action(ScrollTransition(scene.m_box, 0,
        Animation(Transition::DISSOLVE, Easing::EASE_OUT, DEFAULT_DURATION))));
    auto& chosen = get<QComboBox>(inspector, "destination");
    REQUIRE(chosen.count() == 4);
    REQUIRE(chosen.itemText(0) == "None");
    REQUIRE(chosen.itemText(2) == "Toggle");
    REQUIRE(chosen.currentText() == "Box");
    auto& easings = get<QComboBox>(inspector, "easing");
    REQUIRE(easings.isEnabled());
    auto items = QStringList();
    for(auto i = 0; i != easings.count(); ++i) {
      items.push_back(easings.itemText(i));
    }
    REQUIRE(items == QStringList({"None", "Ease Out", "Ease In", "Ease In-Out",
      "Snap", "Wind Up", "Bounce"}));
    auto& offset = get<QLineEdit>(inspector, "y_offset");
    REQUIRE(offset.text() == "0");
    offset.setText("-500");
    offset.editingFinished();
    QApplication::processEvents();
    REQUIRE(std::get<ScrollTransition>(
      get_first(document, scene.m_button).m_action).m_offset == -100);
    REQUIRE(get<QLineEdit>(inspector, "y_offset").text() == "-100");
    choose(get<QComboBox>(inspector, "easing"), "Ease In");
    enter(get<QComboBox>(inspector, "duration"), "0.6 s");
    REQUIRE(get_first(document, scene.m_button).m_action ==
      Action(ScrollTransition(scene.m_box, -100,
        Animation(Transition::DISSOLVE, Easing::EASE_IN, 0.6000000238418579))));
    REQUIRE(editor.get_history().get_undo_label() == "Change Interaction");
    choose(get<QComboBox>(inspector, "destination"), "None");
    REQUIRE(std::get<ScrollTransition>(
      get_first(document, scene.m_button).m_action).m_destination.is_nil());
    REQUIRE(editor.undo());
    REQUIRE(std::get<ScrollTransition>(get_first(document,
      scene.m_button).m_action).m_destination == scene.m_box);
    choose(get<QComboBox>(inspector, "type"), "Transition");
    auto transition = std::get_if<ArtboardTransition>(
      &get_first(document, scene.m_button).m_action);
    REQUIRE(transition);
    REQUIRE(transition->m_destination.is_nil());
  }

  TEST_CASE("scroll_to_triggers") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_instance);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    choose(get<QComboBox>(inspector, "type"), "Scroll To");
    choose(get<QComboBox>(inspector, "destination"), "Box");
    choose(get<QComboBox>(inspector, "trigger"), "Hover");
    REQUIRE(get_first(document, scene.m_instance).m_trigger ==
      Trigger(HoverTrigger()));
    REQUIRE(get_first(document, scene.m_instance).m_action ==
      Action(StateTransition(StateId(), Animation(Transition::AUTO_ANIMATE,
        Easing::EASE_OUT, DEFAULT_DURATION))));
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Auto-Animate");
    REQUIRE(editor.undo());
    QApplication::processEvents();
    choose(get<QComboBox>(inspector, "trigger"), "Drag");
    REQUIRE(get_first(document, scene.m_instance).m_trigger ==
      Trigger(DragTrigger()));
    REQUIRE(get_first(document, scene.m_instance).m_action ==
      Action(ArtboardTransition(NodeId(), Animation(Transition::AUTO_ANIMATE,
        Easing::LINEAR, DEFAULT_DURATION))));
  }

  TEST_CASE("single_artboard") {
    auto document = Document();
    auto board = NodeId();
    auto button = NodeId();
    auto box = NodeId();
    {
      auto transaction = Transaction(document);
      board = add_board(transaction, "Board", Rect(0, 0, 400, 800),
        make_color(255, 255, 255));
      button = add_box(transaction, board, "Button", Rect(10, 10, 100, 50),
        make_color(0, 0, 255));
      box = add_box(transaction, board, "Box", Rect(0, 600, 100, 100),
        make_color(255, 0, 0));
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(button);
    QApplication::processEvents();
    get<QToolButton>(inspector, "add").click();
    QApplication::processEvents();
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Transition");
    auto& destination = get<QComboBox>(inspector, "destination");
    REQUIRE(destination.placeholderText() == "(Choose a layer)");
    REQUIRE(destination.count() == 1);
    REQUIRE(destination.itemText(0) == "Box");
    choose(destination, "Box");
    REQUIRE(get_first(document, button).m_action == Action(ScrollTransition(
      box, 0, Animation(Transition::DISSOLVE, Easing::EASE_OUT,
        DEFAULT_DURATION))));
    REQUIRE(get<QComboBox>(inspector, "type").currentText() == "Scroll To");
    REQUIRE(get<QComboBox>(inspector, "destination").currentText() == "Box");
    choose(get<QComboBox>(inspector, "trigger"), "Drag");
    REQUIRE(get_first(document, button).m_action ==
      Action(ArtboardTransition(NodeId(), Animation(Transition::AUTO_ANIMATE,
        Easing::LINEAR, DEFAULT_DURATION))));
    auto& dragged = get<QComboBox>(inspector, "destination");
    REQUIRE(dragged.count() == 0);
    REQUIRE(dragged.placeholderText() == "(Choose a destination)");
  }

  TEST_CASE("text_hyperlinks") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto label = NodeId();
    {
      auto transaction = Transaction(document);
      auto text = Text();
      text.m_content = Shared(std::u16string(u"alpha beta gamma"));
      text.m_frame = AreaFrame(Size(200, 20));
      auto run = TextRun();
      run.m_length = 5;
      run.m_hyperlink = "https://a.example.com";
      text.m_runs.push_back(run);
      run.m_length = 1;
      run.m_hyperlink.clear();
      text.m_runs.push_back(run);
      run.m_length = 5;
      run.m_hyperlink = "b.example.com";
      text.m_runs.push_back(run);
      run.m_length = 5;
      run.m_style.m_is_underlined = true;
      text.m_runs.push_back(run);
      label = transaction.append(
        scene.m_home, make_stage_header("Label", 10, 250), text);
      transaction.commit();
      document.clear_history();
    }
    auto editor = Editor(document);
    auto inspector = PrototypeInspector();
    inspector.set_editor(&editor);
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    REQUIRE(!inspector.find_field("link_text_0"));
    editor.get_selection().select(label);
    QApplication::processEvents();
    REQUIRE(get<QLabel>(inspector, "link_text_0").text() == "\"alpha\"");
    REQUIRE(get<QLabel>(inspector, "link_text_1").text() ==
      "\"beta gamma\"");
    REQUIRE(!inspector.find_field("link_text_2"));
    REQUIRE(get<QLineEdit>(inspector, "link_url_0").text() ==
      "https://a.example.com");
    REQUIRE(!get<QLabel>(inspector, "link_status_0").pixmap().isNull());
    auto& status = get<QLabel>(inspector, "link_status_1");
    auto& message = *status.parentWidget()->parentWidget()->findChild<QLabel*>(
      "hyperlink_message");
    REQUIRE(!message.isHidden());
    auto& url = get<QLineEdit>(inspector, "link_url_1");
    url.setText("mailto:b@example.com");
    REQUIRE(message.isHidden());
    url.editingFinished();
    QApplication::processEvents();
    auto& runs = document.get(label).get_text()->m_runs;
    REQUIRE(runs.size() == 4);
    REQUIRE(runs[0].m_hyperlink == "https://a.example.com");
    REQUIRE(runs[2].m_hyperlink == "mailto:b@example.com");
    REQUIRE(runs[3].m_hyperlink == "mailto:b@example.com");
    REQUIRE(editor.get_history().get_undo_count() == 1);
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
    REQUIRE(inspector.get_current().is_nil());
    REQUIRE(editor.get_context().get_interaction().is_nil());
    REQUIRE(!get<QToolButton>(inspector, "interaction_0").isChecked());
    REQUIRE(!get<QComboBox>(inspector, "trigger").isEnabled());
    REQUIRE(get<QToolButton>(inspector, "interaction_0").text() ==
      "Tap - Transition");
    get<QToolButton>(inspector, "interaction_1").click();
    QApplication::processEvents();
    REQUIRE(inspector.get_current() == second);
    REQUIRE(editor.get_context().get_interaction() == second);
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

  TEST_CASE("field_keys") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = new PrototypeInspector();
    inspector->set_editor(&editor);
    auto window = FocusWindow(inspector);
    inspector->set_release_handler([&] {
      window.release();
    });
    auto& history = editor.get_history();
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    get<QToolButton>(*inspector, "add").click();
    QApplication::processEvents();
    choose(get<QComboBox>(*inspector, "type"), "Scroll To");
    choose(get<QComboBox>(*inspector, "destination"), "Box");
    auto steps = history.get_undo_count();
    focus(*inspector, "y_offset");
    type("-50");
    press(Qt::Key_Return);
    REQUIRE(get_scroll(document, scene.m_button).m_offset == -50);
    REQUIRE(history.get_undo_count() == steps + 1);
    REQUIRE(window.m_canvas->hasFocus());
    REQUIRE(!window.m_zoom->hasFocus());
    REQUIRE(get<QLineEdit>(*inspector, "y_offset").text() == "-50");
    focus(*inspector, "y_offset");
    type("-30");
    press(Qt::Key_Tab);
    REQUIRE(get_scroll(document, scene.m_button).m_offset == -30);
    REQUIRE(history.get_undo_count() == steps + 2);
    REQUIRE(window.m_canvas->hasFocus());
    focus(*inspector, "y_offset");
    type("-40");
    window.m_canvas->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
    REQUIRE(get_scroll(document, scene.m_button).m_offset == -40);
    REQUIRE(history.get_undo_count() == steps + 3);
    focus(*inspector, "duration");
    type("0.5");
    press(Qt::Key_Return);
    REQUIRE(get_scroll(document, scene.m_button).m_animation.m_duration ==
      0.5);
    REQUIRE(get<QComboBox>(*inspector, "duration").currentText() == "0.5 s");
    REQUIRE(window.m_canvas->hasFocus());
    auto& duration = focus(*inspector, "duration");
    type("0.8");
    press(Qt::Key_Escape);
    REQUIRE(get_scroll(document, scene.m_button).m_animation.m_duration ==
      0.5);
    REQUIRE(duration.text() == "0.5 s");
    REQUIRE(window.m_canvas->hasFocus());
    REQUIRE(editor.get_selection().get_primary() == scene.m_button);
    REQUIRE(!window.m_zoom->hasFocus());
  }

  TEST_CASE("live_y_offset") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = new PrototypeInspector();
    inspector->set_editor(&editor);
    auto window = FocusWindow(inspector);
    inspector->set_release_handler([&] {
      window.release();
    });
    auto& history = editor.get_history();
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    get<QToolButton>(*inspector, "add").click();
    QApplication::processEvents();
    choose(get<QComboBox>(*inspector, "type"), "Scroll To");
    choose(get<QComboBox>(*inspector, "destination"), "Box");
    choose(get<QComboBox>(*inspector, "easing"), "Ease In");
    REQUIRE(editor.undo());
    QApplication::processEvents();
    history.mark_clean();
    auto steps = history.get_undo_count();
    auto interaction = get_first(document, scene.m_button);
    auto& offset = focus(*inspector, "y_offset");
    type("-2");
    REQUIRE(get_scroll(document, scene.m_button).m_offset == -2);
    type("0");
    REQUIRE(get_scroll(document, scene.m_button).m_offset == -20);
    REQUIRE(history.get_undo_count() == steps);
    REQUIRE(history.get_redo_count() == 1);
    REQUIRE(offset.hasFocus());
    REQUIRE(offset.text() == "-20");
    press(Qt::Key_Escape);
    REQUIRE(get_first(document, scene.m_button) == interaction);
    REQUIRE(history.get_undo_count() == steps);
    REQUIRE(history.get_redo_count() == 1);
    REQUIRE(!history.is_dirty());
    REQUIRE(window.m_canvas->hasFocus());
    REQUIRE(get<QLineEdit>(*inspector, "y_offset").text() == "0");
    REQUIRE(editor.redo());
    QApplication::processEvents();
    REQUIRE(get_scroll(document, scene.m_button).m_animation.m_easing ==
      Easing::EASE_IN);
    REQUIRE(get_scroll(document, scene.m_button).m_offset == 0);
    focus(*inspector, "y_offset");
    type("-60");
    REQUIRE(get_scroll(document, scene.m_button).m_offset == -60);
    REQUIRE(history.get_undo_count() == steps + 1);
    press(Qt::Key_Return);
    REQUIRE(get_scroll(document, scene.m_button).m_offset == -60);
    REQUIRE(history.get_undo_count() == steps + 2);
    REQUIRE(history.get_redo_count() == 0);
    REQUIRE(history.get_undo_label() == "Change Interaction");
    REQUIRE(history.is_dirty());
    REQUIRE(editor.undo());
    REQUIRE(get_scroll(document, scene.m_button).m_offset == 0);
    REQUIRE(get_scroll(document, scene.m_button).m_animation.m_easing ==
      Easing::EASE_IN);
  }

  TEST_CASE("menu_focus") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto editor = Editor(document);
    auto inspector = new PrototypeInspector();
    inspector->set_editor(&editor);
    auto window = FocusWindow(inspector);
    inspector->set_release_handler([&] {
      window.release();
    });
    editor.get_selection().select(scene.m_button);
    QApplication::processEvents();
    get<QToolButton>(*inspector, "add").click();
    QApplication::processEvents();
    get<QComboBox>(*inspector, "type").setFocus();
    choose(get<QComboBox>(*inspector, "type"), "Scroll To");
    REQUIRE(window.m_canvas->hasFocus());
    REQUIRE(window.m_releases == 1);
    get<QComboBox>(*inspector, "destination").setFocus();
    choose(get<QComboBox>(*inspector, "destination"), "Box");
    REQUIRE(window.m_canvas->hasFocus());
    REQUIRE(!window.m_zoom->hasFocus());
    REQUIRE(window.m_releases == 2);
    get<QComboBox>(*inspector, "easing").setFocus();
    editor.get_selection().select(scene.m_home);
    QApplication::processEvents();
    REQUIRE(window.m_canvas->hasFocus());
    REQUIRE(window.m_releases == 3);
  }
}

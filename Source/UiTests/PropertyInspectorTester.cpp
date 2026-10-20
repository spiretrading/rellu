#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Ui/ComponentActions.hpp"
#include "Rellu/Ui/ComponentPropertyEditors.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  using Sections = std::vector<std::string>;

  QString get_text(const PropertyInspector& inspector, const char* path) {
    auto property = find_property(inspector.get_properties(), path);
    REQUIRE(property);
    return to_text(property->m_value);
  }

  bool is_mixed(const PropertyInspector& inspector, const char* path) {
    auto property = find_property(inspector.get_properties(), path);
    REQUIRE(property);
    return property->m_is_mixed;
  }

  QLineEdit& get_line(const PropertyInspector& inspector, const char* path) {
    auto line = qobject_cast<QLineEdit*>(inspector.find_field(path));
    REQUIRE(line);
    return *line;
  }

  QLineEdit& get_display(
      const PropertyInspector& inspector, const char* path) {
    auto line = qobject_cast<QLineEdit*>(inspector.find_display(path));
    REQUIRE(line);
    return *line;
  }

  void enter(
      PropertyInspector& inspector, const char* path, const QString& text) {
    auto& line = get_line(inspector, path);
    line.setText(text);
    Q_EMIT line.editingFinished();
  }

  void make_editable(PropertyInspector& inspector, Editor& editor) {
    inspector.set_property_editors(
      make_standard_property_editors(estimate_text_bounds));
    inspector.set_editor(&editor);
  }

  NodeId add_shape(
      Document& document, const NodeId& parent, ShapeGeometry geometry) {
    auto shape = Shape();
    shape.m_geometry = std::move(geometry);
    auto transaction = Transaction(document);
    auto id = transaction.append(parent, make_header("Shape"), shape);
    transaction.commit();
    return id;
  }
}

TEST_SUITE("PropertyInspector") {
  TEST_CASE("current") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    REQUIRE(inspector.get_properties().empty());
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "appearance"}));
    REQUIRE(!inspector.find_section("transform")->isEnabled());
    inspector.set_current(ids.m_background);
    REQUIRE(inspector.get_current() == ids.m_background);
    REQUIRE(inspector.find_section("transform")->isEnabled());
    REQUIRE(get_display(inspector, "transform.width").text() == "100");
    inspector.set_current(NodeId());
    REQUIRE(inspector.get_properties().empty());
    REQUIRE(get_display(inspector, "transform.width").text() == "0");
  }

  TEST_CASE("section_order") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto line = add_shape(
      document, ids.m_artboard, LineShape(Point(0, 0), Point(10, 10)));
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    inspector.set_current(ids.m_background);
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "appearance", "geometry", "fill",
      "stroke", "effects", "export"}));
    inspector.set_current(ids.m_ellipse);
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "appearance", "fill", "stroke",
      "effects", "export"}));
    inspector.set_current(line);
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "appearance", "stroke", "effects",
      "export"}));
    inspector.set_current(ids.m_label);
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "text", "appearance", "fill",
      "stroke", "effects", "export"}));
    inspector.set_current(ids.m_card);
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "appearance", "fill", "stroke",
      "export"}));
    inspector.set_current(ids.m_instance);
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "appearance", "fill", "stroke",
      "export"}));
    inspector.set_current(ids.m_artboard);
    REQUIRE(inspector.get_sections() == Sections({"align", "boolean",
      "component", "transform", "layout", "appearance", "fill", "grid"}));
    inspector.set_workspace(Workspace::PROTOTYPE);
    REQUIRE(inspector.get_sections() == Sections({"interaction"}));
    inspector.set_workspace(Workspace::SHARE);
    REQUIRE(inspector.get_sections() == Sections({"link_settings"}));
  }

  TEST_CASE("updates") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    inspector.set_current(ids.m_reference);
    REQUIRE(get_text(inspector, "fill.color") == "#0000FF");
    {
      auto transaction = Transaction(document);
      auto shape = *document.get(ids.m_master_shape).get_shape();
      shape.m_style.m_fill = SolidFill(make_color(0, 255, 0));
      transaction.replace_payload(ids.m_master_shape, shape);
      transaction.commit();
    }
    REQUIRE(get_text(inspector, "fill.color") == "#00FF00");
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_master_shape).m_header;
      header.m_opacity = 0.25;
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(get_display(inspector, "appearance.opacity").text() == "25%");
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_instance);
      transaction.commit();
    }
    REQUIRE(inspector.get_current().is_nil());
    REQUIRE(inspector.get_properties().empty());
  }

  TEST_CASE("read_only_fields") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    inspector.set_current(ids.m_background);
    REQUIRE(!inspector.find_field("appearance.opacity"));
    REQUIRE(get_display(inspector, "appearance.opacity").isReadOnly());
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_background);
    REQUIRE(inspector.find_field("appearance.opacity"));
    REQUIRE(inspector.find_field("export.marked"));
    auto text = make_text(u"Point");
    text.m_frame = PositionedFrame();
    auto transaction = Transaction(document);
    auto point = transaction.append(ids.m_artboard, make_header("Point"), text);
    transaction.commit();
    editor.get_selection().select(point);
    REQUIRE(!inspector.find_field("transform.width"));
    REQUIRE(get_display(inspector, "transform.width").isReadOnly());
    REQUIRE(inspector.find_field("transform.x"));
  }

  TEST_CASE("display_conventions") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto shape = *document.get(ids.m_background).get_shape();
      shape.m_style.m_effects.push_back(
        DropShadow(Point(0, 4), 8, make_color(0, 0, 0, 0.16)));
      transaction.replace_payload(ids.m_background, shape);
      auto header = document.get(ids.m_background).m_header;
      header.m_transform = make_rotation(0.5);
      transaction.replace_header(header);
      transaction.commit();
    }
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    inspector.set_current(ids.m_background);
    REQUIRE(get_text(inspector, "effects.drop_shadow.blur") == "8");
    REQUIRE(get_display(inspector, "effects.drop_shadow.blur").text() ==
      "16");
    REQUIRE(get_display(inspector, "effects.drop_shadow.y").text() == "4");
    REQUIRE(get_display(inspector, "transform.rotation").text() ==
      QString("28.65") + QChar(0x00B0));
  }

  TEST_CASE("number_edits") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    auto& history = editor.get_history();
    editor.get_selection().select(ids.m_background);
    REQUIRE(inspector.get_current() == ids.m_background);
    REQUIRE(get_line(inspector, "appearance.opacity").text() == "100%");
    enter(inspector, "appearance.opacity", "50%");
    REQUIRE(document.get(ids.m_background).m_header.m_opacity == 0.5);
    enter(inspector, "appearance.opacity", "25");
    REQUIRE(document.get(ids.m_background).m_header.m_opacity == 0.25);
    REQUIRE(get_line(inspector, "appearance.opacity").text() == "25%");
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Change Opacity");
    enter(inspector, "appearance.opacity", "-30");
    REQUIRE(document.get(ids.m_background).m_header.m_opacity == 0);
    REQUIRE(history.get_undo_count() == 1);
    enter(inspector, "transform.x", "30");
    REQUIRE(get_text(inspector, "transform.x") == "30");
    enter(inspector, "transform.x", "35");
    REQUIRE(get_text(inspector, "transform.x") == "35");
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.get_undo_label() == "Move");
    enter(inspector, "transform.width", "-5");
    REQUIRE(get_text(inspector, "transform.width") == "100");
    REQUIRE(get_line(inspector, "transform.width").text() == "100");
    enter(inspector, "transform.width", "wide");
    REQUIRE(get_line(inspector, "transform.width").text() == "100");
    REQUIRE(history.get_undo_count() == 2);
    enter(inspector, "transform.width", "150");
    REQUIRE(get_text(inspector, "transform.width") == "150");
    REQUIRE(get_text(inspector, "transform.x") == "35");
    REQUIRE(history.get_undo_count() == 3);
    REQUIRE(history.get_undo_label() == "Resize");
    enter(inspector, "transform.rotation", "90");
    REQUIRE(get_text(inspector, "transform.rotation") == "90");
    REQUIRE(history.get_undo_count() == 4);
    REQUIRE(editor.undo());
    REQUIRE(get_text(inspector, "transform.rotation") == "0");
    REQUIRE(editor.undo());
    REQUIRE(get_text(inspector, "transform.width") == "100");
    REQUIRE(editor.undo());
    REQUIRE(get_text(inspector, "transform.x") == "0");
    REQUIRE(get_line(inspector, "transform.x").text() == "0");
    REQUIRE(editor.undo());
    REQUIRE(document.get(ids.m_background).m_header.m_opacity == 1);
  }

  TEST_CASE("choice_edits") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    auto& history = editor.get_history();
    editor.get_selection().select(ids.m_background);
    auto blend_mode = qobject_cast<QComboBox*>(
      inspector.find_field("appearance.blend_mode"));
    REQUIRE(blend_mode);
    auto multiply = blend_mode->findText("Multiply");
    REQUIRE(multiply > 0);
    blend_mode->setCurrentIndex(multiply);
    Q_EMIT blend_mode->activated(multiply);
    REQUIRE(document.get(ids.m_background).m_header.m_blend_mode ==
      BlendMode::MULTIPLY);
    REQUIRE(history.get_undo_label() == "Change Blend Mode");
    REQUIRE(inspector.find_field("fill.paint"));
    auto fill =qobject_cast<QCheckBox*>(inspector.find_field("fill.enabled"));
    REQUIRE(fill);
    REQUIRE(fill->isChecked());
    auto paint = document.get(ids.m_background).get_shape()->m_style.m_fill;
    fill->click();
    QCoreApplication::processEvents();
    REQUIRE(get_text(inspector, "fill.enabled") == "No");
    REQUIRE(document.get(ids.m_background).get_shape()->m_style.m_fill ==
      disable_fill(paint));
    REQUIRE(history.get_undo_count() == 2);
    fill = qobject_cast<QCheckBox*>(inspector.find_field("fill.enabled"));
    REQUIRE(fill);
    fill->click();
    QCoreApplication::processEvents();
    REQUIRE(get_text(inspector, "fill.enabled") == "Yes");
    REQUIRE(document.get(ids.m_background).get_shape()->m_style.m_fill ==
      paint);
    REQUIRE(history.get_undo_count() == 3);
    auto border =
      qobject_cast<QCheckBox*>(inspector.find_field("stroke.enabled"));
    REQUIRE(border);
    border->click();
    QCoreApplication::processEvents();
    REQUIRE(get_text(inspector, "stroke.enabled") == "Yes");
    REQUIRE(history.get_undo_count() == 4);
  }

  TEST_CASE("multiple_selection") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_background).m_header;
      header.m_opacity = 0.5;
      transaction.replace_header(header);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(
      std::vector<NodeId>({ids.m_background, ids.m_ellipse}));
    REQUIRE(inspector.get_nodes().size() == 2);
    REQUIRE(inspector.get_current() == ids.m_ellipse);
    REQUIRE(is_mixed(inspector, "appearance.opacity"));
    REQUIRE(get_line(inspector, "appearance.opacity").text().isEmpty());
    REQUIRE(get_line(inspector, "appearance.opacity").placeholderText() ==
      QString(QChar(0x2014)));
    REQUIRE(!is_mixed(inspector, "appearance.blend_mode"));
    REQUIRE(is_mixed(inspector, "fill"));
    REQUIRE(!find_property(inspector.get_properties(), "fill.color"));
    REQUIRE(!find_property(inspector.get_properties(), "geometry"));
    REQUIRE(get_text(inspector, "transform.x") == "0");
    REQUIRE(get_text(inspector, "transform.y") == "2");
    REQUIRE(is_mixed(inspector, "transform.width"));
    REQUIRE(is_mixed(inspector, "transform.height"));
    REQUIRE(get_line(inspector, "transform.width").text().isEmpty());
    REQUIRE(!is_mixed(inspector, "transform.x"));
    enter(inspector, "appearance.opacity", "80");
    REQUIRE(document.get(ids.m_background).m_header.m_opacity == 0.8);
    REQUIRE(document.get(ids.m_ellipse).m_header.m_opacity == 0.8);
    REQUIRE(!is_mixed(inspector, "appearance.opacity"));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    enter(inspector, "transform.x", "10");
    REQUIRE(get_text(inspector, "transform.x") == "10");
    REQUIRE(get_world_transform(document, ids.m_ellipse).m_tx == 110);
    REQUIRE(get_world_transform(document, ids.m_background).m_tx == 120);
    REQUIRE(editor.get_history().get_undo_count() == 2);
    enter(inspector, "transform.width", "40");
    REQUIRE(get_text(inspector, "transform.width") == "40");
    REQUIRE(!is_mixed(inspector, "transform.width"));
    REQUIRE(is_mixed(inspector, "transform.height"));
    REQUIRE(get_text(inspector, "transform.x") == "10");
    REQUIRE(editor.get_history().get_undo_label() == "Resize");
    REQUIRE(editor.get_history().get_undo_count() == 3);
  }

  TEST_CASE("orientation") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(std::vector<NodeId>({ids.m_artboard}));
    auto field = inspector.find_field("transform.orientation");
    REQUIRE(field);
    auto buttons = field->findChildren<QToolButton*>();
    REQUIRE(buttons.size() == 2);
    REQUIRE(buttons[0]->isChecked());
    REQUIRE(!buttons[1]->isChecked());
    buttons[1]->click();
    QCoreApplication::processEvents();
    REQUIRE(document.get(ids.m_artboard).get_artboard()->m_frame ==
      Rect(100, 50, 812, 375));
    REQUIRE(get_text(inspector, "transform.orientation") == "Landscape");
    REQUIRE(editor.get_history().get_undo_label() == "Resize");
  }

  TEST_CASE("action_buttons") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto triggered = 0;
    actions.set_handler("object.align.left", [&] { ++triggered; });
    auto inspector = PropertyInspector();
    inspector.set_actions(&actions);
    inspector.set_document(&document);
    inspector.set_current(ids.m_background);
    auto align = inspector.find_section("align");
    REQUIRE(align);
    auto buttons = align->findChildren<QToolButton*>();
    auto left = static_cast<QToolButton*>(nullptr);
    for(auto button : buttons) {
      if(button->toolTip() == "Align Left") {
        left = button;
      } else {
        REQUIRE(!button->isEnabled());
      }
    }
    REQUIRE(left);
    REQUIRE(left->isEnabled());
    left->click();
    REQUIRE(triggered == 1);
    actions.set_enabled("object.align.left", false);
    REQUIRE(!left->isEnabled());
  }

  TEST_CASE("component_section") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto editor = Editor(document);
    auto component_actions = ComponentActions(actions);
    component_actions.set_editor(&editor);
    auto inspector = PropertyInspector();
    inspector.set_actions(&actions);
    auto editors = make_standard_property_editors(estimate_text_bounds);
    add_component_property_editors(editors);
    inspector.set_property_editors(std::move(editors));
    inspector.set_editor(&editor);
    editor.get_selection().select(ids.m_instance);
    auto section = inspector.find_section("component");
    REQUIRE(section);
    auto edit_main = static_cast<QToolButton*>(nullptr);
    for(auto button : section->findChildren<QToolButton*>()) {
      if(button->text() == "Edit Main Component") {
        edit_main = button;
      }
    }
    REQUIRE(edit_main);
    REQUIRE(edit_main->isEnabled());
    edit_main->click();
    auto main = find_main_component(document, ids.m_component);
    REQUIRE(editor.get_selection().get_primary() == main);
    section = inspector.find_section("component");
    auto add = static_cast<QToolButton*>(nullptr);
    for(auto button : section->findChildren<QToolButton*>()) {
      if(button->toolTip() == "Add State") {
        add = button;
      }
    }
    REQUIRE(add);
    REQUIRE(add->isEnabled());
    REQUIRE(add->menu()->actions().size() == 3);
    add->menu()->actions().front()->trigger();
    REQUIRE(get_text(inspector, "component.state_name") == "State 2");
    auto& state_name = get_line(inspector, "component.state_name");
    state_name.setText("Pressed");
    Q_EMIT state_name.textEdited("Pressed");
    Q_EMIT state_name.editingFinished();
    REQUIRE(document.get_resources().m_components.at(ids.m_component).
      m_states.front().m_name == "Pressed");
    auto combo = qobject_cast<QComboBox*>(
      inspector.find_field("component.state"));
    REQUIRE(combo);
    REQUIRE(combo->count() == 2);
    Q_EMIT combo->activated(0);
    REQUIRE(get_instance_role(document.get(main))->m_state.is_nil());
    REQUIRE(!find_property(inspector.get_properties(), "component.state_name"));
  }

  TEST_CASE("effect_edits") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_background);
    REQUIRE(!inspector.find_field("effects.drop_shadow.x"));
    auto shadow =
      qobject_cast<QCheckBox*>(inspector.find_field("effects.drop_shadow"));
    REQUIRE(shadow);
    shadow->click();
    QCoreApplication::processEvents();
    auto effects = [&] () -> const std::vector<Effect>& {
      return document.get(ids.m_background).get_shape()->m_style.m_effects;
    };
    REQUIRE(effects().size() == 1);
    REQUIRE(get_line(inspector, "effects.drop_shadow.blur").text() == "6");
    enter(inspector, "effects.drop_shadow.blur", "20");
    REQUIRE(std::get<DropShadow>(effects().front()).m_blur == 10);
    enter(inspector, "effects.drop_shadow.x", "4");
    REQUIRE(std::get<DropShadow>(effects().front()).m_offset == Point(4, 3));
    REQUIRE(editor.get_history().get_undo_label() == "Change Effect");
    auto blur = qobject_cast<QCheckBox*>(inspector.find_field("effects.blur"));
    REQUIRE(blur);
    blur->click();
    QCoreApplication::processEvents();
    REQUIRE(effects().size() == 2);
    REQUIRE(get_line(inspector, "effects.blur.amount").text() == "30");
    REQUIRE(get_line(inspector, "effects.blur.brightness").text() == "15");
    REQUIRE(get_line(inspector, "effects.blur.opacity").text() == "0%");
    enter(inspector, "effects.blur.amount", "12");
    auto type =
      qobject_cast<QComboBox*>(inspector.find_field("effects.blur.type"));
    REQUIRE(type);
    Q_EMIT type->activated(1);
    QCoreApplication::processEvents();
    REQUIRE(!std::get<Blur>(effects().back()).m_is_background);
    REQUIRE(std::get<Blur>(effects().back()).m_amount == 12);
    REQUIRE(inspector.find_field("effects.blur.amount"));
    REQUIRE(!inspector.find_field("effects.blur.brightness"));
    Q_EMIT qobject_cast<QComboBox*>(
      inspector.find_field("effects.blur.type"))->activated(0);
    QCoreApplication::processEvents();
    REQUIRE(std::get<Blur>(effects().back()).m_amount == 12);
    REQUIRE(std::get<Blur>(effects().back()).m_brightness == 15);
  }

  TEST_CASE("transform_toggles") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_background);
    auto lock =
      qobject_cast<QToolButton*>(inspector.find_field("transform.lock_aspect"));
    REQUIRE(lock);
    REQUIRE(!lock->isChecked());
    lock->click();
    REQUIRE(document.get(ids.m_background).m_header.m_aspect_lock ==
      Size(100, 50));
    REQUIRE(lock->isChecked());
    enter(inspector, "transform.width", "200");
    REQUIRE(get_text(inspector, "transform.height") == "100");
    auto fixed = qobject_cast<QCheckBox*>(
      inspector.find_field("transform.fixed_position"));
    REQUIRE(fixed);
    fixed->click();
    REQUIRE(document.get(ids.m_background).m_header.m_is_fixed_position);
    auto mark =
      qobject_cast<QCheckBox*>(inspector.find_field("export.marked"));
    REQUIRE(mark);
    mark->click();
    REQUIRE(document.get(ids.m_background).m_header.m_is_marked_for_export);
    REQUIRE(editor.get_history().get_undo_label() == "Mark for Export");
  }

  TEST_CASE("constraints") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_background);
    REQUIRE(get_text(inspector, "layout.responsive.mode") == "Auto");
    auto responsive = qobject_cast<QAbstractButton*>(
      inspector.find_field("layout.responsive"));
    REQUIRE(responsive);
    responsive->click();
    QCoreApplication::processEvents();
    REQUIRE(
      !document.get(ids.m_background).m_header.m_constraints.m_is_enabled);
    REQUIRE(!find_property(
      inspector.get_properties(), "layout.responsive.mode"));
    responsive = qobject_cast<QAbstractButton*>(
      inspector.find_field("layout.responsive"));
    REQUIRE(responsive);
    responsive->click();
    QCoreApplication::processEvents();
    REQUIRE(document.get(ids.m_background).m_header.m_constraints.m_is_enabled);
    REQUIRE(get_text(inspector, "layout.responsive.mode") == "Auto");
    REQUIRE(!inspector.find_field("layout.responsive.width"));
    auto section = inspector.find_section("layout");
    auto manual = static_cast<QToolButton*>(nullptr);
    for(auto button : section->findChildren<QToolButton*>()) {
      if(button->text() == "Manual") {
        manual = button;
      }
    }
    REQUIRE(manual);
    manual->click();
    QCoreApplication::processEvents();
    REQUIRE(document.get(ids.m_background).m_header.m_constraints.m_is_custom);
    auto width = qobject_cast<QCheckBox*>(
      inspector.find_field("layout.responsive.width"));
    REQUIRE(width);
    auto is_fixed =
      document.get(ids.m_background).m_header.m_constraints.m_width;
    width->click();
    REQUIRE(document.get(ids.m_background).m_header.m_constraints.m_width ==
      !is_fixed);
  }

  TEST_CASE("locked_selection") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_background);
    REQUIRE(!inspector.is_locked());
    REQUIRE(get_line(inspector, "appearance.opacity").isEnabled());
    REQUIRE(editor.execute(std::make_unique<SetLockCommand>(
      std::vector{ids.m_background}, true)));
    QCoreApplication::processEvents();
    REQUIRE(inspector.is_locked());
    REQUIRE(!inspector.find_section("transform")->isEnabled());
    REQUIRE(!get_line(inspector, "appearance.opacity").isEnabled());
    REQUIRE(!inspector.open_color_picker("fill.paint"));
  }

  TEST_CASE("disabled_fill_picker") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_background);
    auto paint = document.get(ids.m_background).get_shape()->m_style.m_fill;
    qobject_cast<QCheckBox*>(inspector.find_field("fill.enabled"))->click();
    QCoreApplication::processEvents();
    REQUIRE(document.get(ids.m_background).get_shape()->m_style.m_fill ==
      disable_fill(paint));
    REQUIRE(inspector.open_color_picker("fill.paint"));
    auto& picker = inspector.get_color_picker();
    REQUIRE(picker.get_color() == std::get<SolidFill>(paint).m_color);
    picker.apply_color(make_color(0, 128, 0));
    picker.hide();
    REQUIRE(document.get(ids.m_background).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 128, 0))));
    REQUIRE(get_text(inspector, "fill.enabled") == "Yes");
  }

  TEST_CASE("color_picker") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_background);
    auto& history = editor.get_history();
    REQUIRE(inspector.open_color_picker("fill.paint"));
    auto& picker = inspector.get_color_picker();
    REQUIRE(picker.is_gradient_enabled());
    REQUIRE(picker.get_color() == make_color(255, 0, 0));
    picker.apply_color(make_color(0, 128, 0));
    picker.apply_type(1);
    QCoreApplication::processEvents();
    auto& fill = document.get(ids.m_background).get_shape()->m_style.m_fill;
    auto gradient = std::get_if<GradientFill>(&fill);
    REQUIRE(gradient);
    REQUIRE(gradient->m_stops.front().m_color == make_color(0, 128, 0));
    picker.hide();
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Change Fill");
    REQUIRE(inspector.open_color_picker("stroke.color"));
    REQUIRE(!picker.is_gradient_enabled());
    picker.apply_color(make_color(0, 0, 255));
    picker.hide();
    REQUIRE(document.get(ids.m_background).get_shape()->m_style.m_stroke->
      m_color == make_color(0, 0, 255));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    REQUIRE(document.get(ids.m_background).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(255, 0, 0))));
  }

  TEST_CASE("grid_edits") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_artboard);
    REQUIRE(inspector.find_field("transform.lock_aspect"));
    auto visible =
      qobject_cast<QCheckBox*>(inspector.find_field("grid.visible"));
    REQUIRE(visible);
    visible->click();
    QCoreApplication::processEvents();
    auto& grid = document.get(ids.m_artboard).get_artboard()->m_layout_grid;
    REQUIRE(grid);
    REQUIRE(grid->m_is_visible);
    REQUIRE(get_line(inspector, "grid.columns").text() == "4");
    REQUIRE(get_line(inspector, "grid.margin_linked").text() == "36");
    enter(inspector, "grid.columns", "6");
    REQUIRE(document.get(ids.m_artboard).get_artboard()->m_layout_grid->
      m_column_count == 6);
    enter(inspector, "grid.margin_linked", "20");
    REQUIRE(document.get(ids.m_artboard).get_artboard()->m_layout_grid->
      m_margins.m_right == 20);
    REQUIRE(get_display(inspector, "grid.column_width").font().italic());
    auto use_default = static_cast<QToolButton*>(nullptr);
    for(auto button :
        inspector.find_section("grid")->findChildren<QToolButton*>()) {
      if(button->text() == "Use Default") {
        use_default = button;
      }
    }
    REQUIRE(use_default);
    REQUIRE(use_default->isEnabled());
    use_default->click();
    QCoreApplication::processEvents();
    REQUIRE(document.get(ids.m_artboard).get_artboard()->m_layout_grid->
      m_column_count == 4);
    REQUIRE(get_line(inspector, "grid.margin_linked").text() == "36");
    auto scrolling =
      qobject_cast<QComboBox*>(inspector.find_field("layout.scrolling"));
    REQUIRE(scrolling);
    Q_EMIT scrolling->activated(1);
    QCoreApplication::processEvents();
    REQUIRE(document.get(ids.m_artboard).get_artboard()->m_viewport_height ==
      812);
  }

  TEST_CASE("group_style") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    make_editable(inspector, editor);
    editor.get_selection().select(ids.m_card);
    REQUIRE(inspector.open_color_picker("fill.paint"));
    inspector.get_color_picker().apply_color(make_color(0, 0, 0));
    inspector.get_color_picker().hide();
    REQUIRE(document.get(ids.m_background).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 0))));
    REQUIRE(document.get(ids.m_label).get_text()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 0))));
  }
}

#include <vector>
#include <doctest/doctest.h>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
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
}

TEST_SUITE("PropertyInspector") {
  TEST_CASE("current") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    REQUIRE(inspector.get_properties().empty());
    inspector.set_current(ids.m_background);
    REQUIRE(inspector.get_current() == ids.m_background);
    REQUIRE(get_text(inspector, "layer.name") == "Background");
    REQUIRE(inspector.get_view().topLevelItemCount() ==
      static_cast<int>(inspector.get_properties().size()));
    REQUIRE(inspector.get_view().topLevelItem(0)->text(0) == "Layer");
    inspector.set_current(NodeId());
    REQUIRE(inspector.get_properties().empty());
    REQUIRE(inspector.get_view().topLevelItemCount() == 0);
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
      auto header = document.get(ids.m_reference).m_header;
      header.m_name = "Renamed";
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(get_text(inspector, "layer.name") == "Renamed");
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_instance);
      transaction.commit();
    }
    REQUIRE(inspector.get_current().is_nil());
    REQUIRE(inspector.get_properties().empty());
  }

  TEST_CASE("collapsed_sections") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto inspector = PropertyInspector();
    inspector.set_document(&document);
    inspector.set_current(ids.m_background);
    auto& view = inspector.get_view();
    REQUIRE(view.topLevelItem(0)->isExpanded());
    view.collapseItem(view.topLevelItem(0));
    inspector.set_current(ids.m_ellipse);
    REQUIRE(!view.topLevelItem(0)->isExpanded());
    REQUIRE(view.topLevelItem(1)->isExpanded());
    REQUIRE(!inspector.find_field("appearance.opacity"));
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
    auto lock = qobject_cast<QCheckBox*>(inspector.find_field("layer.locked"));
    REQUIRE(lock);
    lock->click();
    REQUIRE(document.get(ids.m_background).m_header.m_is_locked);
    REQUIRE(history.get_undo_label() == "Lock");
    auto blend_mode = qobject_cast<QComboBox*>(
      inspector.find_field("appearance.blend_mode"));
    REQUIRE(blend_mode);
    blend_mode->setCurrentIndex(1);
    Q_EMIT blend_mode->activated(1);
    REQUIRE(document.get(ids.m_background).m_header.m_blend_mode ==
      BlendMode::MULTIPLY);
    REQUIRE(history.get_undo_label() == "Change Blend Mode");
    enter(inspector, "fill.color", "#00ff00");
    REQUIRE(get_text(inspector, "fill.color") == "#00FF00");
    enter(inspector, "fill.color", "#0000FF 50%");
    REQUIRE(get_text(inspector, "fill.color") == "#0000FF 50%");
    enter(inspector, "fill.color", "blue");
    REQUIRE(get_line(inspector, "fill.color").text() == "#0000FF 50%");
    REQUIRE(history.get_undo_count() == 3);
    auto fill = qobject_cast<QComboBox*>(inspector.find_field("fill"));
    REQUIRE(fill);
    fill->setCurrentIndex(0);
    Q_EMIT fill->activated(0);
    QCoreApplication::processEvents();
    REQUIRE(get_text(inspector, "fill") == "None");
    REQUIRE(!inspector.find_field("fill.color"));
    REQUIRE(history.get_undo_count() == 4);
    auto& name = get_line(inspector, "layer.name");
    name.setText("B");
    Q_EMIT name.textEdited("B");
    name.setText("Box");
    Q_EMIT name.textEdited("Box");
    Q_EMIT name.editingFinished();
    REQUIRE(document.get(ids.m_background).m_header.m_name == "Box");
    REQUIRE(history.get_undo_count() == 5);
    REQUIRE(history.get_undo_label() == "Rename");
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
    REQUIRE(!is_mixed(inspector, "appearance.blend_mode"));
    REQUIRE(is_mixed(inspector, "fill"));
    REQUIRE(!find_property(inspector.get_properties(), "fill.color"));
    REQUIRE(!find_property(inspector.get_properties(), "geometry.center"));
    REQUIRE(get_text(inspector, "transform.x") == "0");
    REQUIRE(get_text(inspector, "transform.y") == "2");
    REQUIRE(get_text(inspector, "transform.width") == "110");
    REQUIRE(get_text(inspector, "transform.height") == "68");
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
  }
}

#include <memory>
#include <vector>
#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("LayersPanel") {
  TEST_CASE("current") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto panel = LayersPanel();
    auto changes = std::vector<NodeId>();
    panel.set_current_handler([&] (const auto& current) {
      changes.push_back(current);
    });
    panel.set_document(&document);
    REQUIRE(panel.get_current().is_nil());
    panel.set_current(ids.m_label);
    REQUIRE(panel.get_current() == ids.m_label);
    REQUIRE(changes == std::vector<NodeId>({ids.m_label}));
    auto& model = panel.get_model();
    REQUIRE(panel.get_view().isExpanded(model.find(ids.m_card)));
    REQUIRE(panel.get_view().isExpanded(model.find(ids.m_artboard)));
    panel.get_view().setCurrentIndex(model.find(ids.m_loose));
    REQUIRE(panel.get_current() == ids.m_loose);
    panel.set_current(ids.m_master_shape);
    REQUIRE(panel.get_current().is_nil());
    REQUIRE(changes ==
      std::vector<NodeId>({ids.m_label, ids.m_loose, NodeId()}));
  }

  TEST_CASE("removed_current") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto panel = LayersPanel();
    panel.set_document(&document);
    panel.set_current(ids.m_background);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_card);
      transaction.commit();
    }
    REQUIRE(panel.get_current().is_nil());
    panel.set_current(ids.m_ellipse);
    REQUIRE(panel.get_current() == ids.m_ellipse);
    panel.set_document(nullptr);
    REQUIRE(panel.get_current().is_nil());
  }

  TEST_CASE("editor_selection") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    REQUIRE(panel.get_editor() == &editor);
    auto& model = panel.get_model();
    auto& view = panel.get_view();
    editor.get_selection().select(
      std::vector<NodeId>({ids.m_ellipse, ids.m_label}));
    REQUIRE(panel.get_current() == ids.m_label);
    REQUIRE(view.selectionModel()->isSelected(model.find(ids.m_ellipse)));
    REQUIRE(view.selectionModel()->isSelected(model.find(ids.m_label)));
    REQUIRE(view.isExpanded(model.find(ids.m_card)));
    REQUIRE(view.currentIndex() == model.find(ids.m_label));
    REQUIRE(view.selectionModel()->selectedRows().size() == 2);
    editor.get_selection().clear();
    REQUIRE(view.selectionModel()->selectedRows().isEmpty());
    REQUIRE(panel.get_current().is_nil());
    panel.set_current(ids.m_card);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_card}));
    panel.set_editor(nullptr);
    REQUIRE(panel.get_current().is_nil());
    REQUIRE(model.rowCount() == 0);
  }

  TEST_CASE("scope") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    auto& model = panel.get_model();
    auto& view = panel.get_view();
    REQUIRE(panel.get_scope().is_nil());
    REQUIRE(panel.get_heading() == "ARTBOARDS");
    REQUIRE(!view.rootIndex().isValid());
    editor.get_selection().select(ids.m_artboard);
    REQUIRE(panel.get_scope().is_nil());
    editor.get_selection().select(ids.m_label);
    REQUIRE(panel.get_scope() == ids.m_artboard);
    REQUIRE(panel.get_heading() == "HOME");
    REQUIRE(view.rootIndex() == model.find(ids.m_artboard));
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_artboard).m_header;
      header.m_name = "Start";
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(panel.get_heading() == "START");
    editor.get_selection().select(ids.m_loose);
    REQUIRE(panel.get_scope().is_nil());
    REQUIRE(panel.get_heading() == "ARTBOARDS");
    REQUIRE(!view.rootIndex().isValid());
  }

  TEST_CASE("rename") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    auto& model = panel.get_model();
    REQUIRE(!model.flags(model.find(ids.m_card)).testFlag(
      Qt::ItemIsEditable));
    REQUIRE(!panel.rename(ids.m_card));
    panel.set_editor(&editor);
    auto index = model.find(ids.m_card);
    REQUIRE(model.flags(index).testFlag(Qt::ItemIsEditable));
    REQUIRE(!model.flags(model.find(ids.m_card, LayersModel::LOCK)).testFlag(
      Qt::ItemIsEditable));
    REQUIRE(model.setData(index, QString("Panel"), Qt::EditRole));
    REQUIRE(document.get(ids.m_card).m_header.m_name == "Panel");
    REQUIRE(editor.get_history().get_undo_label() == "Rename");
    REQUIRE(!model.setData(index, QString(" "), Qt::EditRole));
    REQUIRE(document.get(ids.m_card).m_header.m_name == "Panel");
  }

  TEST_CASE("tree_selection") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    auto activations = std::vector<NodeId>();
    panel.set_activation_handler([&] (const auto& id) {
      activations.push_back(id);
    });
    panel.set_editor(&editor);
    auto& model = panel.get_model();
    auto& view = panel.get_view();
    view.setCurrentIndex(model.find(ids.m_ellipse));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_ellipse}));
    REQUIRE(activations == std::vector<NodeId>({ids.m_ellipse}));
    view.selectionModel()->setCurrentIndex(model.find(ids.m_loose),
      QItemSelectionModel::Select | QItemSelectionModel::Rows);
    auto& selection = editor.get_selection();
    REQUIRE(selection.get_size() == 2);
    REQUIRE(selection.contains(ids.m_ellipse));
    REQUIRE(selection.contains(ids.m_loose));
    REQUIRE(selection.get_primary() == ids.m_loose);
    REQUIRE(panel.get_current() == ids.m_loose);
    REQUIRE(activations == std::vector<NodeId>({ids.m_ellipse, ids.m_loose}));
    view.selectionModel()->select(model.find(ids.m_ellipse),
      QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
    REQUIRE(selection.get_nodes() == std::vector<NodeId>({ids.m_loose}));
  }

  TEST_CASE("search") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto panel = LayersPanel();
    panel.set_document(&document);
    REQUIRE(panel.get_search().isEmpty());
    panel.set_search("back");
    REQUIRE(!panel.is_filtered_out(ids.m_background));
    REQUIRE(!panel.is_filtered_out(ids.m_card));
    REQUIRE(panel.is_filtered_out(ids.m_ellipse));
    REQUIRE(panel.is_filtered_out(ids.m_loose));
    REQUIRE(panel.get_view().isExpanded(panel.get_model().find(ids.m_card)));
    panel.set_search("");
    REQUIRE(!panel.is_filtered_out(ids.m_loose));
    panel.set_filter(LayerFilter::TEXT);
    REQUIRE(panel.get_filter() == LayerFilter::TEXT);
    REQUIRE(!panel.is_filtered_out(ids.m_label));
    REQUIRE(panel.is_filtered_out(ids.m_background));
    REQUIRE(panel.is_filtered_out(ids.m_loose));
    panel.set_filter(LayerFilter::SHAPES);
    REQUIRE(!panel.is_filtered_out(ids.m_background));
    REQUIRE(panel.is_filtered_out(ids.m_label));
    {
      auto transaction = Transaction(document);
      auto shape = *document.get(ids.m_ellipse).get_shape();
      auto image = ImageFill();
      image.m_image = generate_node_id();
      shape.m_style.m_fill = image;
      transaction.replace_payload(ids.m_ellipse, shape);
      transaction.commit();
    }
    panel.set_filter(LayerFilter::IMAGES);
    REQUIRE(!panel.is_filtered_out(ids.m_ellipse));
    REQUIRE(panel.is_filtered_out(ids.m_background));
    panel.set_filter(LayerFilter::VIDEO);
    REQUIRE(panel.is_filtered_out(ids.m_ellipse));
    panel.set_filter(LayerFilter::ALL);
    REQUIRE(!panel.is_filtered_out(ids.m_label));
  }

  TEST_CASE("toggles") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    REQUIRE(panel.toggle_visibility(ids.m_ellipse));
    REQUIRE(!document.get(ids.m_ellipse).m_header.m_is_visible);
    REQUIRE(editor.get_history().get_undo_label() == "Hide");
    REQUIRE(panel.toggle_visibility(ids.m_ellipse));
    REQUIRE(document.get(ids.m_ellipse).m_header.m_is_visible);
    REQUIRE(panel.toggle_lock(ids.m_ellipse));
    REQUIRE(document.get(ids.m_ellipse).m_header.m_is_locked);
    REQUIRE(editor.get_history().get_undo_label() == "Lock");
    REQUIRE(editor.get_selection().is_empty());
  }

  TEST_CASE("move") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    REQUIRE(!panel.move(std::vector{ids.m_artboard}, ids.m_card, 0));
    REQUIRE(!panel.move(std::vector{ids.m_card}, ids.m_card, 0));
    REQUIRE(!panel.move(std::vector{ids.m_ellipse}, ids.m_instance, 0));
    REQUIRE(panel.move(std::vector{ids.m_ellipse}, ids.m_card, 2));
    REQUIRE(document.get(ids.m_ellipse).m_parent == ids.m_card);
    REQUIRE(document.get(ids.m_card).m_children.back() == ids.m_ellipse);
    REQUIRE(panel.move(std::vector{ids.m_ellipse}, ids.m_card, 0));
    REQUIRE(document.get(ids.m_card).m_children.front() == ids.m_ellipse);
  }

  TEST_CASE("context_menu") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    actions.set_handler("edit.copy", [] {});
    actions.set_handler("edit.rename", [] {});
    auto panel = LayersPanel();
    panel.set_actions(&actions);
    panel.set_editor(&editor);
    auto menu = std::unique_ptr<QMenu>(panel.make_context_menu(nullptr));
    auto texts = QStringList();
    for(auto action : menu->actions()) {
      if(!action->isSeparator()) {
        texts.push_back(action->text());
      }
    }
    REQUIRE(texts == QStringList({"Copy", "Paste", "Rename",
      "Export Batch...", "Export Selected..."}));
  }

  TEST_CASE("keyboard_navigation") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    editor.get_selection().select(ids.m_ellipse);
    auto& view = panel.get_view();
    auto down = QKeyEvent(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &down);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_card}));
    panel.set_search("o");
    auto up = QKeyEvent(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &up);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_instance}));
  }
}
#include <vector>
#include <doctest/doctest.h>
#include <QItemSelectionModel>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
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
}

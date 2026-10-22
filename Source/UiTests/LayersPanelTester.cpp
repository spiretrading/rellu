#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <doctest/doctest.h>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPalette>
#include <QToolButton>
#include <QTreeView>
#include <QWidget>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/ToolTips.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  void click(QTreeView& view, const QModelIndex& index, bool is_icon) {
    auto rect = view.visualRect(index);
    auto point = QPointF(is_icon ? rect.left() + 4 : rect.center().x(),
      rect.center().y());
    auto press = QMouseEvent(QEvent::MouseButtonPress, point,
      view.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton,
      Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &press);
    auto release = QMouseEvent(QEvent::MouseButtonRelease, point,
      view.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton,
      Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &release);
  }
}

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
    REQUIRE(panel.get_scope() == LayersModel::PASTEBOARD);
    REQUIRE(panel.get_heading() == "PASTEBOARD");
    REQUIRE(view.rootIndex() == model.find(LayersModel::PASTEBOARD));
    editor.get_selection().clear();
    REQUIRE(panel.get_scope().is_nil());
    REQUIRE(panel.get_heading() == "ARTBOARDS");
    REQUIRE(!view.rootIndex().isValid());
  }

  TEST_CASE("context_rows") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    panel.resize(300, 600);
    auto& model = panel.get_model();
    auto& view = panel.get_view();
    REQUIRE(view.indentation() == 20);
    auto palette = view.palette();
    palette.setColor(QPalette::Base, QColor(0xF5, 0xF5, 0xF5));
    view.setPalette(palette);
    editor.get_selection().select(ids.m_label);
    REQUIRE(editor.get_context().enter(ids.m_card));
    panel.show();
    QCoreApplication::processEvents();
    auto color_of = [&] (const NodeId& id) {
      auto rect = view.visualRect(model.find(id));
      auto image = view.viewport()->grab().toImage();
      return image.pixelColor(1, rect.center().y());
    };
    REQUIRE(color_of(ids.m_card) == QColor(0xFF, 0xFF, 0xFF));
    REQUIRE(color_of(ids.m_background) == QColor(0xFF, 0xFF, 0xFF));
    REQUIRE(color_of(ids.m_ellipse) == QColor(0xF5, 0xF5, 0xF5));
    editor.get_context().exit();
    QCoreApplication::processEvents();
    REQUIRE(color_of(ids.m_card) == QColor(0xF5, 0xF5, 0xF5));
    panel.hide();
  }

  TEST_CASE("elided_name_tool_tip") {
    auto filter = ToolTipFilter();
    QCoreApplication::instance()->installEventFilter(&filter);
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    panel.resize(240, 600);
    editor.get_selection().select(ids.m_ellipse);
    panel.show();
    QCoreApplication::processEvents();
    auto& model = panel.get_model();
    auto& view = panel.get_view();
    auto find_tip = [] {
      for(auto widget : QApplication::topLevelWidgets()) {
        if(widget->objectName() == "rellu_tool_tip" && widget->isVisible()) {
          return widget;
        }
      }
      return static_cast<QWidget*>(nullptr);
    };
    auto hover = [&] (const NodeId& id) {
      auto center = view.visualRect(model.find(id)).center();
      auto help = QHelpEvent(
        QEvent::ToolTip, center, view.viewport()->mapToGlobal(center));
      QCoreApplication::sendEvent(view.viewport(), &help);
    };
    hover(ids.m_card);
    REQUIRE(!find_tip());
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_card).m_header;
      header.m_name = "Rectangle with a very long name that the row has to "
        "elide because it is far too wide END";
      transaction.replace_header(header);
      transaction.commit();
    }
    QCoreApplication::processEvents();
    hover(ids.m_card);
    auto tip = find_tip();
    REQUIRE(tip);
    REQUIRE(tip->height() > 2 * QFontMetrics(QApplication::font()).height());
    QCoreApplication::instance()->removeEventFilter(&filter);
  }

  TEST_CASE("pasteboard") {
    set_language(Language::GERMAN);
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    auto& model = panel.get_model();
    auto& view = panel.get_view();
    panel.resize(300, 600);
    panel.show();
    auto pasteboard = model.find(LayersModel::PASTEBOARD);
    auto name = model.data(pasteboard, Qt::DisplayRole).toString();
    editor.get_selection().select(ids.m_ellipse);
    view.setCurrentIndex(pasteboard);
    REQUIRE(panel.get_scope().is_nil());
    REQUIRE(view.selectionModel()->isSelected(pasteboard));
    REQUIRE(editor.get_selection().is_empty());
    REQUIRE(panel.get_current().is_nil());
    auto center = view.visualRect(pasteboard).center();
    auto click = QMouseEvent(QEvent::MouseButtonDblClick, QPointF(center),
      view.viewport()->mapToGlobal(QPointF(center)), Qt::LeftButton,
      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &click);
    auto heading = panel.get_heading();
    set_language(Language::ENGLISH);
    REQUIRE(name == QString::fromUtf8("Montagefl\xC3\xA4" "che"));
    REQUIRE(heading == QString::fromUtf8("MONTAGEFL\xC3\x84" "CHE"));
    REQUIRE(panel.get_scope() == LayersModel::PASTEBOARD);
    REQUIRE(view.rootIndex() == pasteboard);
    REQUIRE(!model.data(pasteboard, Qt::DecorationRole).isNull());
    REQUIRE(!model.flags(pasteboard).testFlag(Qt::ItemIsEditable));
    REQUIRE(!model.flags(pasteboard).testFlag(Qt::ItemIsDragEnabled));
    REQUIRE(model.flags(pasteboard).testFlag(Qt::ItemIsDropEnabled));
    auto buttons = panel.findChildren<QToolButton*>();
    auto back = std::find_if(buttons.begin(), buttons.end(),
      [] (const auto& button) {
        return button->objectName() == "panel_button" && !button->menu();
      });
    REQUIRE(back != buttons.end());
    (*back)->click();
    REQUIRE(panel.get_scope().is_nil());
    REQUIRE(!view.rootIndex().isValid());
    editor.get_selection().select(ids.m_loose);
    REQUIRE(panel.get_scope() == LayersModel::PASTEBOARD);
    (*back)->click();
    REQUIRE(panel.get_scope().is_nil());
    REQUIRE(editor.get_selection().is_empty());
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_artboard);
      transaction.commit();
    }
    REQUIRE(!model.find(LayersModel::PASTEBOARD).isValid());
    REQUIRE(model.find(ids.m_loose).parent() == QModelIndex());
    panel.hide();
  }

  TEST_CASE("canvas") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_artboard);
      transaction.commit();
    }
    auto folder = NodeId();
    {
      auto transaction = Transaction(document);
      folder = transaction.append(NodeId(), make_header("Folder"), Group());
      transaction.append(
        folder, make_header("Inner"), make_rectangle_shape(5, 5));
      transaction.commit();
    }
    auto panel = LayersPanel();
    panel.set_document(&document);
    panel.resize(300, 600);
    panel.show();
    auto& view = panel.get_view();
    auto& model = panel.get_model();
    REQUIRE(panel.get_heading() == "CANVAS");
    REQUIRE(view.itemsExpandable());
    REQUIRE(!view.rootIsDecorated());
    REQUIRE(panel.get_scope().is_nil());
    auto index = model.find(folder);
    REQUIRE(model.data(index, LayersModel::EXPANDED_ICON_ROLE).isValid());
    REQUIRE(!model.data(model.find(ids.m_loose),
      LayersModel::EXPANDED_ICON_ROLE).isValid());
    click(view, index, true);
    REQUIRE(view.isExpanded(index));
    REQUIRE(panel.get_current() == folder);
    click(view, index, true);
    REQUIRE(!view.isExpanded(index));
    REQUIRE(panel.get_current() == folder);
    panel.hide();
    {
      auto transaction = Transaction(document);
      auto artboard = Artboard();
      artboard.m_frame = Rect(0, 0, 10, 10);
      transaction.append(NodeId(), make_header("Board"), artboard);
      transaction.commit();
    }
    REQUIRE(panel.get_heading() == "ARTBOARDS");
    REQUIRE(!panel.get_view().itemsExpandable());
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
    REQUIRE(panel.is_filtered_out(ids.m_card));
    REQUIRE(panel.is_filtered_out(ids.m_ellipse));
    REQUIRE(panel.is_filtered_out(ids.m_loose));
    auto& results = *panel.get_results().model();
    REQUIRE(results.rowCount() == 2);
    REQUIRE(results.index(0, LayersModel::NAME).data().toString() == "HOME");
    REQUIRE(results.flags(results.index(0, LayersModel::NAME)) ==
      Qt::NoItemFlags);
    REQUIRE(results.index(1, LayersModel::NAME).data().toString() ==
      "Background");
    REQUIRE(!results.hasChildren(results.index(1, LayersModel::NAME)));
    panel.set_search("o");
    auto names = QStringList();
    for(auto row = 0; row != results.rowCount(); ++row) {
      names.push_back(results.index(row, LayersModel::NAME).data().toString());
    }
    REQUIRE(names == QStringList({"HOME", "Home", "Button",
      "Hello world Second line", "Background", "PASTEBOARD", "Loose"}));
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
    REQUIRE(panel.toggle_visibility(ids.m_reference));
    auto& reference = document.get(ids.m_reference);
    REQUIRE(!reference.get_reference());
    REQUIRE(!reference.m_header.m_is_visible);
    REQUIRE(document.get(ids.m_master_shape).m_header.m_is_visible);
    REQUIRE(panel.toggle_visibility(ids.m_reference));
    REQUIRE(document.get(ids.m_reference).m_header.m_is_visible);
  }

  TEST_CASE("compound_operand_visibility") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto operand = NodeId();
    {
      auto transaction = Transaction(document);
      auto compound = Shape();
      compound.m_geometry = CompoundShape(CompoundOperation::UNION,
        Shared(make_rectangle(Rect(0, 0, 5, 5))));
      auto id = transaction.append(
        ids.m_artboard, make_header("Compound"), compound);
      auto shape = Shape();
      shape.m_geometry = RectangleShape(Rect(0, 0, 5, 5));
      operand = transaction.append(id, make_header("Operand"), shape);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    auto& model = panel.get_model();
    REQUIRE(!model.data(model.find(operand, LayersModel::VISIBILITY),
      Qt::DecorationRole).isValid());
    REQUIRE(model.data(model.find(ids.m_ellipse, LayersModel::VISIBILITY),
      Qt::DecorationRole).isValid());
    REQUIRE(!panel.toggle_visibility(operand));
    REQUIRE(document.get(operand).m_header.m_is_visible);
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
    editor.get_selection().select(ids.m_background);
    REQUIRE(panel.get_search() == "o");
    auto up = QKeyEvent(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QCoreApplication::sendEvent(&panel.get_results(), &up);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_label}));
  }

  TEST_CASE("folder_icon") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto panel = LayersPanel();
    panel.set_editor(&editor);
    panel.resize(300, 600);
    panel.show();
    auto& view = panel.get_view();
    auto& model = panel.get_model();
    editor.get_selection().select(ids.m_ellipse);
    REQUIRE(panel.get_scope() == ids.m_artboard);
    REQUIRE(!view.rootIsDecorated());
    auto card = model.find(ids.m_card);
    click(view, card, false);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_card}));
    REQUIRE(!view.isExpanded(card));
    editor.get_selection().select(ids.m_ellipse);
    click(view, card, true);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_card}));
    REQUIRE(view.isExpanded(card));
    click(view, card, true);
    REQUIRE(!view.isExpanded(card));
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_card}));
    panel.set_search("card");
    REQUIRE(!view.isVisible());
    REQUIRE(panel.get_results().isVisible());
    auto& results = panel.get_results();
    auto row = results.model()->index(1, LayersModel::NAME);
    REQUIRE(row.data().toString() == "Card");
    click(results, row, true);
    REQUIRE(editor.get_selection().get_nodes() ==
      std::vector<NodeId>({ids.m_card}));
    REQUIRE(results.model()->rowCount() == 2);
    REQUIRE(results.selectionModel()->isSelected(row));
    editor.get_selection().clear();
    REQUIRE(panel.get_search() == "card");
    REQUIRE(results.isVisible());
    editor.get_selection().select(ids.m_card);
    REQUIRE(panel.get_search() == "card");
    REQUIRE(results.selectionModel()->isSelected(
      results.model()->index(1, LayersModel::NAME)));
    editor.get_selection().clear();
    editor.get_selection().select(
      std::vector<NodeId>({ids.m_card, ids.m_ellipse}), ids.m_ellipse);
    REQUIRE(panel.get_search().isEmpty());
    REQUIRE(view.isVisible());
    REQUIRE(!results.isVisible());
    REQUIRE(panel.get_scope() == ids.m_artboard);
    REQUIRE(panel.get_heading() == "HOME");
    REQUIRE(view.selectionModel()->isSelected(model.find(ids.m_ellipse)));
    REQUIRE(view.selectionModel()->isSelected(model.find(ids.m_card)));
    auto field = panel.findChild<QLineEdit*>("search_field");
    REQUIRE(field->placeholderText() == "All Items");
    panel.hide();
  }

  TEST_CASE("search_field") {
    auto document = Document();
    build_test_document(document);
    auto panel = LayersPanel();
    panel.set_document(&document);
    auto field = panel.findChild<QLineEdit*>("search_field");
    REQUIRE(field);
    panel.set_search("back");
    auto point = QPointF(field->rect().center());
    auto press = QMouseEvent(QEvent::MouseButtonPress, point,
      field->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton,
      Qt::NoModifier);
    QCoreApplication::sendEvent(field, &press);
    REQUIRE(field->selectedText() == "back");
    auto escape = QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(field, &escape);
    REQUIRE(panel.get_search() == "back");
    REQUIRE(panel.get_results().model()->rowCount() == 2);
  }

  TEST_CASE("narrow_search_row") {
    auto panel = LayersPanel();
    panel.resize(118, 400);
    panel.show();
    QCoreApplication::processEvents();
    auto field = panel.findChild<QLineEdit*>("search_field");
    REQUIRE(field);
    REQUIRE(field->mapTo(&panel, QPoint(0, 0)).x() == 12);
    REQUIRE(field->width() == 118 - 12 - 12);
    REQUIRE(field->height() == 30);
    auto buttons = field->findChildren<QToolButton*>();
    auto filter = std::find_if(buttons.begin(), buttons.end(),
      [] (const auto& button) {
        return button->menu() != nullptr;
      });
    REQUIRE(filter != buttons.end());
    REQUIRE((*filter)->geometry() ==
      QRect(field->width() - 21, 0, 21, field->height()));
    REQUIRE((*filter)->cursor().shape() == Qt::ArrowCursor);
    REQUIRE(field->textMargins().left() == 17);
    REQUIRE(field->textMargins().right() == 16);
    REQUIRE(field->actions().empty());
    auto glyphs = field->findChild<QWidget*>("search_glyphs");
    REQUIRE(glyphs);
    REQUIRE(glyphs->geometry() == field->rect());
    auto image = glyphs->grab().toImage();
    auto bounds = [&] (const QRect& area) {
      auto box = QRect();
      for(auto y = area.top(); y <= area.bottom(); ++y) {
        for(auto x = area.left(); x <= area.right(); ++x) {
          if(qAlpha(image.pixel(x, y)) != 0) {
            box |= QRect(x, y, 1, 1);
          }
        }
      }
      return box;
    };
    auto half = image.width() / 2;
    auto line = image.height() - 1;
    REQUIRE(bounds(QRect(0, 0, half, line)) == QRect(1, 9, 9, 9));
    REQUIRE(bounds(QRect(half, 0, image.width() - half, line)) ==
      QRect(field->width() - 14, 11, 9, 6));
    REQUIRE(bounds(QRect(0, line, image.width(), 1)) ==
      QRect(0, line, image.width(), 1));
    REQUIRE(image.pixelColor(half, line) == QColor(0xCA, 0xCA, 0xCA));
    auto heading = panel.findChild<QLabel*>("layers_heading");
    REQUIRE(heading);
    REQUIRE(heading->mapTo(&panel, QPoint(0, 0)) == QPoint(12, 65));
    REQUIRE(heading->height() == 14);
    REQUIRE(heading->contentsMargins().left() == 7);
    auto heading_font = QFont("Segoe UI");
    heading_font.setPixelSize(10);
    heading->setFont(heading_font);
    REQUIRE(heading->fontMetrics().height() <= heading->height());
    heading->setText("ARTBOARDS");
    auto text = QImage(heading->size(), QImage::Format_ARGB32);
    text.fill(Qt::white);
    heading->render(&text);
    auto ink = QRect();
    for(auto y = 0; y != text.height(); ++y) {
      for(auto x = 0; x != text.width(); ++x) {
        if(text.pixelColor(x, y).value() < 200) {
          ink |= QRect(x, y, 1, 1);
        }
      }
    }
    REQUIRE(!ink.isEmpty());
    REQUIRE(ink.left() >= 7);
    REQUIRE(ink.top() > 0);
    REQUIRE(ink.bottom() < text.height() - 1);
    auto tool_buttons = panel.findChildren<QToolButton*>();
    auto back = std::find_if(tool_buttons.begin(), tool_buttons.end(),
      [] (const auto& button) {
        return button->toolTip() == "Artboards";
      });
    REQUIRE(back != tool_buttons.end());
    REQUIRE((*back)->parentWidget() == heading->parentWidget());
    REQUIRE((*back)->geometry() == QRect(12, 20, 20, 20));
    auto view = panel.findChild<QTreeView*>("layers");
    REQUIRE(view);
    REQUIRE(view->mapTo(&panel, QPoint(0, 0)).y() == 80);
    panel.hide();
  }
}

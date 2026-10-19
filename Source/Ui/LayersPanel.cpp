#include <exception>
#include <memory>
#include <utility>
#include <vector>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto INDICATOR_WIDTH = 24;

  class Guard {
    public:
      explicit Guard(bool& flag)
          : m_flag(&flag),
            m_previous(flag) {
        flag = true;
      }

      ~Guard() {
        *m_flag = m_previous;
      }

    private:
      bool* m_flag;
      bool m_previous;

      Guard(const Guard&) = delete;
      Guard& operator =(const Guard&) = delete;
  };

  class StateDelegate : public QStyledItemDelegate {
    public:
      using QStyledItemDelegate::QStyledItemDelegate;

    protected:
      void initStyleOption(QStyleOptionViewItem* option,
          const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if(index.column() == LayersModel::VISIBILITY &&
            index.data(LayersModel::VISIBLE_ROLE).toBool() &&
            !(option->state & QStyle::State_MouseOver)) {
          option->icon = QIcon();
          option->features &= ~QStyleOptionViewItem::HasDecoration;
        }
      }
  };
}

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent),
      m_model(new LayersModel(this)),
      m_view(new QTreeView()),
      m_back(new QToolButton()),
      m_heading(new QLabel()),
      m_editor(nullptr),
      m_is_synchronizing(false) {
  setObjectName("layers_panel");
  auto search = new QWidget();
  auto search_layout = new QHBoxLayout(search);
  search_layout->setContentsMargins(16, 12, 16, 0);
  search_layout->setSpacing(4);
  auto field = new QLineEdit();
  field->setObjectName("search_field");
  field->setPlaceholderText("All Items");
  field->addAction(get_icon(Glyph::SEARCH), QLineEdit::LeadingPosition);
  field->setEnabled(false);
  search_layout->addWidget(field, 1);
  auto filter = new QToolButton();
  filter->setObjectName("panel_button");
  filter->setAutoRaise(true);
  filter->setFocusPolicy(Qt::NoFocus);
  filter->setIcon(get_icon(Glyph::CHEVRON_DOWN));
  filter->setIconSize(QSize(16, 16));
  filter->setFixedSize(24, 24);
  filter->setToolTip("Filter");
  filter->setEnabled(false);
  search_layout->addWidget(filter);
  auto heading = new QWidget();
  auto heading_layout = new QHBoxLayout(heading);
  heading_layout->setContentsMargins(12, 18, 16, 4);
  heading_layout->setSpacing(4);
  m_back->setObjectName("panel_button");
  m_back->setAutoRaise(true);
  m_back->setFocusPolicy(Qt::NoFocus);
  m_back->setIcon(get_icon(Glyph::CHEVRON_LEFT));
  m_back->setIconSize(QSize(14, 14));
  m_back->setFixedSize(20, 20);
  m_back->setToolTip("Back");
  heading_layout->addWidget(m_back);
  m_heading->setObjectName("layers_heading");
  heading_layout->addWidget(m_heading, 1);
  m_view->setObjectName("layers");
  m_view->setModel(m_model);
  m_view->setItemDelegate(new StateDelegate(m_view));
  m_view->setUniformRowHeights(true);
  m_view->setHeaderHidden(true);
  m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_view->setAllColumnsShowFocus(true);
  m_view->setIndentation(16);
  m_view->setIconSize(QSize(16, 16));
  m_view->setFrameShape(QFrame::NoFrame);
  m_view->setMouseTracking(true);
  auto header = m_view->header();
  header->setStretchLastSection(false);
  header->setMinimumSectionSize(INDICATOR_WIDTH);
  header->setSectionResizeMode(LayersModel::NAME, QHeaderView::Stretch);
  for(auto column : {LayersModel::VISIBILITY, LayersModel::LOCK}) {
    header->setSectionResizeMode(column, QHeaderView::Fixed);
    header->resizeSection(column, INDICATOR_WIDTH);
  }
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(search);
  layout->addWidget(heading);
  layout->addWidget(m_view, 1);
  connect(m_back, &QToolButton::clicked, this, [this] {
    show_artboards();
  });
  connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
    this, [this] {
      if(!m_is_synchronizing) {
        on_selection();
      }
    });
  connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
    this, [this] (const QModelIndex& current) {
      if(m_is_synchronizing) {
        return;
      }
      on_selection();
      if(m_activation_handler && current.isValid() &&
          m_view->selectionModel()->isSelected(current)) {
        m_activation_handler(m_model->get_id(current));
      }
    });
  update_scope();
}

LayersModel& LayersPanel::get_model() {
  return *m_model;
}

QTreeView& LayersPanel::get_view() {
  return *m_view;
}

void LayersPanel::set_document(Document* document) {
  m_subscription.release();
  m_document_subscription.release();
  m_editor = nullptr;
  m_model->set_rename_handler(nullptr);
  m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_model->set_document(document);
  if(document) {
    m_document_subscription = document->subscribe([this] (const auto&) {
      update_scope();
    });
  }
  on_current(QModelIndex());
}

Editor* LayersPanel::get_editor() const {
  return m_editor;
}

void LayersPanel::set_editor(Editor* editor) {
  m_subscription.release();
  m_document_subscription.release();
  m_editor = nullptr;
  if(!editor) {
    m_model->set_rename_handler(nullptr);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_model->set_document(nullptr);
    on_current(QModelIndex());
    return;
  }
  m_model->set_document(&editor->get_document());
  m_editor = editor;
  m_model->set_rename_handler([this] (const auto& id, const auto& name) {
    if(!m_editor) {
      return;
    }
    try {
      m_editor->execute(
        std::make_unique<RenameCommand>(id, name.toStdString()));
    } catch(const std::exception&) {}
  });
  m_view->setEditTriggers(
    QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  m_document_subscription =
    editor->get_document().subscribe([this] (const auto&) {
      update_scope();
    });
  m_subscription = m_editor->get_selection().subscribe([this] {
    synchronize();
  });
  synchronize();
}

const NodeId& LayersPanel::get_current() const {
  return m_current;
}

void LayersPanel::set_current(const NodeId& id) {
  if(m_editor) {
    m_editor->get_selection().select(id);
    return;
  }
  auto index = m_model->find(id);
  if(!index.isValid()) {
    m_view->selectionModel()->clear();
    on_current(QModelIndex());
    return;
  }
  for(auto parent = index.parent(); parent.isValid();
      parent = parent.parent()) {
    m_view->expand(parent);
  }
  m_view->selectionModel()->setCurrentIndex(index,
    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  m_view->scrollTo(index);
}

const NodeId& LayersPanel::get_scope() const {
  return m_scope;
}

QString LayersPanel::get_heading() const {
  return m_heading->text();
}

bool LayersPanel::rename(const NodeId& id) {
  if(!m_editor) {
    return false;
  }
  auto index = m_model->find(id);
  if(!index.isValid()) {
    return false;
  }
  m_view->scrollTo(index);
  m_view->setCurrentIndex(index);
  m_view->edit(index);
  return static_cast<bool>(m_model->flags(index) & Qt::ItemIsEditable);
}

void LayersPanel::set_current_handler(CurrentHandler handler) {
  m_current_handler = std::move(handler);
}

void LayersPanel::set_activation_handler(CurrentHandler handler) {
  m_activation_handler = std::move(handler);
}

void LayersPanel::synchronize() {
  auto guard = Guard(m_is_synchronizing);
  auto& selection = m_editor->get_selection();
  auto items = QItemSelection();
  for(auto& id : selection.get_nodes()) {
    auto index = m_model->find(id);
    if(!index.isValid()) {
      continue;
    }
    for(auto parent = index.parent(); parent.isValid();
        parent = parent.parent()) {
      m_view->expand(parent);
    }
    items.select(index, index);
  }
  auto model = m_view->selectionModel();
  model->select(items,
    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  auto primary = m_model->find(selection.get_primary());
  if(primary.isValid()) {
    model->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
  }
  on_current(primary);
  if(primary.isValid()) {
    m_view->scrollTo(primary);
  }
}

void LayersPanel::on_selection() {
  auto model = m_view->selectionModel();
  auto current = model->currentIndex();
  if(!model->isSelected(current)) {
    current = QModelIndex();
  }
  if(m_editor) {
    auto nodes = std::vector<NodeId>();
    for(auto& row : model->selectedRows(LayersModel::NAME)) {
      nodes.push_back(m_model->get_id(row));
    }
    auto guard = Guard(m_is_synchronizing);
    m_editor->get_selection().select(nodes, m_model->get_id(current));
  }
  if(current.isValid()) {
    on_current(current);
  } else {
    auto rows = model->selectedRows(LayersModel::NAME);
    on_current(rows.isEmpty() ? QModelIndex() : rows.front());
  }
}

void LayersPanel::on_current(const QModelIndex& index) {
  auto current = m_model->get_id(index);
  if(current == m_current) {
    update_scope();
    return;
  }
  m_current = current;
  update_scope();
  if(m_current_handler) {
    m_current_handler(m_current);
  }
}

void LayersPanel::update_scope() {
  auto document = m_model->get_document();
  auto scope = NodeId();
  if(document && !m_current.is_nil() && document->find(m_current)) {
    auto artboard = find_artboard(*document, m_current);
    if(!artboard.is_nil() && artboard != m_current &&
        document->is_on_canvas(artboard)) {
      scope = artboard;
    }
  }
  auto root = scope.is_nil() ? QModelIndex() : m_model->find(scope);
  if(!root.isValid()) {
    scope = NodeId();
  }
  if(scope != m_scope || m_view->rootIndex() != root) {
    m_scope = scope;
    if(m_scope.is_nil()) {
      m_view->collapseAll();
    }
    m_view->setRootIndex(root);
  }
  m_view->setRootIsDecorated(!m_scope.is_nil());
  m_view->setItemsExpandable(!m_scope.is_nil());
  if(m_scope.is_nil()) {
    m_heading->setText("ARTBOARDS");
    m_back->setVisible(false);
  } else {
    m_heading->setText(
      get_display_name(*document, document->get(m_scope)).toUpper());
    m_back->setVisible(true);
  }
}

void LayersPanel::show_artboards() {
  if(m_editor) {
    if(!m_scope.is_nil()) {
      m_editor->get_selection().select(m_scope);
    }
    return;
  }
  set_current(m_scope);
}

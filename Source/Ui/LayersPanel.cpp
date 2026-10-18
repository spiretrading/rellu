#include <utility>
#include <vector>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QVBoxLayout>
#include "Rellu/Ui/LayersPanel.hpp"

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
}

LayersPanel::LayersPanel(QWidget* parent)
  : QWidget(parent),
    m_model(new LayersModel(this)),
    m_view(new QTreeView()),
    m_editor(nullptr),
    m_is_synchronizing(false) {
  m_view->setObjectName("layers");
  m_view->setModel(m_model);
  m_view->setUniformRowHeights(true);
  m_view->setHeaderHidden(true);
  m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_view->setAllColumnsShowFocus(true);
  m_view->setIndentation(14);
  m_view->setFrameShape(QFrame::NoFrame);
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
  layout->addWidget(m_view);
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
}

LayersModel& LayersPanel::get_model() {
  return *m_model;
}

QTreeView& LayersPanel::get_view() {
  return *m_view;
}

void LayersPanel::set_document(Document* document) {
  m_subscription.release();
  m_editor = nullptr;
  m_model->set_document(document);
  on_current(QModelIndex());
}

Editor* LayersPanel::get_editor() const {
  return m_editor;
}

void LayersPanel::set_editor(Editor* editor) {
  m_subscription.release();
  m_editor = nullptr;
  if(!editor) {
    m_model->set_document(nullptr);
    on_current(QModelIndex());
    return;
  }
  m_model->set_document(&editor->get_document());
  m_editor = editor;
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
    m_view->scrollTo(primary);
  }
  on_current(primary);
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
    return;
  }
  m_current = current;
  if(m_current_handler) {
    m_current_handler(m_current);
  }
}

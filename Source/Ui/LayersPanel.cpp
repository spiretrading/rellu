#include <utility>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QVBoxLayout>
#include "Rellu/Ui/LayersPanel.hpp"

using namespace Rellu;

namespace {
  constexpr auto INDICATOR_WIDTH = 24;
}

LayersPanel::LayersPanel(QWidget* parent)
  : QWidget(parent),
    m_model(new LayersModel(this)),
    m_view(new QTreeView()) {
  m_view->setObjectName("layers");
  m_view->setModel(m_model);
  m_view->setUniformRowHeights(true);
  m_view->setHeaderHidden(true);
  m_view->setSelectionMode(QAbstractItemView::SingleSelection);
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
      auto rows = m_view->selectionModel()->selectedRows(LayersModel::NAME);
      if(rows.isEmpty()) {
        on_current(QModelIndex());
      } else {
        on_current(rows.front());
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
  m_model->set_document(document);
  on_current(QModelIndex());
}

const NodeId& LayersPanel::get_current() const {
  return m_current;
}

void LayersPanel::set_current(const NodeId& id) {
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

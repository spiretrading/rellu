#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <utility>
#include <vector>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Ui/ContextMenus.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto INDICATOR_WIDTH = 24;
  const auto FILTERS = std::to_array<std::pair<LayerFilter, const char*>>({
    std::pair(LayerFilter::ALL, "All Items"),
    std::pair(LayerFilter::IMAGES, "Images"),
    std::pair(LayerFilter::SHAPES, "Shapes"),
    std::pair(LayerFilter::TEXT, "Text"),
    std::pair(LayerFilter::VIDEO, "Video")});

  bool is_image(const Node& node) {
    auto shape = node.get_shape();
    return shape && std::holds_alternative<ImageFill>(shape->m_style.m_fill);
  }

  bool is_plain_shape(const Node& node) {
    auto shape = node.get_shape();
    if(!shape || std::holds_alternative<CompoundShape>(shape->m_geometry)) {
      return false;
    }
    return !std::holds_alternative<RectangleShape>(shape->m_geometry) ||
      !node.m_header.m_aspect_lock || !is_image(node);
  }

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
        auto is_hovered =
          static_cast<bool>(option->state & QStyle::State_MouseOver);
        if(index.column() == LayersModel::VISIBILITY &&
            index.data(LayersModel::VISIBLE_ROLE).toBool() && !is_hovered) {
          option->icon = QIcon();
          option->features &= ~QStyleOptionViewItem::HasDecoration;
        } else if(index.column() == LayersModel::LOCK &&
            !index.data(LayersModel::LOCKED_ROLE).toBool() && is_hovered) {
          static auto unlocked = make_icon(Glyph::LOCK, Colors::DISABLED_TEXT);
          option->icon = unlocked;
          option->features |= QStyleOptionViewItem::HasDecoration;
        }
      }
  };
}

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent),
      m_model(new LayersModel(this)),
      m_view(new QTreeView()),
      m_search(new QLineEdit()),
      m_filter_button(new QToolButton()),
      m_back(new QToolButton()),
      m_heading(new QLabel()),
      m_editor(nullptr),
      m_actions(nullptr),
      m_filter(LayerFilter::ALL),
      m_is_synchronizing(false),
      m_is_filtered(false) {
  setObjectName("layers_panel");
  auto search = new QWidget();
  auto search_layout = new QHBoxLayout(search);
  search_layout->setContentsMargins(16, 12, 16, 0);
  search_layout->setSpacing(4);
  m_search->setObjectName("search_field");
  m_search->setPlaceholderText(translate("layers", "All Items"));
  m_search->setAccessibleName(translate("layers", "Search Layers"));
  m_search->setClearButtonEnabled(true);
  m_search->addAction(get_icon(Glyph::SEARCH), QLineEdit::LeadingPosition);
  search_layout->addWidget(m_search, 1);
  connect(m_search, &QLineEdit::textChanged, this, [this] (const auto& text) {
    set_search(text);
  });
  m_filter_button->setObjectName("panel_button");
  m_filter_button->setAutoRaise(true);
  m_filter_button->setFocusPolicy(Qt::NoFocus);
  m_filter_button->setIcon(get_icon(Glyph::CHEVRON_DOWN));
  m_filter_button->setIconSize(QSize(16, 16));
  m_filter_button->setFixedSize(24, 24);
  m_filter_button->setToolTip(translate("layers", "Filter"));
  m_filter_button->setAccessibleName(translate("layers", "Filter"));
  m_filter_button->setPopupMode(QToolButton::InstantPopup);
  auto filters = new QMenu(m_filter_button);
  auto group = new QActionGroup(filters);
  for(auto& [filter, label] : FILTERS) {
    auto action = filters->addAction(translate("layers", label));
    action->setCheckable(true);
    action->setChecked(filter == LayerFilter::ALL);
    group->addAction(action);
    connect(action, &QAction::triggered, this, [this, filter] {
      set_filter(filter);
    });
  }
  m_filter_button->setMenu(filters);
  search_layout->addWidget(m_filter_button);
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
  m_back->setToolTip(translate("layers", "Back"));
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
  m_view->setDragDropMode(QAbstractItemView::InternalMove);
  m_view->setDefaultDropAction(Qt::MoveAction);
  m_view->setDropIndicatorShown(true);
  m_view->setContextMenuPolicy(Qt::CustomContextMenu);
  m_view->viewport()->installEventFilter(this);
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
  connect(m_view, &QTreeView::customContextMenuRequested, this,
    [this] (const QPoint& position) {
      auto index = m_view->indexAt(position);
      if(index.isValid() && m_editor &&
          !m_editor->get_selection().contains(m_model->get_id(index))) {
        m_editor->get_selection().select(m_model->get_id(index));
      }
      auto menu = make_context_menu(m_view);
      menu->setAttribute(Qt::WA_DeleteOnClose);
      if(menu->isEmpty()) {
        menu->deleteLater();
        return;
      }
      menu->popup(m_view->viewport()->mapToGlobal(position));
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

void LayersPanel::set_actions(ActionRegistry* actions) {
  m_actions = actions;
}

QString LayersPanel::get_search() const {
  return m_search->text();
}

void LayersPanel::set_search(const QString& text) {
  if(m_search->text() != text) {
    m_search->setText(text);
    return;
  }
  update_scope();
}

LayerFilter LayersPanel::get_filter() const {
  return m_filter;
}

void LayersPanel::set_filter(LayerFilter filter) {
  m_filter = filter;
  for(auto& entry : FILTERS) {
    if(entry.first == filter) {
      m_search->setPlaceholderText(translate("layers", entry.second));
    }
  }
  if(auto menu = m_filter_button->menu()) {
    auto actions = menu->actions();
    for(auto i = 0; i != actions.size(); ++i) {
      actions[i]->setChecked(FILTERS[i].first == filter);
    }
  }
  update_scope();
}

bool LayersPanel::is_filtered_out(const NodeId& id) {
  auto index = m_model->find(id);
  if(!index.isValid()) {
    return true;
  }
  for(auto current = index; current.isValid(); current = current.parent()) {
    if(m_view->isRowHidden(current.row(), current.parent())) {
      return true;
    }
  }
  return false;
}

bool LayersPanel::toggle_visibility(const NodeId& id) {
  auto document = m_model->get_document();
  if(!m_editor || !document || !document->find(id) ||
      is_inside_compound(*document, id)) {
    return false;
  }
  auto header = find_rendered_header(*document, id);
  if(!header) {
    return false;
  }
  auto is_visible = header->m_is_visible;
  try {
    return static_cast<bool>(m_editor->execute(
      std::make_unique<SetVisibilityCommand>(std::vector{id}, !is_visible)));
  } catch(const std::exception&) {
    return false;
  }
}

bool LayersPanel::toggle_lock(const NodeId& id) {
  auto document = m_model->get_document();
  if(!m_editor || !document || !document->find(id)) {
    return false;
  }
  auto header = find_rendered_header(*document, id);
  if(!header) {
    return false;
  }
  auto is_locked = header->m_is_locked;
  try {
    return static_cast<bool>(m_editor->execute(
      std::make_unique<SetLockCommand>(std::vector{id}, !is_locked)));
  } catch(const std::exception&) {
    return false;
  }
}

bool LayersPanel::move(const std::vector<NodeId>& nodes, const NodeId& parent,
    std::size_t index) {
  if(!m_editor || !m_model->is_movable(nodes, parent)) {
    return false;
  }
  auto ordered = nodes;
  sort_by_z_order(m_editor->get_document(), ordered);
  try {
    return static_cast<bool>(m_editor->execute(
      std::make_unique<ReparentCommand>(std::move(ordered), parent, index)));
  } catch(const std::exception&) {
    return false;
  }
}

QMenu* LayersPanel::make_context_menu(QWidget* parent) {
  if(!m_actions) {
    return new QMenu(parent);
  }
  return Rellu::make_context_menu(*m_actions, ContextMenuKind::LAYERS, parent);
}

bool LayersPanel::eventFilter(QObject* watched, QEvent* event) {
  if(watched != m_view->viewport() || !m_editor ||
      (event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonDblClick)) {
    return QWidget::eventFilter(watched, event);
  }
  auto mouse = static_cast<QMouseEvent*>(event);
  if(mouse->button() != Qt::LeftButton) {
    return QWidget::eventFilter(watched, event);
  }
  auto index = m_view->indexAt(mouse->position().toPoint());
  if(!index.isValid() || (index.column() != LayersModel::VISIBILITY &&
      index.column() != LayersModel::LOCK)) {
    return QWidget::eventFilter(watched, event);
  }
  if(event->type() == QEvent::MouseButtonPress) {
    auto id = m_model->get_id(index);
    if(index.column() == LayersModel::VISIBILITY) {
      toggle_visibility(id);
    } else {
      toggle_lock(id);
    }
  }
  return true;
}

bool LayersPanel::is_searching() const {
  return !m_search->text().trimmed().isEmpty() ||
    m_filter != LayerFilter::ALL;
}

bool LayersPanel::is_match(const QModelIndex& index) const {
  auto document = m_model->get_document();
  auto node = document ? document->find(m_model->get_id(index)) : nullptr;
  if(!node) {
    return false;
  }
  auto text = m_search->text().trimmed();
  if(!text.isEmpty() &&
      !get_display_name(*document, *node).contains(text, Qt::CaseInsensitive)) {
    return false;
  }
  auto displayed = get_displayed_node(*document, *node);
  if(!displayed) {
    return m_filter == LayerFilter::ALL;
  }
  switch(m_filter) {
    case LayerFilter::IMAGES:
      return is_image(*displayed);
    case LayerFilter::SHAPES:
      return is_plain_shape(*displayed);
    case LayerFilter::TEXT:
      return static_cast<bool>(displayed->get_text());
    case LayerFilter::VIDEO:
      return false;
    default:
      return true;
  }
}

bool LayersPanel::apply_filter(const QModelIndex& index) {
  auto is_visible = false;
  auto is_searching = this->is_searching();
  for(auto row = 0; row != m_model->rowCount(index); ++row) {
    auto child = m_model->index(row, LayersModel::NAME, index);
    auto has_match = apply_filter(child);
    auto is_shown = !is_searching || is_match(child) || has_match;
    m_view->setRowHidden(row, index, !is_shown);
    if(is_searching && has_match) {
      m_view->expand(child);
    }
    is_visible |= is_shown;
  }
  return is_visible;
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
  m_model->set_move_handler(nullptr);
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
    m_model->set_move_handler(nullptr);
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
  m_model->set_move_handler([this] (const auto& nodes, const auto& parent,
      auto index) {
    return move(nodes, parent, index);
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
  auto is_searching = this->is_searching();
  if(is_searching) {
    m_view->setRootIndex(QModelIndex());
    m_scope = NodeId();
    m_view->setRootIsDecorated(true);
    m_view->setItemsExpandable(true);
    m_heading->setText(translate("layers", "ARTBOARDS"));
    m_back->setVisible(false);
    apply_filter(QModelIndex());
    m_is_filtered = true;
    return;
  } else if(m_is_filtered) {
    m_is_filtered = false;
    apply_filter(QModelIndex());
    m_view->collapseAll();
    m_scope = NodeId();
    m_view->setRootIndex(QModelIndex());
  }
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
    m_heading->setText(translate("layers", "ARTBOARDS"));
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

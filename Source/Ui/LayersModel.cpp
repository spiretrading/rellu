#include <algorithm>
#include <span>
#include <unordered_set>
#include <utility>
#include <QColor>
#include <QFont>
#include "Rellu/Ui/LayersModel.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_RUNS = 32;

  struct Span {
    std::size_t m_first;
    std::size_t m_old_last;
    std::size_t m_new_last;
  };

  Span get_difference(
      const std::vector<NodeId>& old, const std::vector<NodeId>& current) {
    auto prefix = std::size_t(0);
    auto limit = std::min(old.size(), current.size());
    while(prefix != limit && old[prefix] == current[prefix]) {
      ++prefix;
    }
    auto suffix = std::size_t(0);
    while(suffix != limit - prefix &&
        old[old.size() - suffix - 1] == current[current.size() - suffix - 1]) {
      ++suffix;
    }
    return Span(prefix, old.size() - suffix, current.size() - suffix);
  }

  std::unordered_set<NodeId> make_set(
      const std::vector<NodeId>& ids, std::size_t first, std::size_t last) {
    return std::unordered_set<NodeId>(
      ids.begin() + first, ids.begin() + last);
  }
}

LayersModel::LayersModel(QObject* parent)
  : QAbstractItemModel(parent),
    m_document(nullptr),
    m_root(std::make_unique<Item>()) {}

LayersModel::~LayersModel() = default;

Document* LayersModel::get_document() const {
  return m_document;
}

void LayersModel::set_document(Document* document) {
  beginResetModel();
  m_subscription.release();
  m_items.clear();
  m_root = std::make_unique<Item>();
  m_document = document;
  if(m_document) {
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      on_change(changes);
    });
  }
  endResetModel();
}

NodeId LayersModel::get_id(const QModelIndex& index) const {
  if(!index.isValid()) {
    return NodeId();
  }
  return get_item(index)->m_id;
}

QModelIndex LayersModel::find(const NodeId& id, int column) {
  if(!m_document || id.is_nil()) {
    return QModelIndex();
  }
  auto path = std::vector<NodeId>();
  auto current = id;
  while(!current.is_nil()) {
    auto node = m_document->find(current);
    if(!node) {
      return QModelIndex();
    }
    path.push_back(current);
    current = node->m_parent;
  }
  if(!m_document->is_on_canvas(path.back())) {
    return QModelIndex();
  }
  auto item = m_root.get();
  for(auto i = path.rbegin(); i != path.rend(); ++i) {
    populate(*item);
    auto row = std::find(
      item->m_child_ids.begin(), item->m_child_ids.end(), *i);
    if(row == item->m_child_ids.end()) {
      return QModelIndex();
    }
    item = &get_child(
      *item, static_cast<int>(row - item->m_child_ids.begin()));
  }
  return make_index(*item, column);
}

QModelIndex LayersModel::index(
    int row, int column, const QModelIndex& parent) const {
  if(row < 0 || column < 0 || column >= COLUMN_COUNT || parent.column() > 0) {
    return QModelIndex();
  }
  auto& item = *get_item(parent);
  populate(item);
  if(row >= static_cast<int>(item.m_child_ids.size())) {
    return QModelIndex();
  }
  return createIndex(row, column, &get_child(item, row));
}

QModelIndex LayersModel::parent(const QModelIndex& index) const {
  if(!index.isValid()) {
    return QModelIndex();
  }
  return make_index(*get_item(index)->m_parent, NAME);
}

int LayersModel::rowCount(const QModelIndex& parent) const {
  if(parent.column() > 0) {
    return 0;
  }
  auto& item = *get_item(parent);
  populate(item);
  return static_cast<int>(item.m_child_ids.size());
}

int LayersModel::columnCount(const QModelIndex& parent) const {
  return COLUMN_COUNT;
}

bool LayersModel::hasChildren(const QModelIndex& parent) const {
  if(parent.column() > 0) {
    return false;
  }
  auto& item = *get_item(parent);
  if(item.m_is_populated) {
    return !item.m_child_ids.empty();
  } else if(!m_document) {
    return false;
  } else if(&item == m_root.get()) {
    return !m_document->get_canvas().empty();
  } else if(auto node = m_document->find(item.m_id)) {
    return !node->m_children.empty();
  }
  return false;
}

QVariant LayersModel::data(const QModelIndex& index, int role) const {
  if(!index.isValid() || !m_document) {
    return QVariant();
  }
  auto node = m_document->find(get_item(index)->m_id);
  if(!node) {
    return QVariant();
  }
  auto displayed = get_displayed_node(*m_document, *node);
  auto header = displayed ? &displayed->m_header : &node->m_header;
  auto is_reference = node->get_kind() == NodeKind::REFERENCE;
  if(role == KIND_ROLE) {
    if(displayed) {
      return static_cast<int>(displayed->get_kind());
    }
    return static_cast<int>(NodeKind::REFERENCE);
  } else if(role == REFERENCE_ROLE) {
    return is_reference;
  } else if(role == VISIBLE_ROLE) {
    return header->m_is_visible;
  } else if(role == LOCKED_ROLE) {
    return header->m_is_locked;
  } else if(role == ID_ROLE) {
    return QString::fromStdString(to_string(node->get_id()));
  }
  if(index.column() == NAME) {
    if(role == Qt::DisplayRole) {
      return get_display_name(*m_document, *node);
    } else if(role == Qt::DecorationRole) {
      auto kind = displayed ? displayed->get_kind() : NodeKind::REFERENCE;
      return get_kind_icon(kind, is_reference);
    } else if(role == Qt::ToolTipRole) {
      auto type = displayed ? get_type_name(*displayed) :
        QString("Unresolved reference");
      if(is_reference && displayed) {
        return QString("%1 (reference)").arg(type);
      }
      return type;
    } else if(role == Qt::FontRole && is_reference) {
      auto font = QFont();
      font.setItalic(true);
      return font;
    } else if(role == Qt::ForegroundRole && !header->m_is_visible) {
      return QColor(0x9A, 0x9F, 0xA6);
    }
  } else if(index.column() == VISIBILITY) {
    if(role == Qt::DecorationRole) {
      return get_visibility_icon(header->m_is_visible);
    } else if(role == Qt::ToolTipRole) {
      return QString(header->m_is_visible ? "Visible" : "Hidden");
    }
  } else if(index.column() == LOCK) {
    if(role == Qt::DecorationRole && header->m_is_locked) {
      return get_lock_icon();
    } else if(role == Qt::ToolTipRole && header->m_is_locked) {
      return QString("Locked");
    }
  }
  return QVariant();
}

QVariant LayersModel::headerData(
    int section, Qt::Orientation orientation, int role) const {
  if(orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QVariant();
  }
  if(section == NAME) {
    return QString("Name");
  } else if(section == VISIBILITY) {
    return QString("Visible");
  } else if(section == LOCK) {
    return QString("Locked");
  }
  return QVariant();
}

Qt::ItemFlags LayersModel::flags(const QModelIndex& index) const {
  if(!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

LayersModel::Item* LayersModel::get_item(const QModelIndex& index) const {
  if(!index.isValid()) {
    return m_root.get();
  }
  return static_cast<Item*>(index.internalPointer());
}

LayersModel::Item* LayersModel::find_item(const NodeId& id) const {
  if(id.is_nil()) {
    return m_root.get();
  }
  auto i = m_items.find(id);
  if(i == m_items.end()) {
    return nullptr;
  }
  return i->second.get();
}

QModelIndex LayersModel::make_index(Item& item, int column) const {
  if(&item == m_root.get()) {
    return QModelIndex();
  }
  return createIndex(item.m_row, column, &item);
}

void LayersModel::populate(Item& item) const {
  if(item.m_is_populated) {
    return;
  }
  item.m_is_populated = true;
  item.m_child_ids = get_rows(item);
  item.m_children.assign(item.m_child_ids.size(), nullptr);
}

std::vector<NodeId> LayersModel::get_rows(const Item& item) const {
  if(!m_document) {
    return std::vector<NodeId>();
  }
  auto children = [&] () -> const std::vector<NodeId>* {
    if(&item == m_root.get()) {
      return &m_document->get_canvas();
    } else if(auto node = m_document->find(item.m_id)) {
      return &node->m_children;
    }
    return nullptr;
  }();
  if(!children) {
    return std::vector<NodeId>();
  }
  return std::vector<NodeId>(children->rbegin(), children->rend());
}

LayersModel::Item& LayersModel::get_child(Item& item, int row) const {
  populate(item);
  auto& child = item.m_children[row];
  if(!child) {
    auto& slot = m_items[item.m_child_ids[row]];
    slot = std::make_unique<Item>(item.m_child_ids[row], &item, row, false,
      std::vector<NodeId>(), std::vector<Item*>());
    child = slot.get();
  }
  return *child;
}

void LayersModel::destroy(Item& item) {
  for(auto child : item.m_children) {
    if(child) {
      destroy(*child);
    }
  }
  m_items.erase(item.m_id);
}

void LayersModel::renumber(Item& item, int first) {
  for(auto i = first; i < static_cast<int>(item.m_children.size()); ++i) {
    if(auto child = item.m_children[i]) {
      child->m_row = i;
    }
  }
}

void LayersModel::remove_children(Item& item) {
  auto current = get_rows(item);
  auto& old = item.m_child_ids;
  auto span = get_difference(old, current);
  if(span.m_first == span.m_old_last) {
    return;
  }
  auto kept = make_set(current, span.m_first, span.m_new_last);
  auto runs = std::vector<std::pair<int, int>>();
  auto i = static_cast<int>(span.m_old_last) - 1;
  while(i >= static_cast<int>(span.m_first)) {
    if(kept.contains(old[i])) {
      --i;
      continue;
    }
    auto last = i;
    while(i >= static_cast<int>(span.m_first) && !kept.contains(old[i])) {
      --i;
    }
    runs.emplace_back(i + 1, last);
  }
  if(runs.size() > MAXIMUM_RUNS) {
    runs.assign(1, std::pair(0, static_cast<int>(old.size()) - 1));
  }
  auto parent = make_index(item, NAME);
  for(auto& run : runs) {
    beginRemoveRows(parent, run.first, run.second);
    for(auto j = run.first; j <= run.second; ++j) {
      if(auto child = item.m_children[j]) {
        destroy(*child);
      }
    }
    old.erase(old.begin() + run.first, old.begin() + run.second + 1);
    item.m_children.erase(item.m_children.begin() + run.first,
      item.m_children.begin() + run.second + 1);
    renumber(item, run.first);
    endRemoveRows();
  }
}

void LayersModel::reorder_children(Item& item) {
  auto current = get_rows(item);
  auto& old = item.m_child_ids;
  auto span = get_difference(old, current);
  if(span.m_first == span.m_old_last) {
    return;
  }
  auto present = make_set(old, span.m_first, span.m_old_last);
  auto order = std::vector<NodeId>();
  for(auto i = span.m_first; i != span.m_new_last; ++i) {
    if(present.contains(current[i])) {
      order.push_back(current[i]);
    }
  }
  if(std::equal(order.begin(), order.end(), old.begin() + span.m_first,
      old.begin() + span.m_old_last)) {
    return;
  }
  auto parents = QList<QPersistentModelIndex>();
  if(&item != m_root.get()) {
    parents.push_back(make_index(item, NAME));
  }
  Q_EMIT layoutAboutToBeChanged(
    parents, QAbstractItemModel::VerticalSortHint);
  auto children = std::unordered_map<NodeId, Item*>();
  for(auto i = span.m_first; i != span.m_old_last; ++i) {
    if(auto child = item.m_children[i]) {
      children.emplace(old[i], child);
    }
  }
  for(auto i = std::size_t(0); i != order.size(); ++i) {
    old[span.m_first + i] = order[i];
    auto child = children.find(order[i]);
    if(child == children.end()) {
      item.m_children[span.m_first + i] = nullptr;
    } else {
      item.m_children[span.m_first + i] = child->second;
    }
  }
  renumber(item, static_cast<int>(span.m_first));
  auto from = persistentIndexList();
  auto to = QModelIndexList();
  to.reserve(from.size());
  for(auto& index : from) {
    auto child = get_item(index);
    if(child->m_parent == &item) {
      to.push_back(createIndex(child->m_row, index.column(), child));
    } else {
      to.push_back(index);
    }
  }
  changePersistentIndexList(from, to);
  Q_EMIT layoutChanged(parents, QAbstractItemModel::VerticalSortHint);
}

void LayersModel::insert_children(Item& item) {
  auto current = get_rows(item);
  auto& old = item.m_child_ids;
  auto span = get_difference(old, current);
  if(span.m_first == span.m_new_last) {
    return;
  }
  auto present = make_set(old, span.m_first, span.m_old_last);
  auto parent = make_index(item, NAME);
  auto i = span.m_first;
  while(i != span.m_new_last) {
    if(present.contains(current[i])) {
      ++i;
      continue;
    }
    auto first = i;
    while(i != span.m_new_last && !present.contains(current[i])) {
      ++i;
    }
    beginInsertRows(parent, static_cast<int>(first), static_cast<int>(i) - 1);
    old.insert(old.begin() + first, current.begin() + first,
      current.begin() + i);
    item.m_children.insert(item.m_children.begin() + first, i - first, nullptr);
    renumber(item, static_cast<int>(first));
    endInsertRows();
  }
}

void LayersModel::on_change(const ChangeSet& changes) {
  auto parents = std::vector<NodeId>();
  auto visited = std::unordered_set<NodeId>();
  auto add = [&] (const NodeId& id) {
    if(visited.insert(id).second) {
      parents.push_back(id);
    }
  };
  if(!changes.m_inserted.empty() || !changes.m_removed.empty() ||
      !changes.m_moved.empty()) {
    add(NodeId());
  }
  for(auto& change : changes.m_modified) {
    if(is_set(change.m_mask, ChangeMask::CHILDREN)) {
      add(change.m_id);
    }
  }
  for(auto ids : {std::span(changes.m_removed), std::span(changes.m_moved)}) {
    for(auto& id : ids) {
      if(auto item = find_item(id)) {
        add(item->m_parent->m_id);
      }
    }
  }
  for(auto ids : {std::span(changes.m_inserted), std::span(changes.m_moved)}) {
    for(auto& id : ids) {
      if(auto node = m_document->find(id)) {
        add(node->m_parent);
      }
    }
  }
  auto apply = [&] (auto operation) {
    for(auto& id : parents) {
      auto item = find_item(id);
      if(item && item->m_is_populated &&
          (id.is_nil() || m_document->find(id))) {
        (this->*operation)(*item);
      }
    }
  };
  apply(&LayersModel::remove_children);
  apply(&LayersModel::reorder_children);
  apply(&LayersModel::insert_children);
  for(auto& change : changes.m_modified) {
    if(auto item = find_item(change.m_id); item && item != m_root.get()) {
      Q_EMIT dataChanged(make_index(*item, NAME),
        make_index(*item, COLUMN_COUNT - 1));
    }
  }
}

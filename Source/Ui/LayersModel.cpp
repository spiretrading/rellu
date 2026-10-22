#include <algorithm>
#include <cstddef>
#include <span>
#include <unordered_set>
#include <utility>
#include <QColor>
#include <QFont>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Ui/LayersModel.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"
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

void LayersModel::set_rename_handler(RenameHandler handler) {
  m_rename_handler = std::move(handler);
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
  } else if(id == PASTEBOARD) {
    populate(*m_root);
    auto& rows = m_root->m_child_ids;
    auto row = std::find(rows.begin(), rows.end(), PASTEBOARD);
    if(row == rows.end()) {
      return QModelIndex();
    }
    return make_index(
      get_child(*m_root, static_cast<int>(row - rows.begin())), column);
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
  populate(*m_root);
  if(!m_document->get(path.back()).get_artboard() &&
      std::ranges::contains(m_root->m_child_ids, PASTEBOARD)) {
    path.push_back(PASTEBOARD);
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
  } else if(&item == m_root.get() || item.m_id == PASTEBOARD) {
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
  if(get_item(index)->m_id == PASTEBOARD) {
    if(index.column() != NAME) {
      return QVariant();
    } else if(role == Qt::DisplayRole || role == Qt::EditRole ||
        role == Qt::ToolTipRole) {
      return translate("layers", "Pasteboard");
    } else if(role == Qt::DecorationRole) {
      return get_pasteboard_icon();
    } else if(role == VISIBLE_ROLE) {
      return true;
    } else if(role == LOCKED_ROLE || role == MARKED_ROLE) {
      return false;
    }
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
  } else if(role == MARKED_ROLE) {
    return header->m_is_marked_for_export;
  } else if(role == ID_ROLE) {
    return QString::fromStdString(to_string(node->get_id()));
  } else if(role == EXPANDED_ICON_ROLE) {
    if(index.column() == NAME && displayed && !is_reference) {
      if(auto group = displayed->get_group();
          group && !group->m_component_instance) {
        return get_open_group_icon();
      }
    }
    return QVariant();
  }
  if(index.column() == NAME) {
    if(role == Qt::DisplayRole || role == Qt::EditRole) {
      return get_display_name(*m_document, *node);
    } else if(role == Qt::DecorationRole) {
      if(!displayed) {
        return get_kind_icon(NodeKind::REFERENCE, is_reference);
      }
      return get_layer_icon(*displayed, is_reference);
    } else if(role == Qt::FontRole && is_reference) {
      auto font = QFont();
      font.setItalic(true);
      return font;
    } else if(role == Qt::ForegroundRole && !header->m_is_visible) {
      return QColor(0x9A, 0x9F, 0xA6);
    }
  } else if(index.column() == VISIBILITY) {
    if(is_inside_compound(*m_document, node->get_id())) {
      return QVariant();
    } else if(role == Qt::DecorationRole) {
      return get_visibility_icon(header->m_is_visible);
    } else if(role == Qt::ToolTipRole) {
      return translate("layers", header->m_is_visible ? "Hide (%1)" :
        "Show (%1)").arg(to_xd_shortcut_text(QKeySequence("Ctrl+,")));
    }
  } else if(index.column() == LOCK) {
    if(role == Qt::DecorationRole && header->m_is_locked) {
      return get_lock_icon();
    } else if(role == Qt::ToolTipRole) {
      return translate("layers", header->m_is_locked ? "Unlock (%1)" :
        "Lock (%1)").arg(to_xd_shortcut_text(QKeySequence("Ctrl+L")));
    }
  } else if(index.column() == EXPORT) {
    if(role == Qt::DecorationRole && header->m_is_marked_for_export) {
      return get_export_mark_icon(true);
    } else if(role == Qt::ToolTipRole) {
      return translate("layers", header->m_is_marked_for_export ?
        "Unmark for Export (%1)" : "Mark for Export (%1)").arg(
          to_xd_shortcut_text(QKeySequence("Shift+E")));
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
    return translate("layers", "Name");
  } else if(section == VISIBILITY) {
    return translate("layers", "Visible");
  } else if(section == LOCK) {
    return translate("layers", "Locked");
  } else if(section == EXPORT) {
    return translate("layers", "Mark for Export");
  }
  return QVariant();
}

Qt::ItemFlags LayersModel::flags(const QModelIndex& index) const {
  if(!index.isValid()) {
    if(m_move_handler) {
      return Qt::ItemIsDropEnabled;
    }
    return Qt::NoItemFlags;
  }
  auto flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if(get_id(index) == PASTEBOARD) {
    if(m_move_handler) {
      flags |= Qt::ItemIsDropEnabled;
    }
    return flags;
  }
  if(m_rename_handler && index.column() == NAME) {
    flags |= Qt::ItemIsEditable;
  }
  if(m_move_handler) {
    flags |= Qt::ItemIsDragEnabled;
    auto id = get_id(index);
    if(auto node = m_document ? m_document->find(id) : nullptr) {
      auto group = node->get_group();
      if(node->get_artboard() || (group && !group->m_component_instance &&
          !group->m_repeat_grid)) {
        flags |= Qt::ItemIsDropEnabled;
      }
    }
  }
  return flags;
}

Qt::DropActions LayersModel::supportedDropActions() const {
  return Qt::MoveAction;
}

QStringList LayersModel::mimeTypes() const {
  return QStringList({MIME_TYPE});
}

QMimeData* LayersModel::mimeData(const QModelIndexList& indexes) const {
  auto ids = QStringList();
  for(auto& index : indexes) {
    if(index.column() != NAME) {
      continue;
    }
    auto id = QString::fromStdString(to_string(get_id(index)));
    if(!ids.contains(id)) {
      ids.push_back(id);
    }
  }
  auto data = new QMimeData();
  data->setData(MIME_TYPE, ids.join('\n').toUtf8());
  return data;
}

bool LayersModel::canDropMimeData(const QMimeData* data,
    Qt::DropAction action, int row, int column,
    const QModelIndex& parent) const {
  if(!m_move_handler || !m_document || !data ||
      !data->hasFormat(MIME_TYPE)) {
    return false;
  }
  auto nodes = std::vector<NodeId>();
  for(auto& text : QString::fromUtf8(data->data(MIME_TYPE)).split('\n')) {
    if(auto id = parse_node_id(text.toStdString());
        id && m_document->find(*id)) {
      nodes.push_back(*id);
    }
  }
  return !nodes.empty() && is_movable(nodes, get_id(parent));
}

bool LayersModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
    int row, int column, const QModelIndex& parent) {
  if(!canDropMimeData(data, action, row, column, parent)) {
    return false;
  }
  auto nodes = std::vector<NodeId>();
  for(auto& text : QString::fromUtf8(data->data(MIME_TYPE)).split('\n')) {
    if(auto id = parse_node_id(text.toStdString())) {
      if(m_document->find(*id) &&
          std::find(nodes.begin(), nodes.end(), *id) == nodes.end()) {
        nodes.push_back(*id);
      }
    }
  }
  auto parent_id = get_id(parent);
  auto is_moved = [&] (const NodeId& id) {
    return id == PASTEBOARD ||
      std::find(nodes.begin(), nodes.end(), id) != nodes.end();
  };
  auto rows = get_rows(*get_item(parent));
  auto limit = row < 0 ? std::size_t(0) :
    std::min(static_cast<std::size_t>(row), rows.size());
  auto below = std::find_if(rows.begin() + static_cast<std::ptrdiff_t>(limit),
    rows.end(), [&] (const auto& id) { return !is_moved(id); });
  if(parent_id == PASTEBOARD) {
    parent_id = NodeId();
  }
  auto children = m_document->get_children(parent_id);
  std::erase_if(children, is_moved);
  if(below != rows.end()) {
    auto position = std::find(children.begin(), children.end(), *below);
    return m_move_handler(nodes, parent_id,
      static_cast<std::size_t>(position - children.begin()) + 1);
  }
  auto lowest = std::find_if(rows.rbegin(), rows.rend(),
    [&] (const auto& id) { return !is_moved(id); });
  if(lowest == rows.rend()) {
    return m_move_handler(nodes, parent_id, children.size());
  }
  auto position = std::find(children.begin(), children.end(), *lowest);
  return m_move_handler(nodes, parent_id,
    static_cast<std::size_t>(position - children.begin()));
}

void LayersModel::set_move_handler(MoveHandler handler) {
  m_move_handler = std::move(handler);
}

bool LayersModel::is_movable(
    const std::vector<NodeId>& nodes, const NodeId& parent) const {
  if(!m_document || nodes.empty()) {
    return false;
  } else if(parent == PASTEBOARD) {
    return std::none_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
      auto node = m_document->find(id);
      return !node || node->get_artboard();
    }) && is_movable(nodes, NodeId());
  }
  auto container = parent.is_nil() ? nullptr : m_document->find(parent);
  if(!parent.is_nil()) {
    if(!container) {
      return false;
    }
    auto group = container->get_group();
    if(!container->get_artboard() &&
        (!group || group->m_component_instance || group->m_repeat_grid)) {
      return false;
    }
  }
  for(auto& id : nodes) {
    auto node = m_document->find(id);
    if(!node || (node->m_parent.is_nil() && !m_document->is_on_canvas(id))) {
      return false;
    } else if(container && node->get_artboard()) {
      return false;
    }
    if(auto owner = m_document->find(node->m_parent)) {
      auto group = owner->get_group();
      if(owner->get_reference() || owner->get_shape() ||
          (group && (group->m_component_instance || group->m_repeat_grid))) {
        return false;
      }
    }
    for(auto current = parent; !current.is_nil();) {
      if(current == id) {
        return false;
      }
      auto ancestor = m_document->find(current);
      if(!ancestor) {
        break;
      }
      current = ancestor->m_parent;
    }
  }
  return true;
}

bool LayersModel::setData(
    const QModelIndex& index, const QVariant& value, int role) {
  if(!m_rename_handler || role != Qt::EditRole || !index.isValid() ||
      index.column() != NAME || get_id(index) == PASTEBOARD) {
    return false;
  }
  auto name = value.toString();
  if(name.trimmed().isEmpty() || name == data(index, Qt::EditRole)) {
    return false;
  }
  m_rename_handler(get_id(index), name);
  return true;
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
  if(&item == m_root.get() || item.m_id == PASTEBOARD) {
    auto& canvas = m_document->get_canvas();
    auto rows = std::vector<NodeId>(canvas.rbegin(), canvas.rend());
    if(!has_pasteboard()) {
      if(item.m_id == PASTEBOARD) {
        rows.clear();
      }
      return rows;
    }
    auto is_artboard = item.m_id != PASTEBOARD;
    std::erase_if(rows, [&] (const auto& id) {
      return static_cast<bool>(m_document->get(id).get_artboard()) !=
        is_artboard;
    });
    if(is_artboard) {
      rows.push_back(PASTEBOARD);
    }
    return rows;
  } else if(auto node = m_document->find(item.m_id)) {
    return std::vector<NodeId>(
      node->m_children.rbegin(), node->m_children.rend());
  }
  return std::vector<NodeId>();
}

bool LayersModel::has_pasteboard() const {
  auto& canvas = m_document->get_canvas();
  auto is_artboard = [&] (const auto& id) {
    return static_cast<bool>(m_document->get(id).get_artboard());
  };
  return std::any_of(canvas.begin(), canvas.end(), is_artboard) &&
    !std::all_of(canvas.begin(), canvas.end(), is_artboard);
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
    if(id.is_nil() && visited.insert(PASTEBOARD).second) {
      parents.push_back(PASTEBOARD);
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
    if(auto node = m_document->find(change.m_id);
        node && node->m_parent.is_nil()) {
      add(NodeId());
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
          (id.is_nil() || id == PASTEBOARD || m_document->find(id))) {
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
  if(!changes.m_is_asset_group_changed && changes.m_components.empty()) {
    return;
  }
  auto rename = [&] (const auto& self, Item& item) -> void {
    for(auto child : item.m_children) {
      if(child) {
        Q_EMIT dataChanged(make_index(*child, NAME), make_index(*child, NAME));
        self(self, *child);
      }
    }
  };
  rename(rename, *m_root);
}

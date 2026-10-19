#include <algorithm>
#include "Rellu/Editor/Selection.hpp"

using namespace Rellu;

Selection::Selection(Document& document)
    : m_document(&document) {
  m_subscription = document.subscribe([this] (const ChangeSet& changes) {
    on_change(changes);
  });
}

const std::vector<NodeId>& Selection::get_nodes() const {
  return m_nodes;
}

const NodeId& Selection::get_primary() const {
  return m_primary;
}

std::size_t Selection::get_size() const {
  return m_nodes.size();
}

bool Selection::is_empty() const {
  return m_nodes.empty();
}

bool Selection::contains(const NodeId& id) const {
  return m_members.contains(id);
}

void Selection::select(const NodeId& id) {
  if(id.is_nil()) {
    clear();
  } else {
    assign(std::vector{id}, id);
  }
}

void Selection::select(const std::vector<NodeId>& nodes) {
  assign(nodes, NodeId());
}

void Selection::select(
    const std::vector<NodeId>& nodes, const NodeId& primary) {
  assign(nodes, primary);
}

void Selection::add(const NodeId& id) {
  if(!m_document->find(id)) {
    return;
  }
  if(m_members.insert(id).second) {
    m_nodes.push_back(id);
  } else if(m_primary == id) {
    return;
  }
  m_primary = id;
  m_signal();
}

void Selection::remove(const NodeId& id) {
  if(!m_members.erase(id)) {
    return;
  }
  std::erase(m_nodes, id);
  if(m_primary == id) {
    if(m_nodes.empty()) {
      m_primary = NodeId();
    } else {
      m_primary = m_nodes.back();
    }
  }
  m_signal();
}

void Selection::toggle(const NodeId& id) {
  if(contains(id)) {
    remove(id);
  } else {
    add(id);
  }
}

void Selection::extend(const NodeId& id) {
  auto node = m_document->find(id);
  auto anchor = m_document->find(m_primary);
  if(!node || !anchor || node->m_parent != anchor->m_parent ||
      (node->m_parent.is_nil() && (!m_document->is_on_canvas(id) ||
        !m_document->is_on_canvas(m_primary)))) {
    add(id);
    return;
  }
  auto& children = m_document->get_children(node->m_parent);
  auto first = std::find(children.begin(), children.end(), m_primary);
  auto last = std::find(children.begin(), children.end(), id);
  if(first > last) {
    std::swap(first, last);
  }
  assign(std::vector<NodeId>(first, last + 1), m_primary);
}

void Selection::select_all(const NodeId& container) {
  if(!container.is_nil() && !m_document->find(container)) {
    return;
  }
  select(m_document->get_children(container));
}

void Selection::clear() {
  assign(std::vector<NodeId>(), NodeId());
}

Subscription Selection::subscribe(std::function<void ()> observer) {
  return m_signal.subscribe(std::move(observer));
}

bool Selection::assign(std::vector<NodeId> nodes, const NodeId& primary) {
  auto members = std::unordered_set<NodeId>();
  std::erase_if(nodes, [&] (const auto& id) {
    return !m_document->find(id) || !members.insert(id).second;
  });
  auto selected_primary = NodeId();
  if(members.contains(primary)) {
    selected_primary = primary;
  } else if(!nodes.empty()) {
    selected_primary = nodes.back();
  }
  if(nodes == m_nodes && selected_primary == m_primary) {
    return false;
  }
  m_nodes = std::move(nodes);
  m_members = std::move(members);
  m_primary = selected_primary;
  m_signal();
  return true;
}

void Selection::on_change(const ChangeSet& changes) {
  if(changes.m_removed.empty() || m_nodes.empty()) {
    return;
  }
  auto count = std::erase_if(m_nodes, [&] (const auto& id) {
    if(m_document->find(id)) {
      return false;
    }
    m_members.erase(id);
    return true;
  });
  if(count == 0) {
    return;
  }
  if(!m_members.contains(m_primary)) {
    if(m_nodes.empty()) {
      m_primary = NodeId();
    } else {
      m_primary = m_nodes.back();
    }
  }
  m_signal();
}

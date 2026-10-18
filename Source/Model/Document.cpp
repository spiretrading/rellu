#include <algorithm>
#include <stdexcept>
#include "Rellu/Model/Document.hpp"

using namespace Rellu;

struct Document::Observers {
  struct Entry {
    std::function<void (const ChangeSet&)> m_observer;
    bool m_is_released;
  };
  std::vector<std::shared_ptr<Entry>> m_entries;
};

namespace {
  template<typename K, typename V>
  void swap_entries(std::map<K, V>& map,
      std::vector<std::pair<K, std::optional<V>>>& entries) {
    for(auto& entry : entries) {
      auto current = std::optional<V>();
      auto i = map.find(entry.first);
      if(i != map.end()) {
        current = std::move(i->second);
        map.erase(i);
      }
      if(entry.second) {
        map.emplace(entry.first, std::move(*entry.second));
      }
      entry.second = std::move(current);
    }
  }

  template<typename T>
  void swap_value(T& value, std::optional<T>& previous) {
    if(previous) {
      std::swap(value, *previous);
    }
  }

  ChangeSet reverse(const ChangeSet& changes) {
    auto inverse = changes;
    std::swap(inverse.m_inserted, inverse.m_removed);
    return inverse;
  }
}

Document::Document()
  : m_observers(std::make_shared<Observers>()),
    m_transactions(0),
    m_is_publishing(false),
    m_has_transaction(false) {}

Document::~Document() = default;

const NodeStore& Document::get_nodes() const {
  return m_nodes;
}

const Node* Document::find(const NodeId& id) const {
  return m_nodes.find(id);
}

const Node& Document::get(const NodeId& id) const {
  return m_nodes.get(id);
}

const std::vector<NodeId>& Document::get_canvas() const {
  return m_canvas;
}

const std::vector<NodeId>& Document::get_children(const NodeId& parent) const {
  if(parent.is_nil()) {
    return m_canvas;
  }
  return m_nodes.get(parent).m_children;
}

bool Document::is_on_canvas(const NodeId& id) const {
  auto node = m_nodes.find(id);
  return node && node->m_parent.is_nil() &&
    std::find(m_canvas.begin(), m_canvas.end(), id) != m_canvas.end();
}

const Resources& Document::get_resources() const {
  return m_resources;
}

const Prototype& Document::get_prototype() const {
  return m_prototype;
}

bool Document::can_undo() const {
  return !m_undo.empty();
}

bool Document::can_redo() const {
  return !m_redo.empty();
}

std::size_t Document::get_undo_count() const {
  return m_undo.size();
}

std::size_t Document::get_redo_count() const {
  return m_redo.size();
}

bool Document::undo() {
  if(m_has_transaction) {
    throw std::logic_error("Undo while a transaction is open.");
  }
  if(m_undo.empty()) {
    return false;
  }
  auto revision = std::move(m_undo.back());
  m_undo.pop_back();
  swap(revision);
  m_redo.push_back(std::move(revision));
  publish(reverse(m_redo.back().m_changes));
  return true;
}

bool Document::redo() {
  if(m_has_transaction) {
    throw std::logic_error("Redo while a transaction is open.");
  }
  if(m_redo.empty()) {
    return false;
  }
  auto revision = std::move(m_redo.back());
  m_redo.pop_back();
  swap(revision);
  m_undo.push_back(std::move(revision));
  publish(m_undo.back().m_changes);
  return true;
}

void Document::clear_history() {
  m_undo.clear();
  m_redo.clear();
}

Subscription Document::subscribe(
    std::function<void (const ChangeSet&)> observer) {
  auto entry = std::make_shared<Observers::Entry>(std::move(observer), false);
  m_observers->m_entries.push_back(entry);
  return Subscription([observers = std::weak_ptr(m_observers),
      entry = std::weak_ptr(entry)] {
    auto locked_entry = entry.lock();
    if(!locked_entry) {
      return;
    }
    locked_entry->m_is_released = true;
    if(auto locked_observers = observers.lock()) {
      std::erase(locked_observers->m_entries, locked_entry);
    }
  });
}

void Document::swap(Revision& revision) {
  for(auto& entry : revision.m_nodes) {
    auto current = m_nodes.get_record(entry.first);
    if(entry.second) {
      m_nodes.set(std::move(entry.second));
    } else {
      m_nodes.erase(entry.first);
    }
    entry.second = std::move(current);
  }
  swap_value(m_canvas, revision.m_canvas);
  swap_entries(m_resources.m_components, revision.m_components);
  swap_entries(m_resources.m_images, revision.m_images);
  swap_value(m_resources.m_swatches, revision.m_swatches);
  swap_value(m_resources.m_character_styles, revision.m_character_styles);
  swap_value(m_prototype.m_interactions, revision.m_interactions);
  swap_value(m_prototype.m_home_artboard, revision.m_home_artboard);
  swap_value(m_prototype.m_flows, revision.m_flows);
}

void Document::publish(const ChangeSet& changes) {
  if(m_is_publishing) {
    m_pending.push_back(changes);
    return;
  } else if(m_observers->m_entries.empty()) {
    return;
  }
  m_pending.push_back(changes);
  m_is_publishing = true;
  struct Guard {
    bool& m_is_publishing;

    ~Guard() {
      m_is_publishing = false;
    }
  };
  auto guard = Guard(m_is_publishing);
  while(!m_pending.empty()) {
    auto current = std::move(m_pending.front());
    m_pending.pop_front();
    auto entries = m_observers->m_entries;
    for(auto& entry : entries) {
      if(!entry->m_is_released) {
        entry->m_observer(current);
      }
    }
  }
}

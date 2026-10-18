#include <stdexcept>
#include <utility>
#include "Rellu/Model/NodeStore.hpp"

using namespace Rellu;

std::size_t NodeStore::get_size() const {
  return m_index.size();
}

bool NodeStore::contains(const NodeId& id) const {
  return m_index.contains(id);
}

const Node* NodeStore::find(const NodeId& id) const {
  auto entry = m_index.find(id);
  if(entry == m_index.end()) {
    return nullptr;
  }
  return m_slots[entry->second].m_record.get();
}

const Node& NodeStore::get(const NodeId& id) const {
  if(auto node = find(id)) {
    return *node;
  }
  throw std::out_of_range("Node not found: " + to_string(id));
}

std::shared_ptr<const Node> NodeStore::get_record(const NodeId& id) const {
  auto entry = m_index.find(id);
  if(entry == m_index.end()) {
    return nullptr;
  }
  return m_slots[entry->second].m_record;
}

void NodeStore::insert(Node node) {
  auto id = node.get_id();
  if(m_index.contains(id)) {
    throw std::invalid_argument("Duplicate node: " + to_string(id));
  }
  auto record = std::make_shared<Node>(std::move(node));
  auto slot = allocate();
  m_slots[slot] = Slot(std::move(record), true);
  m_index.emplace(id, slot);
}

void NodeStore::set(std::shared_ptr<const Node> record) {
  auto id = record->get_id();
  auto entry = m_index.find(id);
  if(entry != m_index.end()) {
    m_slots[entry->second] = Slot(std::move(record), false);
    return;
  }
  auto slot = allocate();
  m_slots[slot] = Slot(std::move(record), false);
  m_index.emplace(id, slot);
}

std::shared_ptr<const Node> NodeStore::erase(const NodeId& id) {
  auto entry = m_index.find(id);
  if(entry == m_index.end()) {
    return nullptr;
  }
  auto slot = entry->second;
  m_index.erase(entry);
  auto record = std::move(m_slots[slot].m_record);
  m_slots[slot] = Slot(nullptr, false);
  m_free.push_back(slot);
  return record;
}

Node& NodeStore::edit(const NodeId& id) {
  auto entry = m_index.find(id);
  if(entry == m_index.end()) {
    throw std::out_of_range("Node not found: " + to_string(id));
  }
  auto& slot = m_slots[entry->second];
  if(!slot.m_is_mutable || slot.m_record.use_count() != 1) {
    slot.m_record = std::make_shared<Node>(*slot.m_record);
    slot.m_is_mutable = true;
  }
  return const_cast<Node&>(*slot.m_record);
}

std::size_t NodeStore::allocate() {
  if(!m_free.empty()) {
    auto slot = m_free.back();
    m_free.pop_back();
    return slot;
  }
  m_slots.emplace_back(nullptr, false);
  return m_slots.size() - 1;
}

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>
#include "Rellu/Model/NodeStore.hpp"

using namespace Rellu;

namespace {
  constexpr auto EMPTY = std::numeric_limits<std::uint32_t>::max();
  constexpr auto MINIMUM_CAPACITY = std::size_t(16);

  std::size_t get_capacity(std::size_t count) {
    return std::max(MINIMUM_CAPACITY, std::bit_ceil(count + count / 3 + 1));
  }

  std::uint32_t get_tag(const NodeId& id) {
    auto hash = static_cast<std::uint64_t>(std::hash<NodeId>()(id));
    return static_cast<std::uint32_t>((hash * 0x9E3779B97F4A7C15ULL) >> 32);
  }
}

NodeStore::NodeStore() noexcept
  : m_size(0),
    m_shift(32) {}

std::size_t NodeStore::get_size() const {
  return m_size;
}

bool NodeStore::contains(const NodeId& id) const {
  return find_bucket(id) != NONE;
}

const Node* NodeStore::find(const NodeId& id) const {
  auto slot = find_slot(id);
  if(slot == NONE) {
    return nullptr;
  }
  return m_slots[slot].m_record.get();
}

const Node& NodeStore::get(const NodeId& id) const {
  if(auto node = find(id)) {
    return *node;
  }
  throw std::out_of_range("Node not found: " + to_string(id));
}

std::shared_ptr<const Node> NodeStore::get_record(const NodeId& id) const {
  auto slot = find_slot(id);
  if(slot == NONE) {
    return nullptr;
  }
  return m_slots[slot].m_record;
}

void NodeStore::insert(Node node) {
  if(contains(node.get_id())) {
    throw std::invalid_argument("Duplicate node: " + to_string(node.get_id()));
  }
  add(std::make_shared<Node>(std::move(node)), true);
}

void NodeStore::set(std::shared_ptr<const Node> record) {
  auto slot = find_slot(record->get_id());
  if(slot == NONE) {
    add(std::move(record), false);
    return;
  }
  m_slots[slot].m_record = std::move(record);
  m_slots[slot].m_is_mutable = false;
}

std::shared_ptr<const Node> NodeStore::erase(const NodeId& id) {
  auto slot = find_slot(id);
  if(slot == NONE) {
    return nullptr;
  }
  return remove_slot(slot);
}

Node& NodeStore::edit(const NodeId& id) {
  auto slot = find_slot(id);
  if(slot == NONE) {
    throw std::out_of_range("Node not found: " + to_string(id));
  }
  return edit_slot(slot);
}

std::size_t NodeStore::find_slot(const NodeId& id) const {
  auto bucket = find_bucket(id);
  if(bucket == NONE) {
    return NONE;
  }
  return m_buckets[bucket].m_slot;
}

std::size_t NodeStore::add(std::shared_ptr<const Node> record,
    bool is_mutable) {
  if((m_size + 1) * 4 > m_buckets.size() * 3) {
    rehash(get_capacity(m_size + 1));
  }
  auto slot = allocate();
  place(get_tag(record->get_id()), static_cast<std::uint32_t>(slot));
  m_slots[slot] = Slot(std::move(record), 0, is_mutable);
  ++m_size;
  return slot;
}

Node& NodeStore::edit_slot(std::size_t slot) {
  auto& entry = m_slots[slot];
  if(!entry.m_is_mutable || entry.m_record.use_count() != 1) {
    entry.m_record = std::make_shared<Node>(*entry.m_record);
    entry.m_is_mutable = true;
  }
  return const_cast<Node&>(*entry.m_record);
}

std::shared_ptr<const Node> NodeStore::remove_slot(std::size_t slot) {
  auto& entry = m_slots[slot];
  erase_bucket(find_bucket(entry.m_record->get_id()));
  auto record = std::move(entry.m_record);
  entry = Slot(nullptr, 0, false);
  m_free.push_back(slot);
  --m_size;
  return record;
}

std::size_t NodeStore::allocate() {
  if(!m_free.empty()) {
    auto slot = m_free.back();
    m_free.pop_back();
    return slot;
  }
  if(m_slots.size() >= EMPTY) {
    throw std::length_error("Too many nodes.");
  }
  m_slots.emplace_back(nullptr, 0, false);
  return m_slots.size() - 1;
}

std::size_t NodeStore::find_bucket(const NodeId& id) const {
  if(m_buckets.empty()) {
    return NONE;
  }
  auto tag = get_tag(id);
  auto mask = m_buckets.size() - 1;
  for(auto i = std::size_t(tag >> m_shift); ; i = (i + 1) & mask) {
    auto& bucket = m_buckets[i];
    if(bucket.m_slot == EMPTY) {
      return NONE;
    } else if(bucket.m_tag == tag &&
        m_slots[bucket.m_slot].m_record->get_id() == id) {
      return i;
    }
  }
}

void NodeStore::place(std::uint32_t tag, std::uint32_t slot) {
  auto mask = m_buckets.size() - 1;
  auto i = std::size_t(tag >> m_shift);
  while(m_buckets[i].m_slot != EMPTY) {
    i = (i + 1) & mask;
  }
  m_buckets[i] = Bucket(slot, tag);
}

void NodeStore::erase_bucket(std::size_t bucket) {
  auto mask = m_buckets.size() - 1;
  auto hole = bucket;
  for(auto i = (hole + 1) & mask; m_buckets[i].m_slot != EMPTY;
      i = (i + 1) & mask) {
    auto home = std::size_t(m_buckets[i].m_tag >> m_shift);
    if(((i - home) & mask) >= ((i - hole) & mask)) {
      m_buckets[hole] = m_buckets[i];
      hole = i;
    }
  }
  m_buckets[hole].m_slot = EMPTY;
}

void NodeStore::rehash(std::size_t capacity) {
  auto buckets =
    std::exchange(m_buckets, std::vector<Bucket>(capacity, Bucket(EMPTY, 0)));
  m_shift = 32 - std::countr_zero(capacity);
  for(auto& bucket : buckets) {
    if(bucket.m_slot != EMPTY) {
      place(bucket.m_tag, bucket.m_slot);
    }
  }
}

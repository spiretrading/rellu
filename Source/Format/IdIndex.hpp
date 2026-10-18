#ifndef RELLU_FORMAT_ID_INDEX_HPP
#define RELLU_FORMAT_ID_INDEX_HPP
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>
#include "Rellu/Model/NodeId.hpp"

namespace Rellu::Details {

  /** Maps identifiers to 32-bit values in an open addressing hash table. */
  class IdIndex {
    public:

      /** The largest value that can be stored. */
      static constexpr auto MAXIMUM_VALUE =
        std::numeric_limits<std::uint32_t>::max() - 1;

      /** Constructs an empty IdIndex. */
      IdIndex() noexcept;

      /** Returns the number of identifiers. */
      std::size_t get_size() const;

      /**
       * Prepares for a number of identifiers.
       * @param count The number of identifiers expected.
       */
      void reserve(std::size_t count);

      /**
       * Adds an identifier unless it is already present.
       * @param id The identifier.
       * @param value The value associated with the identifier, at most
       *        MAXIMUM_VALUE.
       * @return <code>true</code> iff the identifier was added.
       */
      bool insert(const NodeId& id, std::uint32_t value);

      /**
       * Finds the value associated with an identifier.
       * @param id The identifier.
       * @return The value, or <code>nullptr</code> if the identifier is
       *         absent.
       */
      const std::uint32_t* find(const NodeId& id) const;

      /**
       * Replaces every value.
       * @param f The function mapping each value to its replacement.
       */
      void transform(auto f);

    private:
      struct Entry {
        NodeId m_id;
        std::uint32_t m_value;
      };
      static constexpr auto EMPTY = std::numeric_limits<std::uint32_t>::max();
      std::vector<Entry> m_entries;
      std::size_t m_size;

      std::size_t get_slot(const NodeId& id) const;
  };

  inline IdIndex::IdIndex() noexcept
    : m_size(0) {}

  inline std::size_t IdIndex::get_size() const {
    return m_size;
  }

  inline void IdIndex::reserve(std::size_t count) {
    auto capacity = std::bit_ceil(std::max<std::size_t>(count + count / 2, 16));
    if(capacity <= m_entries.size()) {
      return;
    }
    auto entries = std::move(m_entries);
    m_entries.assign(capacity, Entry(NodeId(), EMPTY));
    m_size = 0;
    for(auto& entry : entries) {
      if(entry.m_value != EMPTY) {
        insert(entry.m_id, entry.m_value);
      }
    }
  }

  inline bool IdIndex::insert(const NodeId& id, std::uint32_t value) {
    if(4 * (m_size + 1) > 3 * m_entries.size()) {
      reserve(2 * (m_size + 1));
    }
    auto& entry = m_entries[get_slot(id)];
    if(entry.m_value != EMPTY) {
      return false;
    }
    entry = Entry(id, value);
    ++m_size;
    return true;
  }

  inline const std::uint32_t* IdIndex::find(const NodeId& id) const {
    if(m_entries.empty()) {
      return nullptr;
    }
    auto& entry = m_entries[get_slot(id)];
    if(entry.m_value == EMPTY) {
      return nullptr;
    }
    return &entry.m_value;
  }

  void IdIndex::transform(auto f) {
    for(auto& entry : m_entries) {
      if(entry.m_value != EMPTY) {
        entry.m_value = f(entry.m_value);
      }
    }
  }

  inline std::size_t IdIndex::get_slot(const NodeId& id) const {
    auto mask = m_entries.size() - 1;
    auto hash = static_cast<std::uint64_t>(std::hash<NodeId>()(id));
    auto slot = static_cast<std::size_t>(
      (hash * 0x9E3779B97F4A7C15ULL) >> 32) & mask;
    while(m_entries[slot].m_value != EMPTY && m_entries[slot].m_id != id) {
      slot = (slot + 1) & mask;
    }
    return slot;
  }
}

#endif

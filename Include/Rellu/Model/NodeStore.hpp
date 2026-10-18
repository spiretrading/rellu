#ifndef RELLU_MODEL_NODE_STORE_HPP
#define RELLU_MODEL_NODE_STORE_HPP
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Holds node records in a slot array with a free list, indexed by NodeId.
   * Records are immutable and shared, so copying one out of the store keeps
   * it unchanged by later edits.
   */
  class NodeStore {
    public:

      /** Constructs an empty NodeStore. */
      NodeStore() noexcept;

      /** Returns the number of nodes. */
      std::size_t get_size() const;

      /** Returns <code>true</code> iff a node has the given id. */
      bool contains(const NodeId& id) const;

      /**
       * Finds a node.
       * @param id The node's id.
       * @return The node, or <code>nullptr</code> if there is none, valid
       *         until the store is next modified.
       */
      const Node* find(const NodeId& id) const;

      /**
       * Returns a node, throwing std::out_of_range if there is none.
       * @param id The node's id.
       * @return The node, valid until the store is next modified.
       */
      const Node& get(const NodeId& id) const;

      /**
       * Returns the shared record of a node.
       * @param id The node's id.
       * @return The record, or <code>nullptr</code> if there is none.
       */
      std::shared_ptr<const Node> get_record(const NodeId& id) const;

      /**
       * Calls a function with every node, in no particular order.
       * @param f The function to call with each <code>const Node&</code>.
       */
      void for_each(auto f) const;

      /**
       * Adds a node, throwing std::invalid_argument if its id is in use.
       * @param node The node to add.
       */
      void insert(Node node);

      /**
       * Stores a record, replacing any record with the same id.
       * @param record The non-null record to store.
       */
      void set(std::shared_ptr<const Node> record);

      /**
       * Removes a node.
       * @param id The node's id.
       * @return The removed record, or <code>nullptr</code> if there was none.
       */
      std::shared_ptr<const Node> erase(const NodeId& id);

      /**
       * Returns a node for modification, leaving every copy of its record
       * held outside the store unchanged. Throws std::out_of_range if there
       * is no such node.
       * @param id The node's id.
       * @return The modifiable node, valid until the store is next modified.
       */
      Node& edit(const NodeId& id);

    private:
      friend class Transaction;
      static constexpr auto NONE = std::numeric_limits<std::size_t>::max();
      struct Slot {
        std::shared_ptr<const Node> m_record;
        std::uint64_t m_mark;
        bool m_is_mutable;
      };
      struct Bucket {
        std::uint32_t m_slot;
        std::uint32_t m_tag;
      };
      std::vector<Slot> m_slots;
      std::vector<std::size_t> m_free;
      std::vector<Bucket> m_buckets;
      std::size_t m_size;
      int m_shift;

      std::size_t find_slot(const NodeId& id) const;
      std::size_t add(std::shared_ptr<const Node> record, bool is_mutable);
      Node& edit_slot(std::size_t slot);
      std::shared_ptr<const Node> remove_slot(std::size_t slot);
      std::size_t allocate();
      std::size_t find_bucket(const NodeId& id) const;
      void place(std::uint32_t tag, std::uint32_t slot);
      void erase_bucket(std::size_t bucket);
      void rehash(std::size_t capacity);
  };

  void NodeStore::for_each(auto f) const {
    for(auto& slot : m_slots) {
      if(slot.m_record) {
        f(static_cast<const Node&>(*slot.m_record));
      }
    }
  }
}

#endif

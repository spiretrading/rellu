#ifndef RELLU_EDITOR_SELECTION_HPP
#define RELLU_EDITOR_SELECTION_HPP
#include <cstddef>
#include <functional>
#include <unordered_set>
#include <vector>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * The ordered set of selected nodes of a document, with a primary node
   * anchoring range selection. Nodes removed from the document leave the
   * selection, and the primary node passes to the most recently selected
   * node remaining.
   */
  class Selection {
    public:

      /**
       * Constructs an empty Selection.
       * @param document The document whose nodes are selected.
       */
      explicit Selection(Document& document);

      /** Returns the selected nodes, in the order they were selected. */
      const std::vector<NodeId>& get_nodes() const;

      /** Returns the primary node, or nil if the selection is empty. */
      const NodeId& get_primary() const;

      /** Returns the number of selected nodes. */
      std::size_t get_size() const;

      /** Returns <code>true</code> iff no node is selected. */
      bool is_empty() const;

      /** Returns <code>true</code> iff a node is selected. */
      bool contains(const NodeId& id) const;

      /**
       * Selects a single node.
       * @param id The node to select, or nil to clear the selection.
       */
      void select(const NodeId& id);

      /**
       * Replaces the selection, the last node selected becoming primary.
       * @param nodes The nodes to select.
       */
      void select(const std::vector<NodeId>& nodes);

      /**
       * Replaces the selection.
       * @param nodes The nodes to select.
       * @param primary The primary node, used only if among the
       *        <i>nodes</i>, otherwise the last node is primary.
       */
      void select(const std::vector<NodeId>& nodes, const NodeId& primary);

      /**
       * Adds a node, making it primary.
       * @param id The node to add.
       */
      void add(const NodeId& id);

      /**
       * Removes a node.
       * @param id The node to remove.
       */
      void remove(const NodeId& id);

      /**
       * Adds a node that is not selected, making it primary, or removes one
       * that is.
       * @param id The node to toggle.
       */
      void toggle(const NodeId& id);

      /**
       * Selects the siblings from the primary node to a node, as a
       * shift-click in the layers panel, keeping the primary node. Adds the
       * node if it is not a sibling of the primary node.
       * @param id The node ending the range.
       */
      void extend(const NodeId& id);

      /**
       * Selects every unlocked child of a container, hidden ones included
       * as Adobe XD does.
       * @param container The container, or nil for the canvas.
       */
      void select_all(const NodeId& container);

      /** Deselects every node. */
      void clear();

      /**
       * Subscribes an observer to every change of the selection.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

    private:
      Document* m_document;
      std::vector<NodeId> m_nodes;
      std::unordered_set<NodeId> m_members;
      NodeId m_primary;
      Signal m_signal;
      Subscription m_subscription;

      Selection(const Selection&) = delete;
      Selection& operator =(const Selection&) = delete;
      bool assign(std::vector<NodeId> nodes, const NodeId& primary);
      void on_change(const ChangeSet& changes);
  };
}

#endif

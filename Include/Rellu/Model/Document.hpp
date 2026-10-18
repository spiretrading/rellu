#ifndef RELLU_MODEL_DOCUMENT_HPP
#define RELLU_MODEL_DOCUMENT_HPP
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/NodeStore.hpp"
#include "Rellu/Model/Prototype.hpp"
#include "Rellu/Model/Resources.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * A design document: the nodes on the canvas and in component trees, the
   * shared resources and the prototype. All changes are made through a
   * Transaction, and every committed change, undo and redo is published to
   * observers as a ChangeSet.
   */
  class Document {
    public:

      /** Constructs an empty Document. */
      Document();

      ~Document();

      /** Returns every node, on the canvas or in a component tree. */
      const NodeStore& get_nodes() const;

      /**
       * Finds a node.
       * @param id The node's id.
       * @return The node, or <code>nullptr</code> if there is none, valid
       *         until the document is next modified.
       */
      const Node* find(const NodeId& id) const;

      /**
       * Returns a node, throwing std::out_of_range if there is none.
       * @param id The node's id.
       * @return The node, valid until the document is next modified.
       */
      const Node& get(const NodeId& id) const;

      /** Returns the top-level canvas nodes in z order. */
      const std::vector<NodeId>& get_canvas() const;

      /**
       * Returns the children of a node, throwing std::out_of_range if there
       * is no such node.
       * @param parent The parent's id, or nil for the canvas.
       * @return The children in z order, valid until the document is next
       *         modified.
       */
      const std::vector<NodeId>& get_children(const NodeId& parent) const;

      /**
       * Tests whether a node is a top-level canvas node.
       * @param id The node's id.
       * @return <code>true</code> iff the node is on the canvas without a
       *         parent.
       */
      bool is_on_canvas(const NodeId& id) const;

      /** Returns the components, images, swatches and character styles. */
      const Resources& get_resources() const;

      /** Returns the interactions and flows. */
      const Prototype& get_prototype() const;

      /** Returns <code>true</code> iff there is a change to undo. */
      bool can_undo() const;

      /** Returns <code>true</code> iff there is an undone change to redo. */
      bool can_redo() const;

      /**
       * Reverts the most recent committed or redone change, throwing
       * std::logic_error while a transaction is open.
       * @return <code>true</code> iff there was a change to undo.
       */
      bool undo();

      /**
       * Reapplies the most recently undone change, throwing std::logic_error
       * while a transaction is open.
       * @return <code>true</code> iff there was a change to redo.
       */
      bool redo();

      /** Discards the undo and redo history. */
      void clear_history();

      /**
       * Subscribes an observer to every ChangeSet published from now on, in
       * commit order, after the observers subscribed before it.
       * @param observer The function called with each ChangeSet.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(
        std::function<void (const ChangeSet&)> observer);

    private:
      friend class Transaction;
      struct Revision {
        std::vector<std::pair<NodeId, std::shared_ptr<const Node>>> m_nodes;
        std::optional<std::vector<NodeId>> m_canvas;
        std::vector<std::pair<ComponentId, std::optional<Component>>>
          m_components;
        std::vector<std::pair<ImageId, std::optional<ImageResource>>>
          m_images;
        std::optional<std::vector<ColorSwatch>> m_swatches;
        std::optional<std::vector<CharacterStyle>> m_character_styles;
        std::optional<std::vector<Interaction>> m_interactions;
        std::optional<NodeId> m_home_artboard;
        std::optional<std::vector<Flow>> m_flows;
        ChangeSet m_changes;
      };
      struct Observers;
      NodeStore m_nodes;
      std::vector<NodeId> m_canvas;
      Resources m_resources;
      Prototype m_prototype;
      std::vector<Revision> m_undo;
      std::vector<Revision> m_redo;
      std::shared_ptr<Observers> m_observers;
      std::deque<ChangeSet> m_pending;
      bool m_is_publishing;
      bool m_has_transaction;

      Document(const Document&) = delete;
      Document& operator =(const Document&) = delete;
      void swap(Revision& revision);
      void publish(ChangeSet changes);
  };
}

#endif

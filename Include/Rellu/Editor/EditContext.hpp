#ifndef RELLU_EDITOR_EDIT_CONTEXT_HPP
#define RELLU_EDITOR_EDIT_CONTEXT_HPP
#include <functional>
#include <vector>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * Where the user is working in a document: the container entered for
   * editing, the active artboard and the node under the pointer. A removed
   * container gives way to its nearest remaining ancestor, and a removed
   * artboard or hovered node to none.
   */
  class EditContext {
    public:

      /**
       * Constructs an EditContext at the top level of a document.
       * @param document The document worked in.
       */
      explicit EditContext(Document& document);

      /** Returns the container entered, or nil at the top level. */
      const NodeId& get_container() const;

      /** Returns the active artboard, or nil if there is none. */
      const NodeId& get_artboard() const;

      /** Returns the node under the pointer, or nil if there is none. */
      const NodeId& get_hover() const;

      /**
       * Returns the container whose children select all applies to: the
       * container entered, otherwise the active artboard, otherwise nil for
       * the canvas.
       */
      NodeId get_scope() const;

      /**
       * Enters a group or compound shape to edit its children, making its
       * artboard active.
       * @param container The container to enter.
       * @return <code>true</code> iff the <i>container</i> was entered.
       */
      bool enter(const NodeId& container);

      /** Leaves the container entered for its nearest enterable ancestor. */
      void exit();

      /**
       * Sets the active artboard.
       * @param artboard The artboard, or nil for none.
       */
      void set_artboard(const NodeId& artboard);

      /**
       * Sets the node under the pointer.
       * @param hover The node, or nil for none.
       */
      void set_hover(const NodeId& hover);

      /**
       * Finds the node that clicking on a node selects: its ancestor within
       * the container entered, or at the top level its ancestor directly
       * within an artboard or on the canvas.
       * @param hit The deepest node under the pointer.
       * @return The node to select, or nil if it is unknown or locked.
       */
      NodeId resolve(const NodeId& hit) const;

      /**
       * Subscribes an observer to every change of the container, artboard or
       * hovered node.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

    private:
      Document* m_document;
      NodeId m_container;
      std::vector<NodeId> m_ancestors;
      NodeId m_artboard;
      NodeId m_hover;
      Signal m_signal;
      Subscription m_subscription;

      EditContext(const EditContext&) = delete;
      EditContext& operator =(const EditContext&) = delete;
      void on_change(const ChangeSet& changes);
  };
}

#endif

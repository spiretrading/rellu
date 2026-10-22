#ifndef RELLU_EDITOR_EDIT_CONTEXT_HPP
#define RELLU_EDITOR_EDIT_CONTEXT_HPP
#include <functional>
#include <optional>
#include <vector>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /** The interactions being edited, as an undoable step records them. */
  struct InteractionSelection {

    /** The interactions' ids, in the order selected. */
    std::vector<InteractionId> m_interactions;

    /** The pending interactions among them. */
    std::vector<Interaction> m_pending;

    bool operator ==(const InteractionSelection&) const = default;
  };

  /**
   * Where the user is working in a document: the container entered for
   * editing, the active artboard, the node under the pointer and the
   * interactions being edited. A removed container gives way to its nearest
   * remaining ancestor, and a removed artboard or hovered node to none.
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
       * Returns the first of the interactions being edited, or nil if there
       * is none.
       */
      const InteractionId& get_interaction() const;

      /** Returns the interactions being edited, in the order selected. */
      const std::vector<InteractionId>& get_interactions() const;

      /**
       * Returns whether interactions are edited, as in Adobe XD's Prototype
       * mode.
       */
      bool is_prototyping() const;

      /**
       * Returns the interactions being edited for an undoable step to
       * record, or none outside of Prototype mode.
       */
      std::optional<InteractionSelection> get_interaction_selection() const;

      /**
       * Returns the node whose interactions being edited were chosen, or
       * nil if none were chosen.
       */
      const NodeId& get_interaction_owner() const;

      /**
       * Returns the interaction last edited, whose properties new
       * interactions take, if any.
       */
      const std::optional<Interaction>& get_last_edited() const;

      /**
       * Returns the interactions being edited that are not in the document
       * because they lack the destination their action needs, as Adobe XD
       * keeps a pending wire.
       */
      const std::vector<Interaction>& get_pending() const;

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
       * Sets the interaction being edited, chosen for its owner.
       * @param interaction The interaction's id, or nil for none chosen.
       */
      void set_interaction(const InteractionId& interaction);

      /**
       * Sets the interactions being edited, dropping the pending
       * interactions not among them.
       * @param owner The node they were chosen for, or nil for none.
       * @param interactions The interactions' ids, in the order selected.
       */
      void set_interactions(
        const NodeId& owner, std::vector<InteractionId> interactions);

      /**
       * Sets the interaction last edited.
       * @param interaction The interaction, or none.
       */
      void set_last_edited(std::optional<Interaction> interaction);

      /**
       * Sets the pending interactions, those not among the interactions
       * being edited being dropped.
       * @param interactions The pending interactions.
       */
      void set_pending(std::vector<Interaction> interactions);

      /**
       * Sets whether interactions are edited, as in Adobe XD's Prototype
       * mode.
       * @param is_prototyping Whether interactions are edited.
       */
      void set_prototyping(bool is_prototyping);

      /**
       * Restores the interactions an undoable step recorded as being
       * edited, in Prototype mode only, dropping those whose node no longer
       * exists.
       * @param owner The node the interactions are chosen for.
       * @param selection The interactions recorded, or none to edit none.
       */
      void restore_interactions(const NodeId& owner,
        const std::optional<InteractionSelection>& selection);

      /**
       * Finds the node that clicking on a node selects: its ancestor within
       * the container entered, or at the top level its ancestor directly
       * within an artboard or on the canvas. A locked node is selected like
       * any other, as Adobe XD selects it by a click.
       * @param hit The deepest node under the pointer.
       * @return The node to select, or nil if it is unknown.
       */
      NodeId resolve(const NodeId& hit) const;

      /**
       * Subscribes an observer to every change of the container, artboard,
       * hovered node or interactions being edited.
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
      InteractionId m_interaction;
      std::vector<InteractionId> m_interactions;
      NodeId m_interaction_owner;
      std::optional<Interaction> m_last_edited;
      std::vector<Interaction> m_pending;
      bool m_is_prototyping;
      Signal m_signal;
      Subscription m_subscription;

      EditContext(const EditContext&) = delete;
      EditContext& operator =(const EditContext&) = delete;
      void on_change(const ChangeSet& changes);
  };
}

#endif

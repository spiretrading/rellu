#ifndef RELLU_PROTOTYPE_INTERACTION_COMMANDS_HPP
#define RELLU_PROTOTYPE_INTERACTION_COMMANDS_HPP
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Model/Prototype.hpp"

namespace Rellu {

  /**
   * The duration in seconds of a new interaction's animation, 0.3 at the
   * single precision Adobe XD stores durations with.
   */
  inline constexpr auto DEFAULT_DURATION = static_cast<double>(0.3f);

  /** The delay in seconds of a new time trigger. */
  inline constexpr auto DEFAULT_DELAY = 1.0;

  /**
   * The duration in seconds the INTERACTION section shows, disabled, before
   * a node has an interaction.
   */
  inline constexpr auto PLACEHOLDER_DURATION = 0.2;

  /**
   * Returns a duration or delay at the single precision Adobe XD stores it
   * with.
   * @param seconds The duration or delay in seconds.
   */
  double to_stored_seconds(double seconds);

  /** Returns a dissolve, easing out, over 0.3 seconds. */
  Animation make_default_animation();

  /**
   * Returns the animation of an interaction Adobe XD adds with the + of the
   * INTERACTION section: none, easing out, over 0.3 seconds.
   */
  Animation make_empty_animation();

  /**
   * Makes the interaction Adobe XD adds with the + of the INTERACTION
   * section before any interaction has been edited: a transition triggered
   * by a tap, without a destination and with the empty animation.
   * @param owner The node the interaction is attached to.
   * @return The interaction, with a nil id.
   */
  Interaction make_empty_interaction(const NodeId& owner);

  /**
   * Makes the interaction Adobe XD adds with the + of the INTERACTION
   * section: the trigger, type and animation of the interaction last edited,
   * without its destination, adjusted to the triggers the owner allows.
   * @param document The document holding the owner.
   * @param owner The node the interaction is attached to.
   * @param has_states Whether the owner is within a component with more
   *        than one state.
   * @param last The interaction last edited, or <code>nullptr</code> for
   *        none.
   * @return The interaction, with a nil id.
   */
  Interaction make_new_interaction(const Document& document,
    const NodeId& owner, bool has_states, const Interaction* last);

  /**
   * Makes an interaction where tapping a node transitions to an artboard
   * with a dissolve, easing out, over 0.3 seconds.
   * @param owner The node the interaction is attached to.
   * @param destination The artboard navigated to.
   * @return The interaction, with a nil id.
   */
  Interaction make_default_interaction(
    const NodeId& owner, const NodeId& destination);

  /**
   * Returns whether Adobe XD keeps an interaction in the document: its
   * action has the destination it needs, a transition leading elsewhere
   * than its own artboard, and a hyperlink has an address.
   * @param document The document holding the owner.
   * @param interaction The interaction.
   */
  bool is_complete(const Document& document, const Interaction& interaction);

  /**
   * Leads an interaction to a destination as Adobe XD does when a wire is
   * dropped on it: an artboard keeps a transition or overlay and makes any
   * other action a transition, and a layer makes a Scroll To to the layer
   * directly within its artboard, triggered by a tap unless by keys.
   * @param document The document holding the destination.
   * @param interaction The interaction.
   * @param destination The artboard or layer.
   * @return The interaction leading to the destination.
   */
  Interaction retarget_interaction(const Document& document,
    Interaction interaction, const NodeId& destination);

  /**
   * Makes the interaction Adobe XD adds when a wire is drawn from a node's
   * handle to a destination: the one the + of the INTERACTION section adds,
   * led to the destination.
   * @param document The document holding the owner.
   * @param owner The node the interaction is attached to.
   * @param destination The artboard or layer.
   * @param has_states Whether the owner is within a component with more
   *        than one state.
   * @param last The interaction last edited, or <code>nullptr</code> for
   *        none.
   * @return The interaction, with a nil id.
   */
  Interaction make_wired_interaction(const Document& document,
    const NodeId& owner, const NodeId& destination, bool has_states,
    const Interaction* last);

  /**
   * Returns the name Adobe XD gives a new flow: Flow followed by the
   * smallest positive number no flow's name already uses.
   * @param prototype The prototype the flow is added to.
   */
  std::string make_flow_name(const Prototype& prototype);

  /**
   * Returns the interactions attached to a node, in order.
   * @param document The document.
   * @param owner The node's id.
   */
  std::vector<const Interaction*> get_interactions(
    const Document& document, const NodeId& owner);

  /** Returns the flow starting on an artboard, or nullptr if none does. */
  const Flow* find_flow(const Document& document, const NodeId& artboard);

  /**
   * Returns the layers a Scroll To interaction can scroll to, in the order
   * Adobe XD lists them.
   * @param document The document.
   * @param owner The node the interaction is attached to.
   */
  std::vector<NodeId> get_scroll_destinations(
    const Document& document, const NodeId& owner);

  /**
   * Clamps a Scroll To offset as Adobe XD does.
   * @param document The document.
   * @param destination The layer scrolled to.
   * @param offset The offset.
   * @return The offset clamped.
   */
  double clamp_scroll_offset(
    const Document& document, const NodeId& destination, double offset);

  /**
   * Changes an interaction's trigger as Adobe XD's inspector does, changing
   * the action or trigger where Adobe XD corrects them.
   * @param interaction The interaction.
   * @param trigger The trigger.
   * @return The interaction with its trigger changed.
   */
  Interaction change_trigger(Interaction interaction, Trigger trigger);

  /** A state a component instance can switch to. */
  struct ComponentStateChoice {

    /** The state's id. */
    StateId m_id;

    /** The state's name, Default State for the default state. */
    std::string m_name;
  };

  /** The states of the component instance a node lies in. */
  struct ComponentStateChoices {

    /** The state the node is shown in, or nil outside a component. */
    StateId m_current;

    /** The states in the order Adobe XD lists them, the default first. */
    std::vector<ComponentStateChoice> m_states;
  };

  /**
   * Finds the states of the component instance a node lies in.
   * @param document The document.
   * @param id The node.
   */
  ComponentStateChoices find_component_states(
    const Document& document, const NodeId& id);

  /**
   * Returns the interaction Adobe XD selects when a node is selected: the
   * first by trigger of the node's interactions when they all share one
   * wire, otherwise none.
   * @param document The document.
   * @param owner The node.
   * @return The interaction's id, or nil if none is selected.
   */
  InteractionId find_default_interaction(
    const Document& document, const NodeId& owner);

  /**
   * Returns the artboard or layer an interaction's wire leads to.
   * @param interaction The interaction.
   * @return The destination of its transition, overlay or scroll, or nil
   *         if its wire has none.
   */
  NodeId get_wire_destination(const Interaction& interaction);

  /**
   * Returns whether two interactions share a wire as Adobe XD draws them:
   * the same owner and the same destination, or no destination for both.
   */
  bool is_same_wire(const Interaction& left, const Interaction& right);

  /**
   * Sorts interactions by trigger in Adobe XD's order: tap, drag, hover,
   * time by delay, then keys by device and key.
   * @param interactions The interactions to sort.
   */
  void sort_by_trigger(std::vector<const Interaction*>& interactions);

  /**
   * Returns the interactions sharing an interaction's wire, sorted by
   * trigger.
   * @param document The document holding the interaction.
   * @param interaction The interaction.
   */
  std::vector<const Interaction*> get_wire(
    const Document& document, const Interaction& interaction);

  /**
   * Returns the interaction Adobe XD edits after deleting interactions: when
   * they share one wire, the one before the first of them by trigger among
   * those left on the wire, or after it when it is the first.
   * @param document The document holding the interactions, before deleting.
   * @param removed The ids of the interactions deleted.
   * @return The interaction's id, or nil if none is edited.
   */
  InteractionId find_next_interaction(
    const Document& document, const std::vector<InteractionId>& removed);

  /**
   * Returns the labels Adobe XD lists the interactions of a wire with, in
   * the order of their triggers.
   * @param interactions The interactions.
   */
  std::vector<std::string> label_interactions(
    const std::vector<const Interaction*>& interactions);

  /**
   * Returns the tooltip Adobe XD shows over a wire.
   * @param interactions The wire's interactions.
   */
  std::string summarize_wire(
    const std::vector<const Interaction*>& interactions);

  /** Adds an interaction to a node, labelled Add Interaction. */
  class AddInteractionCommand : public Command {
    public:

      /**
       * Constructs an AddInteractionCommand.
       * @param interaction The interaction, given a fresh id if it has none.
       */
      explicit AddInteractionCommand(Interaction interaction);

      /** Returns the interaction added. */
      const Interaction& get_interaction() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      Interaction m_interaction;
  };

  /** Replaces an interaction, labelled Change Interaction. */
  class SetInteractionCommand : public Command {
    public:

      /**
       * Constructs a SetInteractionCommand.
       * @param interaction The interaction replacing the one with its id.
       */
      explicit SetInteractionCommand(Interaction interaction) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      Interaction m_interaction;
  };

  /** Sets the offset of a Scroll To, labelled Y-Offset. */
  class SetScrollOffsetCommand : public Command {
    public:

      /**
       * Constructs a SetScrollOffsetCommand.
       * @param id The Scroll To interaction's id.
       * @param offset The offset.
       */
      SetScrollOffsetCommand(const InteractionId& id, double offset) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      bool merge(const Command& command) override;

    private:
      InteractionId m_id;
      double m_offset;
  };

  /** Removes an interaction, labelled Delete Interaction. */
  class RemoveInteractionCommand : public Command {
    public:

      /**
       * Constructs a RemoveInteractionCommand.
       * @param id The interaction's id.
       */
      explicit RemoveInteractionCommand(const InteractionId& id) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      InteractionId m_id;
  };

  /**
   * Makes an artboard the start of a flow, labelled Mark as flow root,
   * doing nothing if a flow already starts on it.
   */
  class AddFlowCommand : public Command {
    public:

      /**
       * Constructs an AddFlowCommand.
       * @param artboard The artboard the flow starts on.
       * @param name The flow's name, or empty for the name make_flow_name
       *        gives.
       */
      AddFlowCommand(const NodeId& artboard, std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      NodeId m_artboard;
      std::string m_name;
  };

  /**
   * Removes the flow starting on an artboard, labelled Unmark as flow root,
   * along with the home artboard if it is that artboard.
   */
  class RemoveFlowCommand : public Command {
    public:

      /**
       * Constructs a RemoveFlowCommand.
       * @param artboard The artboard the flow starts on.
       */
      explicit RemoveFlowCommand(const NodeId& artboard) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      NodeId m_artboard;
  };

  /** Renames the flow starting on an artboard, labelled Rename Flow. */
  class RenameFlowCommand : public Command {
    public:

      /**
       * Constructs a RenameFlowCommand.
       * @param artboard The artboard the flow starts on.
       * @param name The new name.
       */
      RenameFlowCommand(const NodeId& artboard, std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      NodeId m_artboard;
      std::string m_name;
  };

  /** Sets the artboard the prototype starts on, labelled Set Home Artboard. */
  class SetHomeArtboardCommand : public Command {
    public:

      /**
       * Constructs a SetHomeArtboardCommand.
       * @param artboard The artboard, or nil to unset it.
       */
      explicit SetHomeArtboardCommand(const NodeId& artboard) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      NodeId m_artboard;
  };
}

#endif

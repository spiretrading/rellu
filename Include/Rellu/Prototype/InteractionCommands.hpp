#ifndef RELLU_PROTOTYPE_INTERACTION_COMMANDS_HPP
#define RELLU_PROTOTYPE_INTERACTION_COMMANDS_HPP
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Model/Prototype.hpp"

namespace Rellu {

  /** The duration in seconds of a new interaction's animation. */
  inline constexpr auto DEFAULT_DURATION = 0.3;

  /** The delay in seconds of a new time trigger. */
  inline constexpr auto DEFAULT_DELAY = 1.0;

  /**
   * The duration in seconds of the animation of an interaction without a
   * destination.
   */
  inline constexpr auto EMPTY_DURATION = 0.2;

  /**
   * Returns the animation Adobe XD gives a new interaction with a
   * destination: a dissolve, easing out, over 0.3 seconds.
   */
  Animation make_default_animation();

  /**
   * Returns the animation Adobe XD shows for an interaction without a
   * destination: none, easing in, over 0.2 seconds.
   */
  Animation make_empty_animation();

  /**
   * Makes the interaction Adobe XD adds with the + of the INTERACTION
   * section: a transition without a destination, triggered by a tap, or
   * after a delay for an artboard.
   * @param owner The node the interaction is attached to.
   * @param is_artboard Whether the owner is an artboard.
   * @return The interaction, with a nil id.
   */
  Interaction make_empty_interaction(const NodeId& owner, bool is_artboard);

  /**
   * Makes the interaction Adobe XD creates when a wire is dragged from a
   * node to an artboard: tapping the node transitions to the artboard with
   * the default animation.
   * @param owner The node the interaction is attached to.
   * @param destination The artboard navigated to.
   * @return The interaction, with a nil id.
   */
  Interaction make_default_interaction(
    const NodeId& owner, const NodeId& destination);

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
   * Makes an artboard the start of a flow, labelled Add Flow, doing nothing
   * if a flow already starts on it.
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
   * Removes the flow starting on an artboard, labelled Remove Flow, along
   * with the home artboard if it is that artboard.
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

#ifndef RELLU_COMMANDS_COMPONENT_COMMANDS_HPP
#define RELLU_COMMANDS_COMPONENT_COMMANDS_HPP
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Makes a component of nodes, labelled Make Component: a single group
   * becomes the main component, other nodes are grouped into one first.
   */
  class MakeComponentCommand : public Command {
    public:

      /**
       * Constructs a MakeComponentCommand.
       * @param nodes The nodes to make a component of.
       * @param measurer The function measuring text, which sizes the
       *        component.
       */
      explicit MakeComponentCommand(std::vector<NodeId> nodes,
        TextMeasurer measurer = estimate_text_bounds) noexcept;

      /** Returns the component made by the last application, or nil. */
      const ComponentId& get_component() const;

      /** Returns the main component made by the last application, or nil. */
      const NodeId& get_main() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      TextMeasurer m_measurer;
      ComponentId m_component;
      NodeId m_main;
  };

  /**
   * Dissolves component instances into their parents as editable copies of
   * their children, labelled Ungroup Component, or Ungroup Components for
   * several.
   */
  class UngroupComponentCommand : public Command {
    public:

      /**
       * Constructs an UngroupComponentCommand.
       * @param nodes The instances.
       */
      explicit UngroupComponentCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      std::vector<NodeId> m_selection;
  };

  /**
   * Discards the overrides of component instances, labelled Reset to Main
   * State.
   */
  class ResetOverridesCommand : public Command {
    public:

      /**
       * Constructs a ResetOverridesCommand.
       * @param nodes The instances.
       */
      explicit ResetOverridesCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
  };

  /**
   * Shows a state of component instances, keeping their overrides, labelled
   * Change State.
   */
  class SetInstanceStateCommand : public Command {
    public:

      /**
       * Constructs a SetInstanceStateCommand.
       * @param nodes The instances.
       * @param state The state, or nil for the default state.
       */
      SetInstanceStateCommand(
        std::vector<NodeId> nodes, const StateId& state) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      StateId m_state;
  };

  /**
   * Replaces component instances with instances of another component in
   * their place, labelled Replace Component. Replacing a main component
   * replaces every instance of its component outside other components and
   * repeat grids.
   */
  class SwapComponentCommand : public Command {
    public:

      /**
       * Constructs a SwapComponentCommand.
       * @param nodes The instances to replace.
       * @param component The component to instantiate.
       * @param measurer The function measuring text, which sizes the
       *        instances.
       */
      SwapComponentCommand(std::vector<NodeId> nodes,
        const ComponentId& component,
        TextMeasurer measurer = estimate_text_bounds) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      ComponentId m_component;
      TextMeasurer m_measurer;
      std::vector<NodeId> m_selection;
  };

  /** Places an instance of a component, labelled Add Component. */
  class InsertInstanceCommand : public Command {
    public:

      /**
       * Constructs an InsertInstanceCommand.
       * @param component The component to instantiate.
       * @param parent The parent, or nil for the canvas.
       * @param transform The instance's transform.
       * @param measurer The function measuring text, which sizes the
       *        instance.
       */
      InsertInstanceCommand(const ComponentId& component, const NodeId& parent,
        const Matrix& transform,
        TextMeasurer measurer = estimate_text_bounds) noexcept;

      /** Returns the instance placed by the last application, or nil. */
      const NodeId& get_instance() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      ComponentId m_component;
      NodeId m_parent;
      Matrix m_transform;
      TextMeasurer m_measurer;
      NodeId m_instance;
  };

  /**
   * Places a main component for a component that has none, labelled Edit
   * Main Component.
   */
  class RestoreMainComponentCommand : public Command {
    public:

      /**
       * Constructs a RestoreMainComponentCommand.
       * @param component The component.
       * @param transform The main component's transform on the canvas.
       * @param measurer The function measuring text, which sizes the main
       *        component.
       */
      RestoreMainComponentCommand(const ComponentId& component,
        const Matrix& transform,
        TextMeasurer measurer = estimate_text_bounds) noexcept;

      /** Returns the main component placed by the last application, or nil. */
      const NodeId& get_main() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      ComponentId m_component;
      Matrix m_transform;
      TextMeasurer m_measurer;
      NodeId m_main;
  };

  /**
   * Replaces the references inside component instances among nodes with
   * editable copies, as the first change of an edit such as editing a text.
   */
  class MakeEditableCommand : public Command {
    public:

      /**
       * Constructs a MakeEditableCommand.
       * @param nodes The nodes.
       * @param label The label of the edit.
       */
      MakeEditableCommand(
        std::vector<NodeId> nodes, std::string label) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      std::string m_label;
  };

  /** Lists the kinds of state Adobe XD adds to a component. */
  enum class StateKind : unsigned char {

    /** A state without interactions. */
    NEW,

    /** A state shown while the pointer hovers over the default state. */
    HOVER,

    /** A state that a tap toggles with the default state. */
    TOGGLE
  };

  /**
   * Adds a state copying the master to a component and shows it on the
   * main component, labelled Add State.
   */
  class AddStateCommand : public Command {
    public:

      /**
       * Constructs an AddStateCommand.
       * @param component The component.
       * @param name The state's name.
       * @param kind The kind of state.
       */
      AddStateCommand(const ComponentId& component, std::string name,
        StateKind kind) noexcept;

      /** Returns the state added by the last application, or nil. */
      const StateId& get_state() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      ComponentId m_component;
      std::string m_name;
      StateKind m_kind;
      StateId m_state;
  };

  /**
   * Removes a state from a component, showing the default state on the
   * instances that showed it, labelled Delete State.
   */
  class RemoveStateCommand : public Command {
    public:

      /**
       * Constructs a RemoveStateCommand.
       * @param component The component.
       * @param state The state.
       */
      RemoveStateCommand(
        const ComponentId& component, const StateId& state) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      ComponentId m_component;
      StateId m_state;
  };

  /** Renames a component's state, labelled Rename State. */
  class RenameStateCommand : public Command {
    public:

      /**
       * Constructs a RenameStateCommand.
       * @param component The component.
       * @param state The state.
       * @param name The new name.
       */
      RenameStateCommand(const ComponentId& component, const StateId& state,
        std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      bool merge(const Command& command) override;

    private:
      ComponentId m_component;
      StateId m_state;
      std::string m_name;
  };

  /**
   * Renames a component along with its master and main component, labelled
   * Rename Component.
   */
  class RenameComponentCommand : public Command {
    public:

      /**
       * Constructs a RenameComponentCommand.
       * @param component The component.
       * @param name The new name.
       */
      RenameComponentCommand(
        const ComponentId& component, std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      bool merge(const Command& command) override;

    private:
      ComponentId m_component;
      std::string m_name;
  };

  /**
   * Replaces instances of linked components with ordinary groups holding
   * copies of what they show, nested instances turned into groups too,
   * labelled Unlink Component, or Unlink Components for several.
   */
  class UnlinkComponentCommand : public Command {
    public:

      /**
       * Constructs an UnlinkComponentCommand.
       * @param nodes The instances to unlink.
       */
      explicit UnlinkComponentCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      std::vector<NodeId> m_selection;
  };

  /**
   * Makes the command unlinking targets that are all instances of linked
   * components, as Adobe XD's Ungroup does for them.
   * @param document The document holding the targets.
   * @param targets The targets.
   * @return The command, or <code>nullptr</code> if a target is not an
   *         instance of a linked component.
   */
  std::unique_ptr<Command> make_unlink_component(
    const Document& document, const std::vector<NodeId>& targets);

  /**
   * Makes the command turning component instances among targets into
   * ordinary groups.
   * @param document The document holding the targets.
   * @param targets The targets.
   * @return The command, or <code>nullptr</code> if no target is an
   *         instance.
   */
  std::unique_ptr<Command> make_ungroup_component(
    const Document& document, const std::vector<NodeId>& targets);

  /**
   * Adds the component commands acting on targets: object.make_component,
   * object.ungroup_component and object.reset_main_state.
   * @param registry The registry to add to.
   * @param measurer The function measuring text, which sizes components.
   */
  void add_component_commands(CommandRegistry& registry,
    TextMeasurer measurer = estimate_text_bounds);
}

#endif

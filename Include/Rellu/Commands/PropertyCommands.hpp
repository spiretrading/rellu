#ifndef RELLU_COMMANDS_PROPERTY_COMMANDS_HPP
#define RELLU_COMMANDS_PROPERTY_COMMANDS_HPP
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"

namespace Rellu {

  /** Sets the opacity of nodes, labelled Change Opacity. */
  class SetOpacityCommand : public Command {
    public:

      /**
       * Constructs a SetOpacityCommand.
       * @param nodes The nodes to change.
       * @param opacity The opacity, clamped to the range [0, 1].
       */
      SetOpacityCommand(std::vector<NodeId> nodes, double opacity) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Takes the opacity of a SetOpacityCommand changing the same nodes. */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      double m_opacity;
  };

  /** Shows or hides nodes, labelled Show or Hide. */
  class SetVisibilityCommand : public Command {
    public:

      /**
       * Constructs a SetVisibilityCommand.
       * @param nodes The nodes to change.
       * @param is_visible Whether the nodes are shown.
       */
      SetVisibilityCommand(
        std::vector<NodeId> nodes, bool is_visible) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      bool m_is_visible;
  };

  /** Locks or unlocks nodes, labelled Lock or Unlock. */
  class SetLockCommand : public Command {
    public:

      /**
       * Constructs a SetLockCommand.
       * @param nodes The nodes to change.
       * @param is_locked Whether the nodes are locked.
       */
      SetLockCommand(std::vector<NodeId> nodes, bool is_locked) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      bool m_is_locked;
  };

  /** Renames a node, labelled Rename. */
  class RenameCommand : public Command {
    public:

      /**
       * Constructs a RenameCommand.
       * @param id The node's id.
       * @param name The new name.
       */
      RenameCommand(const NodeId& id, std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Takes the name of a RenameCommand renaming the same node. */
      bool merge(const Command& command) override;

    private:
      NodeId m_id;
      std::string m_name;
  };
}

#endif

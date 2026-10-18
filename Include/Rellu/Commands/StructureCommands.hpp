#ifndef RELLU_COMMANDS_STRUCTURE_COMMANDS_HPP
#define RELLU_COMMANDS_STRUCTURE_COMMANDS_HPP
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Point.hpp"

namespace Rellu {

  /** Lists the ways of changing the stacking order of nodes. */
  enum class Arrangement : unsigned char {

    /** One step up, past the next sibling above. */
    BRING_FORWARD,

    /** One step down, past the next sibling below. */
    SEND_BACKWARD,

    /** To the top of the siblings. */
    BRING_TO_FRONT,

    /** To the bottom of the siblings. */
    SEND_TO_BACK
  };

  /**
   * Changes the stacking order of nodes among their siblings, keeping their
   * relative order, labelled Bring Forward, Send Backward, Bring to Front or
   * Send to Back.
   */
  class ArrangeCommand : public Command {
    public:

      /**
       * Constructs an ArrangeCommand.
       * @param nodes The nodes to restack.
       * @param arrangement The change of order.
       */
      ArrangeCommand(
        std::vector<NodeId> nodes, Arrangement arrangement) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      Arrangement m_arrangement;
  };

  /**
   * Moves nodes into a container, keeping where they appear on the canvas,
   * labelled Move.
   */
  class ReparentCommand : public Command {
    public:

      /**
       * Constructs a ReparentCommand.
       * @param nodes The nodes to move, placed in z order.
       * @param parent The container, or nil for the canvas.
       * @param index The position among the container's other children at
       *        which the nodes are placed.
       */
      ReparentCommand(std::vector<NodeId> nodes, const NodeId& parent,
        std::size_t index) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      NodeId m_parent;
      std::size_t m_index;
  };

  /**
   * Groups nodes, labelled Group. The group takes the place of the topmost
   * node, and every node keeps where it appears on the canvas.
   */
  class GroupCommand : public Command {
    public:

      /**
       * Constructs a GroupCommand naming the group Group.
       * @param nodes The nodes to group, none of them an artboard.
       */
      explicit GroupCommand(std::vector<NodeId> nodes);

      /**
       * Constructs a GroupCommand.
       * @param nodes The nodes to group, none of them an artboard.
       * @param name The group's name.
       */
      GroupCommand(std::vector<NodeId> nodes, std::string name) noexcept;

      /** Returns the group made by the last application, or nil. */
      const NodeId& get_group() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      std::string m_name;
      NodeId m_group;
  };

  /**
   * Tests whether a node is a group that ungrouping dissolves without
   * changing how its children appear, which excludes masks, scroll groups
   * and component instances.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_ungroupable(const Document& document, const NodeId& id);

  /**
   * Dissolves groups into their parents, labelled Ungroup. The children keep
   * where they appear on the canvas and take on the group's opacity,
   * visibility and lock. Nodes that are not ungroupable are left unchanged.
   */
  class UngroupCommand : public Command {
    public:

      /**
       * Constructs an UngroupCommand.
       * @param nodes The groups to dissolve.
       */
      explicit UngroupCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Returns the children of the dissolved groups followed by the nodes
       * left unchanged.
       */
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      std::vector<NodeId> m_selection;
  };

  /** Removes nodes and their descendants, labelled Delete. */
  class DeleteCommand : public Command {
    public:

      /**
       * Constructs a DeleteCommand.
       * @param nodes The nodes to remove.
       */
      explicit DeleteCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
  };

  /**
   * Copies nodes with their descendants, the state trees of the component
   * instances among them and the interactions they own, labelled Duplicate.
   * Each copy is placed just above its original, and every id the copied
   * nodes and interactions refer to within the copied set is replaced by the
   * id of its copy.
   */
  class DuplicateCommand : public Command {
    public:

      /**
       * Constructs a DuplicateCommand placing copies over their originals.
       * @param nodes The nodes to copy, on the canvas or in a container.
       */
      explicit DuplicateCommand(std::vector<NodeId> nodes);

      /**
       * Constructs a DuplicateCommand.
       * @param nodes The nodes to copy, on the canvas or in a container.
       * @param offset The displacement of the copies in document space.
       */
      DuplicateCommand(std::vector<NodeId> nodes, Point offset);

      /** Returns the copies made by the last application, in z order. */
      const std::vector<NodeId>& get_copies() const;

      /**
       * Returns the id of every node copied by the last application, mapped
       * to the id of its copy.
       */
      const std::unordered_map<NodeId, NodeId>& get_mapping() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      Point m_offset;
      std::vector<NodeId> m_copies;
      std::unordered_map<NodeId, NodeId> m_mapping;
  };
}

#endif

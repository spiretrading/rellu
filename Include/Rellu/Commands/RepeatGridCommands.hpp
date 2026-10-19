#ifndef RELLU_COMMANDS_REPEAT_GRID_COMMANDS_HPP
#define RELLU_COMMANDS_REPEAT_GRID_COMMANDS_HPP
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Layout.hpp"

namespace Rellu {

  /** The space Adobe XD puts between the cells of a new repeat grid. */
  inline constexpr auto DEFAULT_REPEAT_SPACING = 20.0;

  /**
   * Tests whether a node is a group with the repeat grid role.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_repeat_grid(const Document& document, const NodeId& id);

  /**
   * Turns nodes into a repeat grid of one cell, labelled Repeat Grid, named
   * Repeat Grid with the next free number. A single plain group becomes the
   * cell itself; otherwise the nodes are grouped into the cell. The grid
   * takes the place of the topmost node and starts at the top left of the
   * nodes' bounds, which every node keeps where it appears on the canvas.
   */
  class RepeatGridCommand : public Command {
    public:

      /**
       * Constructs a RepeatGridCommand.
       * @param nodes The nodes to repeat, none of them an artboard.
       * @param measurer The function measuring text, which sizes the cell.
       */
      explicit RepeatGridCommand(std::vector<NodeId> nodes,
        TextMeasurer measurer = estimate_text_bounds) noexcept;

      /** Returns the grid made by the last application, or nil. */
      const NodeId& get_grid() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      TextMeasurer m_measurer;
      NodeId m_grid;
  };

  /**
   * Turns repeat grids into plain groups of their cells, labelled Ungroup
   * Grid.
   */
  class UngroupGridCommand : public Command {
    public:

      /**
       * Constructs an UngroupGridCommand.
       * @param nodes The repeat grids, other nodes being left unchanged.
       */
      explicit UngroupGridCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
  };

  /**
   * Sets the size a repeat grid shows, labelled Resize, adding or removing
   * the cells that start within it. A new cell copies the nearest existing
   * cell in its column, or in its row past the last column.
   */
  class ResizeRepeatGridCommand : public Command {
    public:

      /**
       * Constructs a ResizeRepeatGridCommand.
       * @param grid The repeat grid.
       * @param size The size shown.
       */
      ResizeRepeatGridCommand(const NodeId& grid, Size size) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Takes the size of a ResizeRepeatGridCommand of the same grid. */
      bool merge(const Command& command) override;

    private:
      NodeId m_grid;
      Size m_size;
  };

  /**
   * Sets the space between the columns or rows of repeat grids, labelled
   * Spacing, a grid showing all its cells growing or shrinking with them.
   */
  class SetRepeatGridSpacingCommand : public Command {
    public:

      /**
       * Constructs a SetRepeatGridSpacingCommand.
       * @param nodes The repeat grids, other nodes being left unchanged.
       * @param orientation HORIZONTAL for the space between columns,
       *        VERTICAL for the space between rows.
       * @param spacing The space, at least one less than the negated cell
       *        size.
       */
      SetRepeatGridSpacingCommand(std::vector<NodeId> nodes,
        Orientation orientation, double spacing) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the spacing of a SetRepeatGridSpacingCommand of the same grids
       * and orientation.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      Orientation m_orientation;
      double m_spacing;
  };

  /**
   * Makes the command ungrouping targets that are all repeat grids.
   * @param document The document holding the targets.
   * @param targets The targets.
   * @return An UngroupGridCommand, or <code>nullptr</code> unless every
   *         top-level target is a repeat grid.
   */
  std::unique_ptr<Command> make_ungroup_grid_command(
    const Document& document, const std::vector<NodeId>& targets);

  /**
   * Adds object.repeat_grid, making a repeat grid of the targets unless they
   * are all repeat grids already.
   * @param registry The registry to add to.
   * @param measurer The function measuring text, which sizes cells.
   */
  void add_repeat_grid_commands(CommandRegistry& registry,
    TextMeasurer measurer = estimate_text_bounds);
}

#endif

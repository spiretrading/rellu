#ifndef RELLU_COMMANDS_LAYOUTS_HPP
#define RELLU_COMMANDS_LAYOUTS_HPP
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Group.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu {

  /** The settings layouts are recomputed with. */
  struct LayoutOptions {

    /** The function measuring text. */
    TextMeasurer m_measurer = estimate_text_bounds;

    /**
     * The stack items being dragged, which stay where they are while the
     * other items of their stacks make room for them.
     */
    std::vector<NodeId> m_floating;
  };

  /** The ways stacks keep Adobe XD from transforming a selection. */
  struct StackLimits {

    /** Whether the width is kept. */
    bool m_is_width_fixed = false;

    /** Whether the height is kept. */
    bool m_is_height_fixed = false;

    /** Whether rotating on the canvas is disabled. */
    bool m_is_rotation_fixed = false;

    bool operator ==(const StackLimits&) const = default;
  };

  /**
   * Returns how stacks limit transforming a selection as Adobe XD does.
   * @param document The document holding the nodes.
   * @param nodes The nodes selected, in the order selected.
   */
  StackLimits get_stack_limits(
    const Document& document, const std::vector<NodeId>& nodes);

  /**
   * Returns the number of cells of a repeat grid along an axis: those
   * starting within the grid's size, and at least one.
   * @param size The grid's size along the axis.
   * @param cell The cell's size along the axis.
   * @param spacing The space between cells along the axis.
   */
  int count_repeat_cells(double size, double cell, double spacing);

  /**
   * Returns the extent of a run of cells of a repeat grid.
   * @param count The number of cells.
   * @param cell The cell's size along the run.
   * @param spacing The space between cells along the run.
   */
  double get_repeat_extent(int count, double cell, double spacing);

  /**
   * Puts the children of a node in a given order, moving only those out of
   * place.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param parent The node's id.
   * @param order The children in their new order, every one of them listed.
   * @return <code>true</code> iff a child moved.
   */
  bool reorder_children(const Document& document, Transaction& transaction,
    const NodeId& parent, const std::vector<NodeId>& order);

  /**
   * Copies a node with its descendants, as DuplicateCommand does, as the
   * topmost child of a container.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param source The node copied.
   * @param parent The container receiving the copy.
   * @return The copy, or nil if nothing was copied.
   */
  NodeId copy_into(const Document& document, Transaction& transaction,
    const NodeId& source, const NodeId& parent);

  /**
   * Returns the items of a stack in stack order, the topmost child first,
   * without its background.
   * @param document The document holding the stack.
   * @param id The stack's id.
   */
  std::vector<NodeId> get_stack_items(
    const Document& document, const NodeId& id);

  /**
   * Returns the space a stack keeps after an item: the item's own, or for
   * a reference without one, the space its source has in its own stack.
   * @param document The document holding the stack.
   * @param stack The stack role.
   * @param item The item's id.
   */
  double get_stack_margin(
    const Document& document, const Stack& stack, const NodeId& item);

  /**
   * Arranges the visible items of a stack one after another from the start
   * of the first, each keeping its position across the stack, and fits the
   * stack's background around them.
   * @param document The document holding the stack.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The stack's id.
   * @param measurer The function measuring text.
   */
  void layout_stack(const Document& document, Transaction& transaction,
    const NodeId& id, const TextMeasurer& measurer);

  /**
   * Fits the background of a group with the padding role around the rest of
   * its content.
   * @param document The document holding the group.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The group's id.
   * @param measurer The function measuring text.
   */
  void layout_padding(const Document& document, Transaction& transaction,
    const NodeId& id, const TextMeasurer& measurer);

  /**
   * Places the cells of a repeat grid after its first in rows and columns of
   * its cell size and spacing.
   * @param document The document holding the grid.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The grid's id.
   */
  void layout_repeat_grid(
    const Document& document, Transaction& transaction, const NodeId& id);

  /**
   * Recomputes the stacks, padding groups and repeat grids whose content the
   * most recent revision changed, as Adobe XD lays them out while editing,
   * as part of that revision so that a single undo reverts the change along
   * with the new layouts. An edit within one cell of a repeat grid is first
   * repeated in every other cell, except for text content and image fills,
   * which each cell keeps. A stack item that was only moved takes the place
   * in the stack order its position calls for, and a stack keeps its start
   * as items are added, removed and resized. Inner layouts are recomputed
   * before the layouts containing them, and the layouts are left unchanged
   * if they can not be recomputed.
   * @param document The document changed.
   * @param changes The changes the most recent revision made.
   * @param options The settings to lay out with.
   * @return <code>true</code> iff a layout changed.
   */
  bool refresh_layouts(Document& document, const ChangeSet& changes,
    const LayoutOptions& options);
}

#endif

#ifndef RELLU_UI_LAYOUT_PROPERTIES_HPP
#define RELLU_UI_LAYOUT_PROPERTIES_HPP
#include <optional>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Model/Layout.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"

namespace Rellu {

  /**
   * Adds the content-aware layout of a group to Adobe XD's LAYOUT section:
   * layout.padding (bool) with layout.padding.top, layout.padding.right,
   * layout.padding.bottom and layout.padding.left (double) while it is on,
   * layout.stack (bool) with layout.stack.orientation (Choice of Horizontal
   * and Vertical) and layout.stack.spacing (double, mixed unless every item
   * has the same, edited in a step labelled All Spacings Value) while it is
   * on, and for a repeat grid
   * layout.repeat_grid with layout.repeat_grid.column_spacing and
   * layout.repeat_grid.row_spacing (double) instead.
   * @param node The node displayed.
   * @param section The layout section to add to.
   */
  void add_layout_properties(const Node& node, PropertyDescriptor& section);

  /**
   * Adds the spacings Adobe XD shows in the LAYOUT section for the items of
   * a stack, when the first node is an item of a stack of more than one
   * visible item: layout.spacing_before (double), the space before each
   * selected item that has one, labelled Spacing Before Value, and
   * layout.spacing_after (double), the space after each selected item that
   * has one, labelled Spacing After Value, each mixed unless those spaces
   * are the same and present only if there is such a space. Edited, they
   * set those spaces in one step labelled the same.
   * @param document The document holding the nodes.
   * @param nodes The nodes selected.
   * @param measurer The function measuring text.
   * @param section The layout section to add to.
   */
  void add_item_spacing_properties(const Document& document,
    const std::vector<NodeId>& nodes, const TextMeasurer& measurer,
    PropertyDescriptor& section);

  /**
   * Returns the direction of the stack a node is an item of, along which
   * Adobe XD disables the node's position field: X for a horizontal stack,
   * Y for a vertical one.
   * @param document The document holding the node.
   * @param id The node's id.
   * @return The stack's direction, or <code>std::nullopt</code> if the node
   *         is not an item of a stack.
   */
  std::optional<Orientation> get_stack_direction(
    const Document& document, const NodeId& id);

  /**
   * Adds the editors of the properties add_layout_properties and
   * add_item_spacing_properties describe, makes the width and height of
   * each repeat grid selected set the size it shows rather than scale it,
   * and leaves the position of stack items along their stack's direction to
   * the stack.
   * @param editors The editors to add to, holding the standard editors.
   * @param measurer The function measuring text.
   */
  void add_layout_property_editors(
    PropertyEditors& editors, TextMeasurer measurer);
}

#endif

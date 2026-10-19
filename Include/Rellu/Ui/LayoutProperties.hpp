#ifndef RELLU_UI_LAYOUT_PROPERTIES_HPP
#define RELLU_UI_LAYOUT_PROPERTIES_HPP
#include <optional>
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
   * has the same) while it is on, and for a repeat grid
   * layout.repeat_grid with layout.repeat_grid.column_spacing and
   * layout.repeat_grid.row_spacing (double) instead.
   * @param node The node displayed.
   * @param section The layout section to add to.
   */
  void add_layout_properties(const Node& node, PropertyDescriptor& section);

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
   * Adds the editors of the properties add_layout_properties describes,
   * makes the width and height of a repeat grid selected alone set the size
   * it shows rather than scale it, and leaves the position of stack items
   * along their stack's direction to the stack.
   * @param editors The editors to add to, holding the standard editors.
   * @param measurer The function measuring text.
   */
  void add_layout_property_editors(
    PropertyEditors& editors, TextMeasurer measurer);
}

#endif

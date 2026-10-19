#ifndef RELLU_UI_LAYOUT_PROPERTIES_HPP
#define RELLU_UI_LAYOUT_PROPERTIES_HPP
#include "Rellu/Commands/Bounds.hpp"
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
   * Adds the editors of the properties add_layout_properties describes, and
   * makes the width and height of a repeat grid selected alone set the size
   * it shows rather than scale it.
   * @param editors The editors to add to, holding the standard editors.
   * @param measurer The function measuring text.
   */
  void add_layout_property_editors(
    PropertyEditors& editors, TextMeasurer measurer);
}

#endif

#ifndef RELLU_MODEL_GROUP_HPP
#define RELLU_MODEL_GROUP_HPP
#include <map>
#include <optional>
#include <vector>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Component.hpp"
#include "Rellu/Model/Layout.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"

namespace Rellu {

  /** A shape whose region contributes to a mask. */
  struct MaskShape {

    /** The transform from the shape's space to the group's. */
    Matrix m_transform;

    /** The outline. */
    ShapeGeometry m_geometry;

    bool operator ==(const MaskShape&) const = default;
  };

  /** The group role clipping the children to the union of mask shapes. */
  struct Mask {

    /** The shapes whose union clips the children. */
    std::vector<MaskShape> m_shapes;

    bool operator ==(const Mask&) const = default;
  };

  /** The group role repeating a cell in rows and columns. */
  struct RepeatGrid {

    /** The size of each cell. */
    Size m_cell_size = Size(0, 0);

    /** The space between columns. */
    double m_column_spacing = 0;

    /** The space between rows. */
    double m_row_spacing = 0;

    /** The number of columns. */
    int m_column_count = 1;

    /** The number of rows. */
    int m_row_count = 1;

    /** The overall size of the grid. */
    Size m_size = Size(0, 0);

    bool operator ==(const RepeatGrid& grid) const;
  };

  /** Lists the directions a scroll group scrolls in. */
  enum class ScrollDirection : unsigned char {

    /** Along the x axis only. */
    HORIZONTAL,

    /** Along the y axis only. */
    VERTICAL,

    /** Along both axes. */
    PANNING
  };

  /** The group role showing its children through a scrolling viewport. */
  struct ScrollGroup {

    /** The directions the content scrolls in. */
    ScrollDirection m_direction = ScrollDirection::VERTICAL;

    /** The size of the viewport. */
    Size m_viewport = Size(0, 0);

    /** The offset of the viewport from the group's origin. */
    Point m_offset = Point(0, 0);

    bool operator ==(const ScrollGroup&) const = default;
  };

  /** The group role arranging its children in a row or column. */
  struct Stack {

    /** The direction children are arranged in. */
    Orientation m_orientation = Orientation::VERTICAL;

    /** The space after each child, by the child's id. */
    std::map<NodeId, double> m_spacing;

    /** The space between the content and the group's edges. */
    Margins m_padding;

    /** Whether the first child is a background spanning the others. */
    bool m_has_background = false;

    bool operator ==(const Stack& stack) const;
  };

  /** The group role keeping padding around its content. */
  struct Padding {

    /** The space between the content and the background's edges. */
    Margins m_padding;

    /** Whether the padding adapts as the content changes. */
    bool m_is_content_aware = false;

    /** Whether the first child is a background around the others. */
    bool m_has_background = false;

    bool operator ==(const Padding&) const = default;
  };

  /** The group role making the group an instance of a component. */
  struct ComponentInstance {

    /** The component instantiated. */
    ComponentId m_component;

    /** The active state, or nil for the default state. */
    StateId m_state;

    /** The size the instance is laid out at. */
    Size m_size = Size(0, 0);

    /** Maps each node of the instance to the master node it mirrors. */
    std::map<NodeId, NodeId> m_sync_map;

    /** How the instance differs from its master. */
    Overrides m_overrides;

    /** The instance's own copies of the component's states. */
    std::vector<ComponentState> m_states;

    bool operator ==(const ComponentInstance&) const = default;
  };

  /** The payload of a group node, which may play several roles at once. */
  struct Group {

    /** The mask role, if any. */
    std::optional<Mask> m_mask;

    /** The repeat grid role, if any. */
    std::optional<RepeatGrid> m_repeat_grid;

    /** The scroll group role, if any. */
    std::optional<ScrollGroup> m_scroll_group;

    /** The stack role, if any. */
    std::optional<Stack> m_stack;

    /** The padding role, if any. */
    std::optional<Padding> m_padding;

    /** The component instance role, if any. */
    std::optional<ComponentInstance> m_component_instance;

    bool operator ==(const Group&) const = default;
  };

  inline bool RepeatGrid::operator ==(const RepeatGrid& grid) const {
    return m_cell_size == grid.m_cell_size &&
      equals(m_column_spacing, grid.m_column_spacing) &&
      equals(m_row_spacing, grid.m_row_spacing) &&
      m_column_count == grid.m_column_count &&
      m_row_count == grid.m_row_count && m_size == grid.m_size;
  }

  inline bool Stack::operator ==(const Stack& stack) const {
    return m_orientation == stack.m_orientation &&
      equals(m_spacing, stack.m_spacing) && m_padding == stack.m_padding &&
      m_has_background == stack.m_has_background;
  }
}

#endif

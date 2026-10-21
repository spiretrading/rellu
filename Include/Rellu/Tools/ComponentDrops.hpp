#ifndef RELLU_TOOLS_COMPONENT_DROPS_HPP
#define RELLU_TOOLS_COMPONENT_DROPS_HPP
#include <memory>
#include <Qt>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/ToolContext.hpp"

namespace Rellu {

  /**
   * Finds the instance a component dropped on the canvas replaces, as Adobe
   * XD does: the outermost instance under the point within the edit context
   * with no locked or hidden node or repeat grid above it, unless it is an
   * instance of the component dropped.
   * @param context The context dropped in.
   * @param component The component dropped.
   * @param position The point in logical units.
   * @return The instance, or nil if there is none.
   */
  NodeId find_swap_target(const ToolContext& context,
    const ComponentId& component, Point position);

  /**
   * Makes the command dropping a component on the canvas as Adobe XD does:
   * replacing the instance find_swap_target finds, labelled Replace
   * Component, or else placing an instance with its top left at the point,
   * inside the topmost artboard holding the point, labelled Place
   * Component; either is labelled Place Graphic while Alt is held.
   * @param context The context dropped in.
   * @param component The component dropped.
   * @param position The point in logical units.
   * @param modifiers The modifier keys held.
   * @return The command, or <code>nullptr</code> if the document has no such
   *         component.
   */
  std::unique_ptr<Command> make_component_drop(const ToolContext& context,
    const ComponentId& component, Point position,
    Qt::KeyboardModifiers modifiers);
}

#endif

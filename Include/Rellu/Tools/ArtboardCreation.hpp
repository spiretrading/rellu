#ifndef RELLU_TOOLS_ARTBOARD_CREATION_HPP
#define RELLU_TOOLS_ARTBOARD_CREATION_HPP
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/ToolContext.hpp"

namespace Rellu {

  /**
   * Creates an artboard of the last used preset as a click of Adobe XD's
   * Artboard tool does, selecting it and bringing it into view.
   * @param context The context worked in.
   * @param point The point clicked in document space.
   * @return The artboard.
   */
  NodeId create_clicked_artboard(ToolContext& context, Point point);
}

#endif

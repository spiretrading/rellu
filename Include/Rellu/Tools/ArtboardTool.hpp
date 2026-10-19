#ifndef RELLU_TOOLS_ARTBOARD_TOOL_HPP
#define RELLU_TOOLS_ARTBOARD_TOOL_HPP
#include <memory>
#include "Rellu/Tools/DrawTool.hpp"

namespace Rellu {

  /**
   * Draws artboards on the pasteboard as Adobe XD's artboard tool does,
   * with a white background and named as XD names them, such as
   * "Artboard - 1" with an en dash and the lowest free number. Shift draws
   * a square and Alt draws from the center, and the nodes on the pasteboard
   * that the artboard overlaps at all once drawn, hidden and locked ones
   * included, move into it. A click on an artboard's title selects the
   * artboard, and a click elsewhere creates an artboard of the last used
   * preset.
   */
  class ArtboardTool : public DrawTool {
    public:

      /** Constructs an ArtboardTool. */
      ArtboardTool();

    protected:
      std::unique_ptr<Command> make_command(ToolContext& context,
        NodeHeader header, Point start, Point end,
        Qt::KeyboardModifiers modifiers) override;
      void click(ToolContext& context, Point point) override;
      void complete(ToolContext& context, const NodeId& id) override;
      NodeId find_parent(
        const ToolContext& context, Point point) const override;
  };
}

#endif

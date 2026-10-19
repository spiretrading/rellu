#ifndef RELLU_TOOLS_ARTBOARD_TOOL_HPP
#define RELLU_TOOLS_ARTBOARD_TOOL_HPP
#include <memory>
#include "Rellu/Tools/DrawTool.hpp"

namespace Rellu {

  /**
   * Draws artboards on the pasteboard as Adobe XD's artboard tool does,
   * with a white background and named as XD names them, such as
   * "Artboard - 1" with an en dash. Shift draws a square and Alt draws from
   * the center. A click selects the artboard under the pointer.
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
      NodeId find_parent(
        const ToolContext& context, Point point) const override;
  };
}

#endif

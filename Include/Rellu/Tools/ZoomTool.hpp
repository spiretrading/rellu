#ifndef RELLU_TOOLS_ZOOM_TOOL_HPP
#define RELLU_TOOLS_ZOOM_TOOL_HPP
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Zooms the view as Adobe XD's zoom tool does: a click zooms in about the
   * pointer, a click with Alt zooms out, and a drag zooms to the rectangle
   * dragged. The pointer is a magnifier showing whether a click zooms in or
   * out. Escape abandons a drag and otherwise returns to the select tool.
   */
  class ZoomTool : public Tool {
    public:

      /** The factor a click multiplies the zoom by. */
      static constexpr auto ZOOM_STEP = 2.0;

      /** The distance in logical units a press moves before it drags. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      /** Constructs a ZoomTool. */
      ZoomTool();

      void press(ToolContext& context, const PointerEvent& event) override;
      void move(ToolContext& context, const PointerEvent& event) override;
      void release(ToolContext& context, const PointerEvent& event) override;
      bool key_press(ToolContext& context, const KeyEvent& event) override;
      void cancel(ToolContext& context) override;
      bool is_busy() const override;
      ToolCursor get_cursor() const override;
      void draw(
        const ToolContext& context, CanvasOverlay& overlay) const override;

    private:
      bool m_is_pressed;
      bool m_is_dragging;
      bool m_is_zooming_out;
      Point m_press;
      Point m_position;
  };
}

#endif

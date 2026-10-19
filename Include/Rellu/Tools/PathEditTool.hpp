#ifndef RELLU_TOOLS_PATH_EDIT_TOOL_HPP
#define RELLU_TOOLS_PATH_EDIT_TOOL_HPP
#include <optional>
#include <vector>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/PathPoints.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Edits the points of the single selected shape as Adobe XD's path editing
   * mode does. A click selects a point, Shift toggles it, and a drag over
   * empty canvas selects the points within a marquee; dragging moves the
   * selected points, or a handle of one, which also turns the opposite
   * handle of a smooth point unless Alt is held. The point or handle
   * dragged snaps through the context's snap service. Clicking a segment adds a
   * point, a double click on a point toggles it between corner and smooth,
   * Delete removes the selected points and the arrow keys nudge them. The
   * first edit of a rectangle, ellipse, polygon or line converts it to a
   * path. Enter, Escape or a click away from the shape returns to the tool
   * named select, as does a change of the selection. Each edit is one
   * undoable step.
   */
  class PathEditTool : public Tool {
    public:

      /** The distance in logical units within which a point is hit. */
      static constexpr auto ANCHOR_REACH = 6.0;

      /** The distance in logical units within which a segment is hit. */
      static constexpr auto SEGMENT_REACH = 4.0;

      /** The distance in logical units a press moves before it drags. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      /** The distance in document units a nudge moves. */
      static constexpr auto NUDGE = 1.0;

      /** The distance in document units a nudge with Shift moves. */
      static constexpr auto LARGE_NUDGE = 10.0;

      /** Constructs a PathEditTool. */
      PathEditTool() noexcept;

      /**
       * Returns the node edited, or nil if the selection has none.
       * @param context The context worked in.
       */
      NodeId get_node(const ToolContext& context) const;

      /** Returns the selected points. */
      const std::vector<AnchorIndex>& get_selection() const;

      /**
       * Tests whether a key is one this tool uses, ahead of any shortcut
       * bound to it.
       * @param context The context worked in.
       * @param event The key event.
       */
      bool is_edit_key(const ToolContext& context, const KeyEvent& event) const;

      void press(ToolContext& context, const PointerEvent& event) override;
      void move(ToolContext& context, const PointerEvent& event) override;
      void release(ToolContext& context, const PointerEvent& event) override;
      void double_click(
        ToolContext& context, const PointerEvent& event) override;
      bool key_press(ToolContext& context, const KeyEvent& event) override;
      bool key_release(ToolContext& context, const KeyEvent& event) override;
      void cancel(ToolContext& context) override;
      bool is_busy() const override;
      void draw(
        const ToolContext& context, CanvasOverlay& overlay) const override;

    private:
      enum class Gesture : unsigned char {
        NONE,
        PENDING,
        MOVE,
        HANDLE,
        MARQUEE
      };
      NodeId m_node;
      std::vector<AnchorIndex> m_selection;
      Gesture m_gesture;
      Point m_press;
      Point m_press_document;
      std::optional<EditablePath> m_base;
      std::vector<AnchorIndex> m_base_selection;
      std::optional<HandleIndex> m_handle;
      std::optional<AnchorIndex> m_anchor;
      bool m_is_smooth;
      bool m_is_applied;
      bool m_is_marquee_pending;
      Rect m_marquee;
      int m_nudge_key;

      std::optional<EditablePath> load(ToolContext& context);
      std::optional<EditablePath> load(const ToolContext& context) const;
      void exit(ToolContext& context);
      void move_points(ToolContext& context, Point delta);
      void update_handle(ToolContext& context, const PointerEvent& event);
      void update_marquee(ToolContext& context, const PointerEvent& event);
      void click_away(ToolContext& context);
      void remove_selection(ToolContext& context);
      void finish(ToolContext& context);
  };
}

#endif

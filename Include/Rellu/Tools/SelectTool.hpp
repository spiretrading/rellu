#ifndef RELLU_TOOLS_SELECT_TOOL_HPP
#define RELLU_TOOLS_SELECT_TOOL_HPP
#include <cstdint>
#include <optional>
#include <vector>
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/ArtboardGuides.hpp"
#include "Rellu/Tools/LayoutHandles.hpp"
#include "Rellu/Tools/SelectionBox.hpp"
#include "Rellu/Tools/SelectionHandles.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Selects, moves, resizes and rotates nodes as Adobe XD's select tool
   * does. A click selects what lies under the pointer at the level of the
   * edit context, a double click enters a group, hands the text clicked
   * to the tool named text for editing or hands the shape clicked to the
   * tool named path_edit, as Enter does for a single selected shape, Ctrl
   * selects the deepest node, Shift adds a node, or removes a selected one
   * when released without a drag, a drag with Alt held moves a
   * copy of the selection, and a drag over empty canvas
   * draws a marquee. A press on a node that is selected, or whose part under
   * the pointer is, keeps the selection, so a drag moves all of it. While
   * several nodes including an artboard are selected, a press inside that
   * artboard also keeps the selection, and when only artboards are
   * selected, a release without a drag then selects the node clicked. The
   * selection lists nodes in the order they were
   * clicked or reached by the marquee. The pointer hovering an unlocked
   * node outlines it, a shape by its own outline and anything else by its
   * box. The selection
   * is drawn and edited through SelectionHandles. Arrow
   * keys nudge the selection, except stack items along their stack's
   * direction. Locked nodes are selected by a click or a marquee but not
   * moved. The part of a node lying outside its
   * artboard is clicked or hovered only while it, or another node of its
   * artboard, is selected, or while it is a child of the container entered.
   * A repeat grid or stack selected
   * alone is also edited through LayoutHandles, and a double click enters
   * both a repeat grid and the cell clicked. Stack items dragged make room
   * for each other until dropped. Artboard guides are created, moved and
   * deleted through ArtboardGuides. Each drag is one undoable step.
   */
  class SelectTool : public Tool {
    public:

      /** The distance in logical units a press moves before it drags. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      /** The distance in logical units within which an outline is hit. */
      static constexpr auto HIT_TOLERANCE = 3.0;

      /** The distance in document units a nudge moves. */
      static constexpr auto NUDGE = 1.0;

      /** The distance in document units a nudge with Shift moves. */
      static constexpr auto LARGE_NUDGE = 10.0;

      /** Constructs a SelectTool. */
      SelectTool();

      void press(ToolContext& context, const PointerEvent& event) override;
      void move(ToolContext& context, const PointerEvent& event) override;
      void release(ToolContext& context, const PointerEvent& event) override;
      void double_click(
        ToolContext& context, const PointerEvent& event) override;
      void leave(ToolContext& context) override;
      bool key_press(ToolContext& context, const KeyEvent& event) override;
      bool key_release(ToolContext& context, const KeyEvent& event) override;
      void cancel(ToolContext& context) override;
      bool is_busy() const override;
      ToolCursor get_cursor() const override;
      void draw(
        const ToolContext& context, CanvasOverlay& overlay) const override;

    private:
      enum class Gesture : unsigned char {
        NONE,
        PENDING,
        MARQUEE,
        MOVE,
        HANDLE,
        LAYOUT,
        GUIDE
      };
      struct CachedBox {
        NodeId m_id;
        std::optional<std::uint64_t> m_revision;
        std::optional<SelectionBox> m_box;
        std::optional<Path> m_outline;
      };
      Gesture m_gesture;
      Point m_press;
      Point m_press_document;
      NodeId m_clicked;
      NodeId m_removed;
      bool m_is_marquee_pending;
      std::vector<NodeId> m_nodes;
      SelectionBox m_box;
      Point m_delta;
      bool m_is_applied;
      bool m_is_copied;
      NodeId m_scope;
      std::vector<NodeId> m_base;
      Rect m_marquee;
      int m_nudge_key;
      ToolCursor m_cursor;
      SelectionHandles m_handles;
      LayoutHandles m_layout_handles;
      ArtboardGuides m_guides;
      mutable CachedBox m_hover_box;
      mutable CachedBox m_container_box;

      const std::optional<SelectionBox>& get_box(const ToolContext& context,
        const NodeId& id, CachedBox& cache) const;
      NodeId resolve_target(ToolContext& context, const NodeId& hit,
        Qt::KeyboardModifiers modifiers, bool is_entering) const;
      void begin_drag(ToolContext& context, const PointerEvent& event);
      void copy(ToolContext& context);
      void update_move(ToolContext& context, const PointerEvent& event);
      void update_marquee(ToolContext& context, const PointerEvent& event);
      void update_hover(ToolContext& context, const PointerEvent& event);
      void finish(ToolContext& context);
      void draw_hover(const ToolContext& context, const NodeId& hover,
        CanvasOverlay& overlay) const;
  };
}

#endif

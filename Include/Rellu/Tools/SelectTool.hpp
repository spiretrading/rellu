#ifndef RELLU_TOOLS_SELECT_TOOL_HPP
#define RELLU_TOOLS_SELECT_TOOL_HPP
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/LayoutHandles.hpp"
#include "Rellu/Tools/SelectionBox.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Selects, moves, resizes and rotates nodes as Adobe XD's select tool
   * does. A click selects what lies under the pointer at the level of the
   * edit context, a double click enters a group, hands the text clicked
   * to the tool named text for editing or hands the shape clicked to the
   * tool named path_edit, as Enter does for a single selected shape, Ctrl
   * selects the deepest node, Shift toggles, and a drag over empty canvas
   * draws a marquee. The
   * selection's box resizes by its handles and rotates from just outside
   * its corners. A single selected rectangle also shows a corner radius
   * handle inside each corner, which rounds every corner alike, or only
   * its own corner while Alt is held. A repeat grid or stack selected
   * alone is also edited through LayoutHandles, a repeat grid without the
   * resize handles, and a double click enters both a repeat grid and the
   * cell clicked. Stack items dragged make room for each other until
   * dropped. Each drag is one undoable step.
   */
  class SelectTool : public Tool {
    public:

      /** The radius in logical units of a resize handle. */
      static constexpr auto HANDLE_RADIUS = 4.0;

      /** The distance in logical units within which a handle is hit. */
      static constexpr auto HANDLE_REACH = 6.0;

      /**
       * The distance in logical units outside a corner within which a drag
       * rotates.
       */
      static constexpr auto ROTATION_REACH = 20.0;

      /** The distance in logical units a press moves before it drags. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      /** The distance in logical units within which an outline is hit. */
      static constexpr auto HIT_TOLERANCE = 3.0;

      /** The angle in radians rotations snap to while Shift is held. */
      static constexpr auto ROTATION_STEP = 0.2617993877991494;

      /** The distance in document units a nudge moves. */
      static constexpr auto NUDGE = 1.0;

      /** The distance in document units a nudge with Shift moves. */
      static constexpr auto LARGE_NUDGE = 10.0;

      /** The radius in logical units of a corner radius handle. */
      static constexpr auto RADIUS_HANDLE_RADIUS = 3.0;

      /**
       * The smallest distance in logical units between a corner radius
       * handle and the sides meeting at its corner.
       */
      static constexpr auto RADIUS_HANDLE_INSET = 14.0;

      /**
       * The length in logical units both sides of a rectangle must reach
       * for its corner radius handles to show.
       */
      static constexpr auto RADIUS_HANDLE_SIDE = 40.0;

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
        RESIZE,
        ROTATE,
        RADIUS,
        LAYOUT
      };
      enum class Target : unsigned char {
        NONE,
        HANDLE,
        ROTATION
      };
      struct Grab {
        Target m_target;
        Handle m_handle;
      };
      struct RadiusHandles {
        NodeId m_id;
        Rect m_rect;
        Matrix m_transform;
        CornerRadii m_radii;
        double m_limit;
        std::array<Point, 4> m_points;
      };
      struct CachedBox {
        NodeId m_id;
        std::optional<std::uint64_t> m_revision;
        std::optional<SelectionBox> m_box;
      };
      Gesture m_gesture;
      Point m_press;
      Point m_press_document;
      NodeId m_clicked;
      bool m_is_reselecting;
      bool m_is_marquee_pending;
      std::vector<NodeId> m_nodes;
      SelectionBox m_box;
      Handle m_handle;
      Point m_grab_offset;
      Point m_delta;
      Rect m_resized;
      double m_angle;
      double m_raw_angle;
      bool m_is_applied;
      NodeId m_scope;
      std::vector<NodeId> m_base;
      Rect m_marquee;
      int m_nudge_key;
      std::optional<RadiusHandles> m_radius_handles;
      std::size_t m_corner;
      double m_radius;
      double m_inset;
      ToolCursor m_cursor;
      LayoutHandles m_layout_handles;
      mutable std::optional<std::uint64_t> m_revision;
      mutable std::vector<NodeId> m_cached_nodes;
      mutable std::optional<SelectionBox> m_cached_box;
      mutable std::vector<SelectionBox> m_cached_outlines;
      mutable CachedBox m_hover_box;
      mutable CachedBox m_container_box;

      void update_cache(const ToolContext& context) const;
      std::optional<SelectionBox> get_box(const ToolContext& context) const;
      const std::optional<SelectionBox>& get_box(const ToolContext& context,
        const NodeId& id, CachedBox& cache) const;
      std::vector<NodeId> get_movable(const ToolContext& context) const;
      bool is_rotatable(const ToolContext& context) const;
      std::optional<Grab> find_grab(
        const ToolContext& context, Point position) const;
      std::optional<RadiusHandles> get_radius_handles(
        const ToolContext& context) const;
      std::optional<std::size_t> find_radius_handle(
        const ToolContext& context, Point position) const;
      double get_inset(const ToolContext& context, Point position) const;
      NodeId resolve_target(ToolContext& context, const NodeId& hit,
        Qt::KeyboardModifiers modifiers, bool is_entering) const;
      void begin_drag(ToolContext& context, const PointerEvent& event);
      void update_move(ToolContext& context, const PointerEvent& event);
      void update_resize(ToolContext& context, const PointerEvent& event);
      void update_rotate(ToolContext& context, const PointerEvent& event);
      void update_radius(ToolContext& context, const PointerEvent& event);
      void update_marquee(ToolContext& context, const PointerEvent& event);
      void update_hover(ToolContext& context, const PointerEvent& event);
      void finish(ToolContext& context);
      std::optional<SelectionBox> get_gesture_box() const;
      void draw_box(const ToolContext& context, const SelectionBox& box,
        CanvasOverlay& overlay, bool has_handles) const;
  };
}

#endif

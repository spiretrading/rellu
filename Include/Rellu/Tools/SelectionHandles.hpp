#ifndef RELLU_TOOLS_SELECTION_HANDLES_HPP
#define RELLU_TOOLS_SELECTION_HANDLES_HPP
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <QImage>
#include "Rellu/Commands/ResponsiveResize.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/SelectionBox.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Draws the selection and edits it through its handles as Adobe XD's
   * selection decorations do, whichever tool is active. The selection's
   * box resizes by the handles get_handle_layout shows, flipping what it
   * holds when dragged past the opposite side, and rotates from the rotate
   * handles beside them, a repeat grid as Rotate Repeat Grid. A single
   * line shows a handle at each end instead, dragged to move that end,
   * along multiples of 45 degrees while Shift is held and about the line's
   * center while Alt is held. A single rectangle also shows a corner radius
   * handle inside each corner, which rounds every corner alike, or only its
   * own corner while Alt is held. Dragging the top or bottom handle of auto
   * height text first makes it fixed size. Several nodes selected are
   * outlined each, and locked nodes are framed in grey, with small grey
   * handles that do nothing and a lock badge at the top left corner. Text
   * in a fixed size frame shows the frame's bottom edge dashed. Each drag
   * is one undoable step.
   */
  class SelectionHandles {
    public:

      /** The angle in radians rotations snap to while Shift is held. */
      static constexpr auto ROTATION_STEP = 0.2617993877991494;

      /** The radius in logical units of a corner radius handle. */
      static constexpr auto RADIUS_HANDLE_RADIUS = 4.0;

      /** The radius in logical units of a handle of a locked node. */
      static constexpr auto LOCKED_HANDLE_RADIUS = 2.5;

      /** The width and height in logical units of the lock badge. */
      static constexpr auto LOCK_BADGE_SIZE = 12.0;

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

      /** The radius in logical units rounding a scroll group's handles. */
      static constexpr auto SCROLL_HANDLE_RADIUS = 3.0;

      /** Constructs a SelectionHandles without a drag in progress. */
      SelectionHandles();

      /**
       * Returns the box framing the selection.
       * @param context The context worked in.
       */
      std::optional<SelectionBox> get_box(const ToolContext& context) const;

      /**
       * Returns the pointer to show at a position, or
       * <code>std::nullopt</code> if no handle is there.
       * @param context The context worked in.
       * @param position The position in logical units.
       */
      std::optional<ToolCursor> hover(
        const ToolContext& context, Point position) const;

      /**
       * Starts dragging the handle under a press.
       * @param context The context worked in.
       * @param event The press.
       * @return <code>true</code> iff a drag started.
       */
      bool press(ToolContext& context, const PointerEvent& event);

      /**
       * Continues the drag.
       * @param context The context worked in.
       * @param event The pointer move.
       */
      void move(ToolContext& context, const PointerEvent& event);

      /**
       * Ends the drag.
       * @param context The context worked in.
       */
      void release(ToolContext& context);

      /**
       * Abandons the drag, reverting what it changed.
       * @param context The context worked in.
       */
      void cancel(ToolContext& context);

      /** Returns <code>true</code> iff a drag is in progress. */
      bool is_active() const;

      /**
       * Returns <code>true</code> iff a drag resizing, rotating or moving
       * the end of a line is in progress.
       */
      bool is_transforming() const;

      /** Returns the pointer to show during the drag. */
      ToolCursor get_cursor() const;

      /**
       * Draws the selection with its handles.
       * @param context The context worked in.
       * @param overlay The overlay to draw into.
       * @param has_radius_handles Whether the corner radius handles show.
       */
      void draw(const ToolContext& context, CanvasOverlay& overlay,
        bool has_radius_handles) const;

      /**
       * Draws the outline of a box framing the selection, without handles.
       * @param context The context worked in.
       * @param box The box.
       * @param overlay The overlay to draw into.
       */
      void draw_outline(const ToolContext& context, const SelectionBox& box,
        CanvasOverlay& overlay) const;

    private:
      enum class Gesture : unsigned char {
        NONE,
        RESIZE,
        ROTATE,
        RADIUS,
        ENDPOINT
      };
      struct RadiusHandles {
        NodeId m_id;
        Rect m_rect;
        Matrix m_transform;
        CornerRadii m_radii;
        double m_limit;
        std::array<Point, 4> m_points;
      };
      struct LineHandles {
        NodeId m_id;
        std::array<Point, 2> m_points;
      };
      Gesture m_gesture;
      Point m_press_document;
      std::vector<NodeId> m_nodes;
      SelectionBox m_box;
      Handle m_handle;
      Point m_grab_offset;
      Rect m_resized;
      ResizeRule m_rule;
      double m_angle;
      double m_raw_angle;
      bool m_is_applied;
      std::optional<RadiusHandles> m_radius_handles;
      std::size_t m_corner;
      double m_radius;
      double m_inset;
      NodeId m_line;
      Matrix m_parent_transform;
      std::size_t m_endpoint;
      Point m_fixed;
      Point m_dragged;
      bool m_is_coordinate_snapped;
      std::optional<Handle> m_scroll_handle;
      ToolCursor m_cursor;
      mutable std::optional<std::uint64_t> m_revision;
      mutable std::vector<NodeId> m_cached_nodes;
      mutable std::optional<SelectionBox> m_cached_box;
      mutable std::vector<SelectionBox> m_cached_outlines;
      mutable double m_badge_scale;
      mutable std::shared_ptr<const QImage> m_badge;

      void update_cache(const ToolContext& context) const;
      bool is_locked_selection(const ToolContext& context) const;
      HandleLayout get_layout(
        const ToolContext& context, const SelectionBox& box) const;
      std::optional<HandleHit> find_grab(
        const ToolContext& context, Point position) const;
      std::optional<RadiusHandles> get_radius_handles(
        const ToolContext& context) const;
      std::optional<std::size_t> find_radius_handle(
        const ToolContext& context, Point position) const;
      double get_inset(const ToolContext& context, Point position) const;
      std::optional<LineHandles> get_line_handles(
        const ToolContext& context) const;
      std::optional<std::size_t> find_endpoint(
        const ToolContext& context, Point position) const;
      void begin_endpoint(ToolContext& context, const LineHandles& handles,
        std::size_t endpoint);
      void update_resize(ToolContext& context, const PointerEvent& event);
      void update_rotate(ToolContext& context, const PointerEvent& event);
      void update_radius(ToolContext& context, const PointerEvent& event);
      void update_endpoint(ToolContext& context, const PointerEvent& event);
      void finish(ToolContext& context);
      void draw_box(const ToolContext& context, const SelectionBox& box,
        CanvasOverlay& overlay, bool has_handles) const;
      void draw_locked(const ToolContext& context, const SelectionBox& box,
        CanvasOverlay& overlay) const;
  };

  /**
   * Returns the top level nodes of the selection that are not locked, nor
   * inside a locked node.
   * @param context The context worked in.
   */
  std::vector<NodeId> get_movable_nodes(const ToolContext& context);

  /**
   * Returns <code>true</code> iff a node or one of its containers is locked.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_locked_node(const Document& document, const NodeId& id);

  /**
   * Adds the outline of a quadrilateral to an overlay.
   * @param overlay The overlay to draw into.
   * @param corners The corners in logical units, clockwise.
   * @param color The color.
   * @param is_bottom_dashed Whether the side from the third corner to the
   *        fourth is dashed.
   */
  void add_box_outline(CanvasOverlay& overlay,
    const std::array<Point, 4>& corners, Color color,
    bool is_bottom_dashed = false);
}

#endif

#ifndef RELLU_TOOLS_PROTOTYPE_TOOL_HPP
#define RELLU_TOOLS_PROTOTYPE_TOOL_HPP
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <QImage>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /** A wire the PrototypeTool draws from a node to a destination. */
  struct WireShape {

    /** The node whose interactions the wire shows. */
    NodeId m_owner;

    /** The artboard or layer the wire leads to, or nil for none. */
    NodeId m_destination;

    /** The interactions the wire shows. */
    std::vector<InteractionId> m_interactions;

    /** Whether the wire shows the interaction being edited. */
    bool m_is_selected = false;

    /** The start of the wire in logical units. */
    Point m_start = Point(0, 0);

    /** The middle of the wire in logical units. */
    Point m_middle = Point(0, 0);

    /** The end of the wire in logical units. */
    Point m_end = Point(0, 0);

    /** The center of the knob on the wire in logical units, if any. */
    std::optional<Point> m_knob;
  };

  /**
   * Selects nodes and wires them to artboards as Adobe XD's Prototype mode
   * does. A click selects what lies under the pointer at the level of the
   * edit context, an artboard's title selects the artboard, a double click
   * enters a group and Shift toggles. A selected node shows a blue wire
   * handle on the middle of its right edge, with a plus once it has
   * interactions; clicking the handle starts a pending interaction, kept
   * out of the document until it has a destination, and dragging it onto
   * another artboard, a layer of the
   * node's own artboard or an artboard's title draws a wire there, adding
   * the interaction Adobe XD adds, which also starts a flow on the node's
   * artboard when the document has none. Dragging the end of an edited
   * wire leads its interactions to the destination it is dropped on, or
   * deletes them when dropped on none, the destination outlined while
   * dragging and Escape abandoning the drag. The wires of the interactions
   * in the
   * selection's artboards, and of those leading to a selected artboard, are
   * drawn as Adobe XD draws them, one per node and destination: the wires
   * of the interactions being edited thick and blue, those of selected
   * nodes light blue and the others grey, with a knob counting the
   * interactions of a wire holding several, and the selection is tinted
   * while no interaction is edited. Hovering a wire shows a knob under the
   * pointer and a tooltip of its triggers; clicking it selects its node and
   * edits the first of its interactions by trigger, and Shift adds or
   * removes the wire from those edited. The artboards of the selection and
   * those starting a flow show a home marker beside their top left corner,
   * blue on the start of a flow and darker under the pointer, sized by the
   * zoom, which a click turns on or off; each flow is named by a label
   * above its artboard, blue while its artboards are selected by marking
   * it or clicking the label. The Scroll To being edited shows a line
   * across its node's artboard at its layer's top moved by its offset, the
   * offset shaded, and a handle on the artboard's left edge dragging the
   * offset. Each edit is one undoable step.
   */
  class PrototypeTool : public Tool {
    public:

      /** The radius in logical units of the wire handle. */
      static constexpr auto HANDLE_RADIUS = 10.0;

      /** The size in logical units of a home marker at a large zoom. */
      static constexpr auto LARGE_MARKER_SIZE = Size(20, 104.0 / 3);

      /** The size in logical units of a home marker at a medium zoom. */
      static constexpr auto MEDIUM_MARKER_SIZE = Size(16, 28);

      /** The size in logical units of a home marker at a small zoom. */
      static constexpr auto SMALL_MARKER_SIZE = Size(10, 52.0 / 3);

      /** The zoom from which a home marker is large. */
      static constexpr auto LARGE_MARKER_ZOOM = 0.75;

      /** The zoom from which a home marker is medium, below which small. */
      static constexpr auto MEDIUM_MARKER_ZOOM = 0.33;

      /** The distance in logical units within which an outline is hit. */
      static constexpr auto HIT_TOLERANCE = 3.0;

      /** The distance in logical units within which a wire is hit. */
      static constexpr auto WIRE_SLOP = 12.0;

      /** The distance in logical units the pointer moves to drag a wire. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      /** The size in logical units of a Scroll To's offset handle. */
      static constexpr auto OFFSET_HANDLE_SIZE = Size(20, 14);

      /** The size in logical units of the offset handle zoomed out. */
      static constexpr auto SMALL_OFFSET_HANDLE_SIZE = Size(14, 8);

      /** The zoom below which the offset handle is small. */
      static constexpr auto SMALL_OFFSET_HANDLE_ZOOM = 0.33;

      /** Constructs a PrototypeTool. */
      PrototypeTool();

      ~PrototypeTool() override;

      /**
       * Returns the position in logical units of the wire handle of the
       * selection, or <code>std::nullopt</code> if it shows none.
       * @param context The context worked in.
       */
      std::optional<Point> get_handle(const ToolContext& context) const;

      /**
       * Returns the region in logical units of an artboard's home marker.
       * @param context The context worked in.
       * @param artboard The artboard.
       */
      std::optional<Rect> get_marker(
        const ToolContext& context, const NodeId& artboard) const;

      /**
       * Returns the region in logical units of the handle dragging the
       * offset of the Scroll To being edited, or <code>std::nullopt</code>
       * if it shows none.
       * @param context The context worked in.
       */
      std::optional<Rect> get_offset_handle(const ToolContext& context) const;

      /**
       * Returns the region in logical units of the label naming the flow
       * starting on an artboard, or <code>std::nullopt</code> if none does.
       * @param context The context worked in.
       * @param artboard The artboard.
       */
      std::optional<Rect> get_flow_label(
        const ToolContext& context, const NodeId& artboard) const;

      /**
       * Finds the artboard a wire dropped at a point connects to.
       * @param context The context worked in.
       * @param position The point in logical units.
       * @return The topmost visible artboard containing the point, or nil.
       */
      NodeId find_destination(
        const ToolContext& context, Point position) const;

      /**
       * Finds where the wire being dragged connects when dropped at a
       * point, as Adobe XD does: a layer directly within the trigger's own
       * artboard, another artboard, or the artboard of a title.
       * @param context The context worked in.
       * @param position The point in logical units.
       * @return The artboard or layer, or nil if there is none.
       */
      NodeId find_wire_target(
        const ToolContext& context, Point position) const;

      /**
       * Returns the wires drawn, from bottom to top.
       * @param context The context worked in.
       */
      std::vector<WireShape> get_wires(const ToolContext& context) const;

      /**
       * Returns the center in logical units of the knob shown on the wire
       * under the pointer, or <code>std::nullopt</code> if none is shown.
       */
      std::optional<Point> get_hover_knob() const;

      /**
       * Returns the tooltip shown beside the pointer over a wire, empty if
       * none is shown.
       */
      std::string get_tool_tip() const;

      void press(ToolContext& context, const PointerEvent& event) override;
      void move(ToolContext& context, const PointerEvent& event) override;
      void release(ToolContext& context, const PointerEvent& event) override;
      void double_click(
        ToolContext& context, const PointerEvent& event) override;
      void leave(ToolContext& context) override;
      bool key_press(ToolContext& context, const KeyEvent& event) override;
      void cancel(ToolContext& context) override;
      bool is_busy() const override;
      ToolCursor get_cursor() const override;
      void draw(
        const ToolContext& context, CanvasOverlay& overlay) const override;

    private:
      NodeId m_source;
      std::vector<InteractionId> m_dragged;
      Point m_press;
      bool m_is_dragged;
      std::optional<Point> m_wire_end;
      NodeId m_target;
      InteractionId m_offset_interaction;
      double m_initial_offset;
      double m_offset_start;
      bool m_is_offset_applied;
      bool m_is_over_offset;
      NodeId m_hover_owner;
      NodeId m_hover_destination;
      Point m_hover_point;
      Point m_pointer;
      bool m_is_wire_hovered;
      bool m_is_knob_shown;
      bool m_is_wire_pressed;
      std::string m_tool_tip;
      NodeId m_hover_marker;
      NodeId m_focused_flow;
      std::vector<NodeId> m_flow_selection;
      mutable std::unordered_map<std::string, std::shared_ptr<const QImage>>
        m_images;

      NodeId get_wired(const ToolContext& context) const;
      void draw_drag(
        const ToolContext& context, CanvasOverlay& overlay) const;
      bool is_near_handle(const ToolContext& context, Point position) const;
      std::vector<NodeId> get_marked_artboards(
        const ToolContext& context) const;
      NodeId find_marker(const ToolContext& context, Point position) const;
      bool is_focused_flow(
        const ToolContext& context, const NodeId& artboard) const;
      void focus_flow(ToolContext& context, const NodeId& artboard);
      const Interaction* find_edited(
        const ToolContext& context, const InteractionId& id) const;
      void select_trigger(ToolContext& context, const NodeId& owner);
      void select_wire(ToolContext& context,
        const std::vector<InteractionId>& interactions, bool is_shift);
      void create_wire(ToolContext& context, bool is_shift);
      void connect(
        ToolContext& context, const NodeId& destination, bool is_shift);
      void rewire(
        ToolContext& context, const NodeId& destination, bool is_shift);
      std::shared_ptr<const QImage> get_marker_image(
        Size size, bool is_flow, bool is_hovered, double scale) const;
      std::shared_ptr<const QImage> get_label_image(
        const std::string& name, bool is_focused, double scale) const;
      std::shared_ptr<const QImage> get_offset_handle_image(
        bool is_small, double scale) const;
      std::shared_ptr<const QImage> get_offset_label_image(
        const QString& text, double scale) const;
  };
}

#endif

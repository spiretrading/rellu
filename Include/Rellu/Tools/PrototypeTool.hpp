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
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Selects nodes and wires them to artboards as Adobe XD's Prototype mode
   * does. A click selects what lies under the pointer at the level of the
   * edit context, an artboard's title selects the artboard, a double click
   * enters a group and Shift toggles. A selected node shows a blue wire
   * handle on the middle of its right edge and the wires of its
   * interactions to their destinations; dragging the handle onto an
   * artboard wires the node to it, rewiring its tap transition if it has
   * one and otherwise adding XD's default interaction, which also starts a
   * flow on the node's artboard when the document has none. Each artboard
   * shows a home marker at its top left, blue on the start of a flow, which
   * a click turns on or off. Each edit is one undoable step.
   */
  class PrototypeTool : public Tool {
    public:

      /** The radius in logical units of the wire handle. */
      static constexpr auto HANDLE_RADIUS = 8.0;

      /** The width in logical units of a home marker. */
      static constexpr auto MARKER_WIDTH = 16.0;

      /** The height in logical units of a home marker. */
      static constexpr auto MARKER_HEIGHT = 24.0;

      /** The distance in logical units within which an outline is hit. */
      static constexpr auto HIT_TOLERANCE = 3.0;

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
       * Finds the artboard a wire dropped at a point connects to.
       * @param context The context worked in.
       * @param position The point in logical units.
       * @return The topmost visible artboard containing the point, or nil.
       */
      NodeId find_destination(
        const ToolContext& context, Point position) const;

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
      std::optional<Point> m_wire_end;
      NodeId m_target;
      mutable std::unordered_map<std::string, std::shared_ptr<const QImage>>
        m_images;

      NodeId get_wired(const ToolContext& context) const;
      NodeId get_marked_artboard(const ToolContext& context) const;
      void connect(ToolContext& context, const NodeId& destination);
      std::shared_ptr<const QImage> get_marker_image(
        bool is_flow, double scale) const;
      std::shared_ptr<const QImage> get_label_image(
        const std::string& name, double scale) const;
  };
}

#endif

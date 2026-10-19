#ifndef RELLU_TOOLS_SNAP_SERVICE_HPP
#define RELLU_TOOLS_SNAP_SERVICE_HPP
#include <optional>
#include <vector>
#include <Qt>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"

namespace Rellu {

  /** Lists the coordinates of a point that a snap may adjust. */
  enum class SnapAxes : unsigned char {

    /** Neither coordinate. */
    NONE,

    /** The x coordinate, as when dragging a left or right edge. */
    HORIZONTAL,

    /** The y coordinate, as when dragging a top or bottom edge. */
    VERTICAL,

    /** Both coordinates. */
    BOTH
  };

  /**
   * Adjusts what tools drag so that it lines up with the rest of the
   * document, as smart guides do, and shows the distances between nodes.
   * The base class snaps and shows nothing.
   */
  class SnapService {
    public:
      virtual ~SnapService() = default;

      /**
       * Begins a gesture dragging nodes or drawing one.
       * @param container The container whose children and frame are
       *        snapped to, or nil for the canvas.
       * @param nodes The nodes dragged, which are not snapped to.
       */
      virtual void begin(
        const NodeId& container, const std::vector<NodeId>& nodes);

      /**
       * Adjusts the displacement of a selection being moved.
       * @param bounds The selection's bounds in document space before the
       *        move.
       * @param delta The displacement proposed, in document units.
       * @param modifiers The modifier keys held.
       * @return The displacement to apply.
       */
      virtual Point snap_move(
        const Rect& bounds, Point delta, Qt::KeyboardModifiers modifiers);

      /**
       * Adjusts a point that a resize drags an edge or corner to, or that a
       * drawing tool draws from or to.
       * @param point The point proposed, in document space.
       * @param axes The coordinates that may be adjusted.
       * @param modifiers The modifier keys held.
       * @return The point to use.
       */
      virtual Point snap_point(
        Point point, SnapAxes axes, Qt::KeyboardModifiers modifiers);

      /** Ends the gesture begun most recently. */
      virtual void end();

      /**
       * Shows the distances between the selection and a node, as while Alt
       * is held over the node.
       * @param selection The selection's bounds in document space, or
       *        <code>std::nullopt</code> to stop showing distances.
       * @param target The bounds of the node in document space.
       */
      virtual void measure(
        const std::optional<Rect>& selection, const Rect& target);

      /**
       * Draws the guides showing what the most recent adjustment lined up
       * with, and the distances measured.
       * @param view The transform from document space to the overlay's
       *        logical units.
       * @param overlay The overlay to draw into.
       */
      virtual void draw(const Matrix& view, CanvasOverlay& overlay) const;
  };

  inline void SnapService::begin(
    const NodeId& container, const std::vector<NodeId>& nodes) {}

  inline Point SnapService::snap_move(
      const Rect& bounds, Point delta, Qt::KeyboardModifiers modifiers) {
    return delta;
  }

  inline Point SnapService::snap_point(
      Point point, SnapAxes axes, Qt::KeyboardModifiers modifiers) {
    return point;
  }

  inline void SnapService::end() {}

  inline void SnapService::measure(
    const std::optional<Rect>& selection, const Rect& target) {}

  inline void SnapService::draw(
    const Matrix& view, CanvasOverlay& overlay) const {}
}

#endif

#ifndef RELLU_TOOLS_SNAP_SERVICE_HPP
#define RELLU_TOOLS_SNAP_SERVICE_HPP
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"

namespace Rellu {

  /**
   * Adjusts what tools drag so that it lines up with the rest of the
   * document, as smart guides do. The base class snaps nothing.
   */
  class SnapService {
    public:
      virtual ~SnapService() = default;

      /**
       * Begins a gesture dragging nodes.
       * @param nodes The nodes dragged, which are not snapped to.
       */
      virtual void begin(const std::vector<NodeId>& nodes);

      /**
       * Adjusts the displacement of a selection being moved.
       * @param bounds The selection's bounds in document space before the
       *        move.
       * @param delta The displacement proposed, in document units.
       * @return The displacement to apply.
       */
      virtual Point snap_move(const Rect& bounds, Point delta);

      /**
       * Adjusts the point in document space that a resize drags an edge or
       * corner of the selection to.
       * @param point The point proposed.
       * @return The point to drag to.
       */
      virtual Point snap_resize(Point point);

      /** Ends the gesture begun most recently. */
      virtual void end();

      /**
       * Draws the guides showing what the most recent adjustment lined up
       * with.
       * @param view The transform from document space to the overlay's
       *        logical units.
       * @param overlay The overlay to draw into.
       */
      virtual void draw(const Matrix& view, CanvasOverlay& overlay) const;
  };

  inline void SnapService::begin(const std::vector<NodeId>& nodes) {}

  inline Point SnapService::snap_move(const Rect& bounds, Point delta) {
    return delta;
  }

  inline Point SnapService::snap_resize(Point point) {
    return point;
  }

  inline void SnapService::end() {}

  inline void SnapService::draw(
    const Matrix& view, CanvasOverlay& overlay) const {}
}

#endif

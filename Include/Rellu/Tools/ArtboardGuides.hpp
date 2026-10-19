#ifndef RELLU_TOOLS_ARTBOARD_GUIDES_HPP
#define RELLU_TOOLS_ARTBOARD_GUIDES_HPP
#include <optional>
#include <string>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Model/Artboard.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"
#include "Rellu/Tools/Tool.hpp"
#include "Rellu/Tools/ToolContext.hpp"

namespace Rellu {

  /** The color of artboard guides. */
  constexpr auto GUIDE_COLOR = make_color(0, 255, 255);

  /** The color of the artboard guide being dragged. */
  constexpr auto ACTIVE_GUIDE_COLOR = make_color(0, 217, 217);

  /**
   * Draws the guides of the visible artboards, except the one being
   * dragged, as one pixel lines across their artboards, when guides are
   * visible.
   * @param context The context drawn in.
   * @param overlay The overlay to draw into.
   */
  void draw_artboard_guides(const ToolContext& context, CanvasOverlay& overlay);

  /**
   * Creates, moves and deletes artboard guides with the pointer as Adobe XD
   * does. Dragging from the left or top edge of an artboard creates a
   * vertical or horizontal guide, and dragging a guide moves it unless
   * guides are locked. The position is a whole number following the
   * pointer, snapping to the edges and centers of the artboard's content,
   * or to multiples of ten while Shift is held, Ctrl suspending snapping.
   * While dragging, the distances to the neighbouring guides or artboard
   * edges and the guide's position are shown. A guide released outside its
   * artboard or onto another is deleted, and a new one is then discarded.
   * The changes are labelled Add Guide, Move Guide and Delete Guide.
   */
  class ArtboardGuides {
    public:

      /**
       * The width in logical units of the band along an artboard's edge
       * from which guides are created.
       */
      static constexpr auto CREATION_WIDTH = 11.0;

      /** The distance in logical units within which a guide is grabbed. */
      static constexpr auto SLOP = 3.0;

      /** The step positions snap to while Shift is held. */
      static constexpr auto SNAP_STEP = 10.0;

      /** Constructs ArtboardGuides with no gesture in progress. */
      ArtboardGuides();

      /**
       * Starts dragging a guide if a press lands on one, or on an edge a
       * guide can be created from.
       * @param context The context worked in.
       * @param event The press.
       * @return <code>true</code> iff a guide is dragged.
       */
      bool press(ToolContext& context, const PointerEvent& event);

      /**
       * Moves the guide dragged.
       * @param context The context worked in.
       * @param event The move.
       */
      void move(ToolContext& context, const PointerEvent& event);

      /**
       * Finishes dragging, recording the change.
       * @param context The context worked in.
       * @param event The release.
       */
      void release(ToolContext& context, const PointerEvent& event);

      /**
       * Abandons the drag, changing nothing.
       * @param context The context worked in.
       */
      void cancel(ToolContext& context);

      /** Returns <code>true</code> iff a guide is dragged. */
      bool is_busy() const;

      /**
       * Returns the pointer shown over a guide or an edge guides are
       * created from, or during a drag.
       * @param context The context worked in.
       * @param position The pointer's position in logical units.
       */
      std::optional<ToolCursor> hover(
        const ToolContext& context, Point position);

      /**
       * Draws the guide dragged with its measurements, or the band
       * highlighted under the pointer.
       * @param context The context worked in.
       * @param overlay The overlay to draw into.
       */
      void draw(const ToolContext& context, CanvasOverlay& overlay) const;

    private:
      struct Grip {
        NodeId m_artboard;
        Orientation m_orientation;
        std::optional<std::size_t> m_index;
      };
      std::optional<Grip> m_grip;
      std::optional<Grip> m_hover;
      double m_position;
      bool m_is_shown_once;
      Point m_pointer;

      ArtboardGuides(const ArtboardGuides&) = delete;
      ArtboardGuides& operator =(const ArtboardGuides&) = delete;
      std::optional<Grip> find(
        const ToolContext& context, Point position) const;
      bool is_visible(const ToolContext& context) const;
      bool is_taken(const ToolContext& context) const;
  };
}

#endif

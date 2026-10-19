#ifndef RELLU_RENDERING_DRAWING_HPP
#define RELLU_RENDERING_DRAWING_HPP
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>
#include <QRect>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Rendering/DrawList.hpp"

namespace Rellu {

  /**
   * A region of a drawing: bounds in document coordinates grown by a margin
   * in pixels at whatever scale the drawing is drawn.
   */
  struct DrawRegion {

    /** The bounds in document coordinates. */
    Rect m_bounds;

    /** The distance in pixels the region extends beyond its bounds. */
    double m_margin;
  };

  /**
   * Returns the pixels a region covers.
   * @param region The region.
   * @param view The transform from document coordinates to pixels, which
   *        scales both axes alike without rotating.
   * @return The smallest rect of pixels containing the <i>region</i>.
   */
  QRect to_pixels(const DrawRegion& region, const Matrix& view);

  /**
   * A drawing made of draw lists in z order, indexed by the regions their
   * commands affect so that the commands drawing a region are found without
   * visiting the others.
   */
  class Drawing {
    public:

      /** Constructs an empty Drawing. */
      Drawing();

      ~Drawing();

      /** Returns the draw lists in z order. */
      const std::vector<std::shared_ptr<const DrawList>>& get_lists() const;

      /**
       * Returns the bounds in document coordinates of every region filled,
       * within its clip, leaving out the effects drawn around them.
       */
      const Rect& get_bounds() const;

      /**
       * Returns the region that the commands of a draw list affect.
       * @param index The index of the draw list in z order.
       */
      DrawRegion get_region(std::size_t index) const;

      /**
       * Replaces the draw lists, indexing only those not already part of the
       * drawing.
       * @param lists The non-null draw lists in z order, each of which must
       *        remain unchanged while it is part of the drawing.
       * @return The regions whose drawing changed.
       */
      std::vector<DrawRegion> set_lists(
        std::vector<std::shared_ptr<const DrawList>> lists);

      /**
       * Finds the commands needed to draw a region, which draw it the same as
       * every command of the drawing does.
       * @param region The region in document coordinates.
       * @param scale The number of pixels per document unit drawn at.
       * @param commands Receives the commands in drawing order.
       * @return The region in document coordinates, containing the
       *         <i>region</i>, whose pixels the commands read when drawing
       *         it.
       */
      Rect query(
        const Rect& region, double scale, DrawSequence& commands) const;

      /**
       * Tests whether no command draws within a region, so that the region
       * shows only what lies beneath the drawing.
       * @param region The region in document coordinates.
       * @param scale The number of pixels per document unit drawn at.
       */
      bool is_empty(const Rect& region, double scale) const;

    private:
      struct Entry;
      std::vector<std::shared_ptr<const DrawList>> m_lists;
      std::vector<std::unique_ptr<Entry>> m_entries;
      mutable std::optional<Rect> m_bounds;

      Drawing(const Drawing&) = delete;
      Drawing& operator =(const Drawing&) = delete;
  };
}

#endif

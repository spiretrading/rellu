#ifndef RELLU_PROTOTYPE_PREVIEW_COMPOSER_HPP
#define RELLU_PROTOTYPE_PREVIEW_COMPOSER_HPP
#include <memory>
#include <vector>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Prototype/PreviewPlayer.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

namespace Rellu {

  /**
   * Returns a draw list's commands moved by a translation and clipped to a
   * region.
   * @param list The draw list.
   * @param offset The translation.
   * @param clip The region, in the moved coordinates, that nothing is drawn
   *        outside of.
   * @param out The draw list receiving the commands.
   */
  void translate(const DrawList& list, Point offset, const Rect& clip,
    DrawList& out);

  /**
   * Composes the frames of a preview from the artboards its PreviewPlayer
   * shows: each artboard moved to its place in the viewport, scrolled,
   * clipped to the part of it shown, with its fixed layers drawn over it
   * unscrolled, and drawn at its opacity. The conversions of the stage's
   * nodes are kept, and converted again only once the stage changes them.
   */
  class PreviewComposer {
    public:

      /**
       * Constructs a PreviewComposer.
       * @param stage The document drawn, which must outlive this
       *        PreviewComposer.
       * @param tolerance The largest distance in document units between a
       *        curve and the lines approximating it.
       */
      PreviewComposer(Document& stage, double tolerance);

      /**
       * Sets the largest distance in document units between a curve and the
       * lines approximating it.
       * @param tolerance The positive tolerance.
       */
      void set_tolerance(double tolerance);

      /**
       * Composes a frame in viewport coordinates.
       * @param layers The artboards to draw, bottom first.
       * @return The frame.
       */
      std::shared_ptr<const DrawList> compose(
        const std::vector<PreviewLayer>& layers);

    private:
      Document* m_stage;
      SceneBuilder m_builder;
      Subscription m_subscription;

      PreviewComposer(const PreviewComposer&) = delete;
      PreviewComposer& operator =(const PreviewComposer&) = delete;
      void add(const NodeId& artboard, Point offset, const Rect& clip,
        DrawList& frame);
  };
}

#endif

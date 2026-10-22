#ifndef RELLU_SCENE_ASSET_RENDERER_HPP
#define RELLU_SCENE_ASSET_RENDERER_HPP
#include <memory>
#include <QImage>
#include <QSize>
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Returns the size in pixels of the image Adobe XD exports for a region.
   * @param bounds The region in document units.
   * @param scale The number of pixels per document unit.
   */
  QSize get_asset_size(const Rect& bounds, double scale);

  /** Renders nodes into images as Adobe XD renders exported assets. */
  class AssetRenderer {
    public:

      /**
       * Constructs an AssetRenderer.
       * @param document The document rendered, which must outlive this
       *        AssetRenderer.
       */
      explicit AssetRenderer(const Document& document);

      ~AssetRenderer();

      /**
       * Renders a node, clipped neither to its artboard nor to the scroll
       * groups holding it.
       * @param id The node's id.
       * @param bounds The region rendered, in document coordinates.
       * @param scale The number of pixels per document unit.
       * @return The image, with premultiplied alpha over a transparent
       *         background, of size get_asset_size(bounds, scale).
       */
      QImage render(const NodeId& id, const Rect& bounds, double scale);

    private:
      struct State;
      const Document* m_document;
      std::unique_ptr<State> m_state;

      AssetRenderer(const AssetRenderer&) = delete;
      AssetRenderer& operator =(const AssetRenderer&) = delete;
  };
}

#endif

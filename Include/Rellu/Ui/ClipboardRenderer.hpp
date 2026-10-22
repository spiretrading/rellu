#ifndef RELLU_UI_CLIPBOARD_RENDERER_HPP
#define RELLU_UI_CLIPBOARD_RENDERER_HPP
#include <memory>
#include <vector>
#include <QImage>
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** Renders the image Adobe XD puts on the system clipboard on a copy. */
  class ClipboardRenderer {
    public:

      /** Constructs a ClipboardRenderer. */
      ClipboardRenderer();

      ~ClipboardRenderer();

      /**
       * Renders copied nodes, each over the ones before it, at one pixel per
       * document unit.
       * @param document The document holding the nodes.
       * @param nodes The nodes in z order.
       * @param bounds The region rendered, in document coordinates.
       * @return The image with premultiplied alpha over a transparent
       *         background, or a null image if the region is empty or the
       *         nodes can not be rendered.
       */
      QImage render(const Document& document,
        const std::vector<NodeId>& nodes, const Rect& bounds);

    private:
      struct State;
      std::unique_ptr<State> m_state;

      ClipboardRenderer(const ClipboardRenderer&) = delete;
      ClipboardRenderer& operator =(const ClipboardRenderer&) = delete;
  };
}

#endif

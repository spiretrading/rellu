#ifndef RELLU_COMPONENT_THUMBNAILS_HPP
#define RELLU_COMPONENT_THUMBNAILS_HPP
#include <memory>
#include <QImage>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"

namespace Rellu {

  /** Renders the masters of components into thumbnails offscreen. */
  class ComponentThumbnails {
    public:

      /** Constructs ComponentThumbnails, rendering nothing until asked. */
      ComponentThumbnails();

      ~ComponentThumbnails();

      /**
       * Renders a component's master to fit a square.
       * @param document The document holding the component.
       * @param component The component.
       * @param size The width and height of the square in pixels.
       * @return The image, or a null image if nothing was rendered.
       */
      QImage render(
        const Document& document, const ComponentId& component, int size);

    private:
      CanvasRenderer m_renderer;
      std::unique_ptr<OffscreenRenderer> m_offscreen;

      ComponentThumbnails(const ComponentThumbnails&) = delete;
      ComponentThumbnails& operator =(const ComponentThumbnails&) = delete;
  };
}

#endif

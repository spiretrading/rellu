#ifndef RELLU_RENDERING_CANVAS_RENDERER_HPP
#define RELLU_RENDERING_CANVAS_RENDERER_HPP
#include <QColor>
#include "Rellu/Rendering/Renderer.hpp"

namespace Rellu {

  /** The color of the canvas behind the document. */
  inline const auto CANVAS_BACKGROUND = QColor(232, 232, 232);

  /** Renders the contents of the canvas. */
  class CanvasRenderer : public Renderer {
    public:

      /** Constructs a CanvasRenderer. */
      CanvasRenderer() = default;

      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
        int sample_count) override;
      void release() override;
      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
        double scale) override;
  };
}

#endif

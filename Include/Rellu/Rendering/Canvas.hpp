#ifndef RELLU_RENDERING_CANVAS_HPP
#define RELLU_RENDERING_CANVAS_HPP
#include <optional>
#include <QRhiWidget>
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"

namespace Rellu {

  /** Renders the design surface using the GPU. */
  class Canvas : public QRhiWidget {
    public:

      /**
       * Constructs a Canvas.
       * @param parent The parent widget.
       */
      explicit Canvas(QWidget* parent = nullptr);

      /** Returns the renderer drawing the canvas. */
      CanvasRenderer& get_renderer();

      /**
       * Returns the region Adobe XD treats as the canvas, in logical pixels
       * relative to this widget, by default the widget's own rect.
       */
      Rect get_view_region() const;

      /**
       * Sets the region Adobe XD treats as the canvas, which may extend
       * past the widget by less than a logical pixel.
       * @param region The region in logical pixels relative to this widget,
       *        or none for the widget's own rect.
       */
      virtual void set_view_region(const std::optional<Rect>& region);

    protected:
      void initialize(QRhiCommandBuffer* buffer) override;
      void render(QRhiCommandBuffer* buffer) override;
      void releaseResources() override;

    private:
      CanvasRenderer m_renderer;
      QRhi* m_rhi;
      std::optional<Rect> m_view_region;
  };
}

#endif

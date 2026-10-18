#ifndef RELLU_RENDERING_CANVAS_RENDERER_HPP
#define RELLU_RENDERING_CANVAS_RENDERER_HPP
#include <memory>
#include <QColor>
#include <QSize>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Rendering/PathRenderer.hpp"
#include "Rellu/Rendering/Renderer.hpp"

namespace Rellu {

  /** The color of the canvas behind the document. */
  inline const auto CANVAS_BACKGROUND = QColor(232, 232, 232);

  /** Renders the contents of the canvas. */
  class CanvasRenderer : public Renderer {
    public:

      /** Constructs a CanvasRenderer drawing an empty draw list. */
      CanvasRenderer();

      /** Returns the draw list drawn over the background. */
      const std::shared_ptr<const DrawList>& get_draw_list() const;

      /**
       * Sets the draw list drawn over the background, which must remain
       * unchanged while it is set.
       * @param list The draw list to draw, or null to draw nothing.
       */
      void set_draw_list(std::shared_ptr<const DrawList> list);

      /** Returns the transform from document coordinates to logical units. */
      const Matrix& get_view() const;

      /**
       * Sets the transform from document coordinates to logical units, which
       * the frame's scale then maps to pixels.
       * @param view The transform to use.
       */
      void set_view(const Matrix& view);

      /** Returns the color the frame is cleared to before drawing. */
      const QColor& get_background() const;

      /**
       * Sets the color the frame is cleared to before drawing.
       * @param background The color to clear to, whose components are taken
       *        as premultiplied by its alpha.
       */
      void set_background(const QColor& background);

      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
        int sample_count) override;
      void release() override;
      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
        double scale) override;

    private:
      PathRenderer m_paths;
      std::shared_ptr<const DrawList> m_list;
      Matrix m_view;
      QColor m_background;
      QRhi* m_rhi;
      bool m_is_prepared;
      QSize m_size;
      double m_scale;
  };
}

#endif

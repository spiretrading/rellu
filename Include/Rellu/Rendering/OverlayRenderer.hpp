#ifndef RELLU_RENDERING_OVERLAY_RENDERER_HPP
#define RELLU_RENDERING_OVERLAY_RENDERER_HPP
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <QSize>
#include <rhi/qrhi.h>
#include "Rellu/Rendering/CanvasOverlay.hpp"

namespace Rellu {

  /**
   * Draws a CanvasOverlay over what a render target already holds, in a
   * render pass of its own at the target's full resolution.
   */
  class OverlayRenderer {
    public:

      /** Constructs an OverlayRenderer. */
      OverlayRenderer();

      ~OverlayRenderer();

      /**
       * Creates the graphics resources used to draw.
       * @param rhi The QRhi that frames are recorded with.
       */
      void initialize(QRhi& rhi);

      /** Releases the graphics resources created by initialize. */
      void release();

      /**
       * Records the drawing of an overlay, doing nothing unless the render
       * target is a texture render target.
       * @param buffer The command buffer, without an active render pass.
       * @param target The render target, whose contents are kept beneath
       *        the overlay.
       * @param scale The number of pixels per logical unit.
       * @param overlay The overlay to draw.
       */
      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
        double scale, const CanvasOverlay& overlay);

    private:
      struct ImageTexture {
        std::weak_ptr<const QImage> m_image;
        std::unique_ptr<QRhiTexture> m_texture;
        std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
        bool m_is_used;
      };
      QRhi* m_rhi;
      QRhiTexture* m_texture;
      std::uint64_t m_native;
      QSize m_size;
      QRhiTexture::Format m_format;
      std::unique_ptr<QRhiTextureRenderTarget> m_target;
      std::unique_ptr<QRhiRenderPassDescriptor> m_descriptor;
      std::unique_ptr<QRhiBuffer> m_vertices;
      std::size_t m_vertex_capacity;
      std::unique_ptr<QRhiBuffer> m_uniforms;
      std::unique_ptr<QRhiBuffer> m_corners;
      std::unique_ptr<QRhiBuffer> m_image_uniforms;
      std::unique_ptr<QRhiBuffer> m_layout_uniforms;
      std::size_t m_image_capacity;
      std::uint32_t m_uniform_stride;
      std::unique_ptr<QRhiSampler> m_sampler;
      std::unique_ptr<QRhiTexture> m_blank;
      std::unique_ptr<QRhiShaderResourceBindings> m_shape_bindings;
      std::unique_ptr<QRhiShaderResourceBindings> m_image_layout;
      std::unique_ptr<QRhiGraphicsPipeline> m_shape_pipeline;
      std::unique_ptr<QRhiGraphicsPipeline> m_image_pipeline;
      std::unordered_map<qint64, ImageTexture> m_images;
      bool m_is_corners_uploaded;

      OverlayRenderer(const OverlayRenderer&) = delete;
      OverlayRenderer& operator =(const OverlayRenderer&) = delete;
      bool prepare_target(QRhiRenderTarget& target);
      void make_pipelines();
      void reserve_images(std::size_t count);
  };
}

#endif

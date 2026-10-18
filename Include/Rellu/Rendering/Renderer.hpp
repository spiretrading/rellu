#ifndef RELLU_RENDERING_RENDERER_HPP
#define RELLU_RENDERING_RENDERER_HPP
#include <stdexcept>
#include <QRect>
#include <QSize>
#include <rhi/qrhi.h>

namespace Rellu {

  /**
   * Records the drawing of frames into a QRhiCommandBuffer. A Renderer is
   * initialized for one QRhi at a time and its resources are released before
   * that QRhi is destroyed.
   */
  class Renderer {
    public:
      virtual ~Renderer() = default;

      /**
       * Creates the graphics resources used to draw frames.
       * @param rhi The QRhi that frames are recorded with.
       * @param descriptor The render pass descriptor compatible with every
       *        render target drawn to.
       * @param sample_count The sample count of every render target drawn to.
       */
      virtual void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
        int sample_count) = 0;

      /** Releases the graphics resources created by initialize. */
      virtual void release() = 0;

      /**
       * Records a frame, including its render pass.
       * @param buffer The command buffer to record into, without an active
       *        render pass.
       * @param target The render target to draw to.
       * @param scale The number of pixels per logical unit.
       */
      virtual void render(
        QRhiCommandBuffer& buffer, QRhiRenderTarget& target, double scale) = 0;

      /**
       * Records a region of a frame into a texture, drawing each pixel of the
       * region the same as recording the whole frame does.
       * @param buffer The command buffer to record into, without an active
       *        render pass.
       * @param texture The RGBA8 texture receiving the region at its top left
       *        as rendered, at least as large as the region.
       * @param scale The number of pixels per logical unit.
       * @param size The size of the frame in pixels.
       * @param region The region of the frame to record.
       * @throws std::invalid_argument The Renderer does not record regions.
       */
      virtual void render(QRhiCommandBuffer& buffer, QRhiTexture& texture,
        double scale, QSize size, const QRect& region);

    protected:

      /** Constructs a Renderer. */
      Renderer() = default;

    private:
      Renderer(const Renderer&) = delete;
      Renderer& operator =(const Renderer&) = delete;
  };

  inline void Renderer::render(QRhiCommandBuffer& buffer, QRhiTexture& texture,
      double scale, QSize size, const QRect& region) {
    throw std::invalid_argument("The renderer does not record regions.");
  }
}

#endif

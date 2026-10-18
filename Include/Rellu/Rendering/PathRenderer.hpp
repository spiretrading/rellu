#ifndef RELLU_RENDERING_PATH_RENDERER_HPP
#define RELLU_RENDERING_PATH_RENDERER_HPP
#include <memory>
#include <QSize>
#include <rhi/qrhi.h>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"

namespace Rellu {

  /**
   * Draws a DrawList's filled paths with antialiasing based on the exact
   * area each path covers in every pixel. Each frame first prepares the
   * draw list outside of a render pass, then draws it within one.
   */
  class PathRenderer {
    public:

      /** Constructs a PathRenderer. */
      PathRenderer();

      ~PathRenderer();

      /**
       * Creates the graphics resources used to draw.
       * @param rhi The QRhi that frames are recorded with.
       * @param descriptor The render pass descriptor compatible with every
       *        render target drawn to.
       * @param sample_count The sample count of every render target drawn to.
       */
      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
        int sample_count);

      /** Releases the graphics resources created by initialize. */
      void release();

      /**
       * Rasterizes a draw list and records the uploads needed to draw it.
       * @param batch The batch receiving the uploads, which must be submitted
       *        within the frame drawing the list.
       * @param list The draw list to draw.
       * @param view The transform from document coordinates to pixels of the
       *        render target.
       * @param size The size in pixels of the render target.
       */
      void prepare(QRhiResourceUpdateBatch& batch, const DrawList& list,
        const Matrix& view, QSize size);

      /**
       * Records the drawing of the most recently prepared draw list.
       * @param buffer The command buffer with an active render pass on a
       *        render target of the prepared size.
       */
      void draw(QRhiCommandBuffer& buffer);

      /** Returns the scene most recently prepared. */
      const StripScene& get_scene() const;

    private:
      QRhi* m_rhi;
      StripRasterizer m_rasterizer;
      StripScene m_scene;
      QSize m_size;
      std::unique_ptr<QRhiBuffer> m_corners;
      std::unique_ptr<QRhiBuffer> m_strips;
      std::unique_ptr<QRhiBuffer> m_uniforms;
      std::unique_ptr<QRhiTexture> m_atlas;
      std::unique_ptr<QRhiSampler> m_sampler;
      std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
      std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
      bool m_is_corners_uploaded;

      PathRenderer(const PathRenderer&) = delete;
      PathRenderer& operator =(const PathRenderer&) = delete;
      void reserve_strips();
      void reserve_atlas();
  };
}

#endif

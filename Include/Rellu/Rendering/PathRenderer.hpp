#ifndef RELLU_RENDERING_PATH_RENDERER_HPP
#define RELLU_RENDERING_PATH_RENDERER_HPP
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <QColor>
#include <QImage>
#include <QSize>
#include <rhi/qrhi.h>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Rendering/RenderPlan.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"

namespace Rellu {

  /**
   * Draws a DrawList's filled paths with antialiasing based on the exact
   * area each path covers in every pixel, compositing layers and effects
   * through intermediate textures. Each frame first prepares the draw list
   * outside of a render pass, then renders it. A list without layers or
   * backdrop blurs draws in a single pass.
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
       * @param list The draw list to draw, which must remain unchanged until
       *        it is drawn.
       * @param view The transform from document coordinates to pixels of the
       *        render target.
       * @param size The size in pixels of the render target.
       */
      void prepare(QRhiResourceUpdateBatch& batch, const DrawList& list,
        const Matrix& view, QSize size);

      /**
       * Records the drawing of the most recently prepared draw list,
       * including every render pass.
       * @param buffer The command buffer, without an active render pass.
       * @param target The render target of the prepared size.
       * @param background The color the render target is cleared to, whose
       *        components are taken as premultiplied by its alpha.
       * @param batch A batch submitted with the first render pass, or null.
       */
      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
        const QColor& background, QRhiResourceUpdateBatch* batch);

      /** Returns the scene most recently prepared. */
      const StripScene& get_scene() const;

      /** Returns the plan most recently prepared. */
      const RenderPlan& get_plan() const;

    private:
      struct Surface {
        QSize m_size;
        std::unique_ptr<QRhiTexture> m_texture;
        std::unique_ptr<QRhiTextureRenderTarget> m_clearing;
        std::unique_ptr<QRhiTextureRenderTarget> m_keeping;
        std::unique_ptr<QRhiRenderPassDescriptor> m_clearing_descriptor;
        std::unique_ptr<QRhiRenderPassDescriptor> m_keeping_descriptor;
        bool m_is_used;
      };
      struct ImageTexture {
        std::weak_ptr<const QImage> m_image;
        std::unique_ptr<QRhiTexture> m_texture;
        bool m_is_used;
      };
      QRhi* m_rhi;
      StripRasterizer m_rasterizer;
      StripScene m_scene;
      RenderPlan m_plan;
      QSize m_size;
      std::unique_ptr<QRhiBuffer> m_corners;
      std::unique_ptr<QRhiBuffer> m_strips;
      std::unique_ptr<QRhiBuffer> m_uniforms;
      std::unique_ptr<QRhiBuffer> m_layout_uniforms;
      std::unique_ptr<QRhiTexture> m_atlas;
      std::unique_ptr<QRhiTexture> m_paints;
      std::unique_ptr<QRhiTexture> m_ramps;
      std::unique_ptr<QRhiTexture> m_blank;
      std::unique_ptr<QRhiSampler> m_nearest;
      std::unique_ptr<QRhiSampler> m_linear;
      std::unique_ptr<QRhiTexture> m_format_texture;
      std::unique_ptr<QRhiTextureRenderTarget> m_format_target;
      std::unique_ptr<QRhiRenderPassDescriptor> m_format_descriptor;
      std::unique_ptr<QRhiShaderResourceBindings> m_layout;
      std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
      std::unique_ptr<QRhiGraphicsPipeline> m_solid_pipeline;
      std::unique_ptr<QRhiGraphicsPipeline> m_solid;
      std::unique_ptr<QRhiGraphicsPipeline> m_over;
      std::unique_ptr<QRhiGraphicsPipeline> m_atop;
      std::unique_ptr<QRhiGraphicsPipeline> m_blend;
      std::unique_ptr<QRhiGraphicsPipeline> m_blur;
      std::map<std::tuple<QRhiTexture*, QRhiTexture*, PlanPipeline>,
        std::unique_ptr<QRhiShaderResourceBindings>> m_bindings;
      std::vector<Surface> m_surfaces;
      std::vector<int> m_assignments;
      std::unordered_map<const QImage*, ImageTexture> m_images;
      std::uint32_t m_uniform_stride;
      bool m_is_corners_uploaded;

      PathRenderer(const PathRenderer&) = delete;
      PathRenderer& operator =(const PathRenderer&) = delete;
      void reserve_strips(std::size_t size);
      void reserve_uniforms(std::size_t size);
      void reserve_texture(std::unique_ptr<QRhiTexture>& texture,
        QRhiTexture::Format format, QSize size);
      void prepare_surfaces();
      void prepare_images(QRhiResourceUpdateBatch& batch);
      QRhiTexture* get_texture(const PlanSource& source);
      QRhiShaderResourceBindings& get_bindings(
        QRhiTexture* source, QRhiTexture* backdrop, PlanPipeline pipeline);
      QRhiGraphicsPipeline& get_pipeline(
        PlanPipeline pipeline, bool is_external);
      void record(QRhiCommandBuffer& buffer, const PlanPass& pass,
        bool is_external);
  };
}

#endif

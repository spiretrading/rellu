#ifndef RELLU_RENDERING_BENCHMARKS_PROTOTYPE_RENDERER_HPP
#define RELLU_RENDERING_BENCHMARKS_PROTOTYPE_RENDERER_HPP
#include <memory>
#include <string_view>
#include <vector>
#include <QByteArrayView>
#include <QSize>
#include <rhi/qrhi.h>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Rendering/Renderer.hpp"

namespace Rellu {
  QByteArrayView find_benchmark_shader(std::string_view name);

  enum class Approach {
    STENCIL,
    TRIANGULATION
  };

  struct Vertex {
    float m_x;
    float m_y;
    float m_red;
    float m_green;
    float m_blue;
    float m_alpha;
  };

  struct PrototypeDraw {
    int m_first;
    int m_count;
    FillRule m_rule;
  };

  struct PrototypeScene {
    std::vector<Vertex> m_vertices;
    std::vector<Vertex> m_covers;
    std::vector<PrototypeDraw> m_draws;
  };

  void prepare_stencil(const DrawList& list, const Matrix& view, QSize size,
    PrototypeScene& scene);
  void prepare_triangulation(const DrawList& list, const Matrix& view,
    QSize size, PrototypeScene& scene);

  class PrototypeRenderer : public Renderer {
    public:
      PrototypeRenderer(Approach approach, int sample_count);
      ~PrototypeRenderer() override;

      void set_draw_list(std::shared_ptr<const DrawList> list);
      void set_view(const Matrix& view);
      const PrototypeScene& get_scene() const;
      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
        int sample_count) override;
      void release() override;
      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
        double scale) override;

    private:
      Approach m_approach;
      int m_sample_count;
      std::shared_ptr<const DrawList> m_list;
      Matrix m_view;
      bool m_is_prepared;
      QRhi* m_rhi;
      PrototypeScene m_scene;
      std::unique_ptr<QRhiRenderBuffer> m_color;
      std::unique_ptr<QRhiRenderBuffer> m_depth_stencil;
      std::unique_ptr<QRhiTexture> m_resolve;
      std::unique_ptr<QRhiTextureRenderTarget> m_target;
      std::unique_ptr<QRhiRenderPassDescriptor> m_descriptor;
      std::unique_ptr<QRhiBuffer> m_uniforms;
      std::unique_ptr<QRhiBuffer> m_vertices;
      std::unique_ptr<QRhiBuffer> m_covers;
      std::unique_ptr<QRhiBuffer> m_blit_vertices;
      std::unique_ptr<QRhiSampler> m_sampler;
      std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
      std::unique_ptr<QRhiShaderResourceBindings> m_blit_bindings;
      std::unique_ptr<QRhiGraphicsPipeline> m_non_zero;
      std::unique_ptr<QRhiGraphicsPipeline> m_even_odd;
      std::unique_ptr<QRhiGraphicsPipeline> m_cover;
      std::unique_ptr<QRhiGraphicsPipeline> m_triangles;
      std::unique_ptr<QRhiGraphicsPipeline> m_blit;
      QRhiRenderPassDescriptor* m_output;

      void prepare_target(QSize size);
      void upload(QRhiResourceUpdateBatch& batch,
        std::unique_ptr<QRhiBuffer>& buffer, const std::vector<Vertex>& data);
  };
}

#endif

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Rendering/OffscreenRenderer.hpp"

using namespace Rellu;

namespace {
  const auto VERTEX_HLSL =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  return float4(float(id & 1) - 1.0, float(id >> 1), 0.0, 1.0);\n"
    "}\n";
  const auto VERTEX_GLSL =
    "#version 330\n"
    "void main() {\n"
    "  gl_Position = vec4(\n"
    "    float(gl_VertexID & 1) - 1.0, float(gl_VertexID >> 1), 0.0, 1.0);\n"
    "}\n";
  const auto FRAGMENT_HLSL =
    "float4 main() : SV_Target {\n"
    "  return float4(1.0, 0.0, 0.0, 1.0);\n"
    "}\n";
  const auto FRAGMENT_GLSL =
    "#version 330\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  color = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "}\n";

  auto make_shader(QShader::Stage stage, const char* hlsl, const char* glsl) {
    auto shader = QShader();
    shader.setStage(stage);
    shader.setShader(QShaderKey(QShader::HlslShader, QShaderVersion(50)),
      QShaderCode(hlsl, "main"));
    shader.setShader(QShaderKey(QShader::GlslShader, QShaderVersion(330)),
      QShaderCode(glsl));
    return shader;
  }

  class QuadrantRenderer : public Renderer {
    public:
      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
          int sample_count) override {
        m_bindings.reset(rhi.newShaderResourceBindings());
        REQUIRE(m_bindings->create());
        m_pipeline.reset(rhi.newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages({
          QRhiShaderStage(QRhiShaderStage::Vertex,
            make_shader(QShader::VertexStage, VERTEX_HLSL, VERTEX_GLSL)),
          QRhiShaderStage(QRhiShaderStage::Fragment,
            make_shader(QShader::FragmentStage, FRAGMENT_HLSL, FRAGMENT_GLSL))
        });
        m_pipeline->setShaderResourceBindings(m_bindings.get());
        m_pipeline->setRenderPassDescriptor(&descriptor);
        m_pipeline->setSampleCount(sample_count);
        REQUIRE(m_pipeline->create());
      }

      void release() override {
        m_pipeline.reset();
        m_bindings.reset();
      }

      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
          double scale) override {
        auto size = target.pixelSize();
        buffer.beginPass(&target, QColor(0, 0, 255),
          QRhiDepthStencilClearValue(1, 0));
        buffer.setGraphicsPipeline(m_pipeline.get());
        buffer.setViewport(QRhiViewport(0, 0, size.width(), size.height()));
        buffer.setShaderResources();
        buffer.draw(4);
        buffer.endPass();
      }

    private:
      std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
      std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
  };
}

TEST_SUITE("OffscreenRenderer") {
  TEST_CASE("default_backend") {
    auto renderer = QuadrantRenderer();
    auto offscreen = OffscreenRenderer(renderer);
    auto backend = offscreen.get_backend();
#ifdef Q_OS_WIN
    REQUIRE(backend.m_implementation == QRhi::D3D11);
    REQUIRE(backend.m_is_software);
    REQUIRE(offscreen.get_rhi().driverInfo().deviceType ==
      QRhiDriverInfo::CpuDevice);
#endif
    REQUIRE(offscreen.get_rhi().backend() == backend.m_implementation);
  }

  TEST_CASE("orientation") {
    auto backends = std::vector<RenderBackend>();
    backends.push_back(get_default_render_backend());
#ifdef Q_OS_WIN
    backends.push_back(RenderBackend(QRhi::D3D12, true));
#endif
    backends.push_back(RenderBackend(QRhi::OpenGLES2, false));
    for(auto backend : backends) {
      auto renderer = QuadrantRenderer();
      auto offscreen = std::unique_ptr<OffscreenRenderer>();
      try {
        offscreen = std::make_unique<OffscreenRenderer>(renderer, backend);
      } catch(const std::runtime_error&) {
        REQUIRE(backend.m_implementation !=
          get_default_render_backend().m_implementation);
        MESSAGE("Skipped unavailable backend " <<
          std::string(QRhi::backendName(backend.m_implementation)));
        continue;
      }
      auto name = std::string(offscreen->get_rhi().backendName());
      CAPTURE(name);
      auto image = offscreen->render(QSize(64, 48), 1);
      REQUIRE(image.format() == QImage::Format_RGBA8888_Premultiplied);
      REQUIRE(image.size() == QSize(64, 48));
      auto red = QColor(255, 0, 0);
      auto blue = QColor(0, 0, 255);
      REQUIRE(image.pixelColor(0, 0) == red);
      REQUIRE(image.pixelColor(31, 23) == red);
      REQUIRE(image.pixelColor(32, 23) == blue);
      REQUIRE(image.pixelColor(31, 24) == blue);
      REQUIRE(image.pixelColor(63, 0) == blue);
      REQUIRE(image.pixelColor(0, 47) == blue);
      REQUIRE(image.pixelColor(63, 47) == blue);
    }
  }

  TEST_CASE("invalid_size") {
    auto renderer = QuadrantRenderer();
    auto offscreen = OffscreenRenderer(renderer);
    REQUIRE_THROWS_AS(
      offscreen.render(QSize(0, 10), 1), std::invalid_argument);
    auto limit = offscreen.get_rhi().resourceLimit(QRhi::TextureSizeMax);
    REQUIRE_THROWS_AS(
      offscreen.render(QSize(limit + 1, 1), 1), std::invalid_argument);
  }
}

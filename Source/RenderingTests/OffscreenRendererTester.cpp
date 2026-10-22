#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
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

  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  DrawCommand make_fill(Path path, Color color) {
    return FillCommand(share(std::move(path)), Matrix(), SolidPaint(color));
  }

  std::shared_ptr<const DrawList> make_list() {
    auto list = std::make_shared<DrawList>();
    list->push_back(FillCommand(share(make_rectangle(Rect(0, 0, 200, 150))),
      Matrix(), GradientPaint(LinearGradient(Point(0, 0), Point(200, 150)),
        {GradientStop(0, make_color(255, 255, 255)),
          GradientStop(1, make_color(40, 120, 200))}, Matrix())));
    auto shadow = BeginLayerCommand();
    shadow.m_drop_shadows.push_back(
      Shadow(Point(5, 7), 4, make_color(0, 0, 0, 0.4)));
    list->push_back(shadow);
    list->push_back(make_fill(make_rectangle(Rect(30, 30, 40, 30),
      CornerRadii(6, 6, 6, 6)), make_color(250, 200, 0)));
    list->push_back(EndLayerCommand());
    auto blur = BeginLayerCommand();
    blur.m_blur = 3;
    list->push_back(blur);
    list->push_back(
      make_fill(make_ellipse(Point(115, 55), 15, 15), make_color(200, 0, 0)));
    list->push_back(EndLayerCommand());
    auto inner = BeginLayerCommand();
    inner.m_inner_shadows.push_back(
      Shadow(Point(2, 2), 3, make_color(0, 0, 0, 0.8)));
    list->push_back(inner);
    list->push_back(make_fill(
      make_rectangle(Rect(40, 90, 50, 30)), make_color(0, 160, 80)));
    list->push_back(EndLayerCommand());
    auto multiply = BeginLayerCommand();
    multiply.m_opacity = 0.8;
    multiply.m_blend_mode = BlendMode::MULTIPLY;
    list->push_back(multiply);
    list->push_back(make_fill(
      make_rectangle(Rect(60, 50, 60, 40)), make_color(255, 100, 255)));
    list->push_back(EndLayerCommand());
    list->push_back(BackdropBlurCommand(
      share(make_rectangle(Rect(120, 80, 50, 40))), Matrix(), 5, 0.1,
      std::nullopt));
    list->push_back(make_fill(make_rectangle(Rect(120, 80, 50, 40)),
      make_color(255, 255, 255, 0.3)));
    auto masked = BeginLayerCommand();
    masked.m_mask = LayerMask(share(make_circle(Point(160, 30), 15)),
      Matrix());
    list->push_back(masked);
    list->push_back(make_fill(make_rectangle(Rect(140, 10, 50, 50)),
      make_color(0, 0, 200)));
    list->push_back(EndLayerCommand());
    auto image = QImage(8, 8, QImage::Format_RGBA8888_Premultiplied);
    for(auto y = 0; y != 8; ++y) {
      for(auto x = 0; x != 8; ++x) {
        image.setPixelColor(x, y, QColor(32 * x, 32 * y, 128, 255));
      }
    }
    list->push_back(FillCommand(share(make_rectangle(Rect(10, 110, 30, 30))),
      Matrix(), ImagePaint(std::make_shared<const QImage>(image),
        make_translation(10, 110) * make_scale(3.75, 3.75))));
    return list;
  }
}

TEST_SUITE("OffscreenRenderer") {
  TEST_CASE("export_limit") {
    REQUIRE(is_exportable(QSize(20000, 12800)));
    REQUIRE(is_exportable(QSize(16000, 16000)));
    REQUIRE(is_exportable(QSize(1, 256000000)));
    REQUIRE(!is_exportable(QSize(20000, 12801)));
    REQUIRE(!is_exportable(QSize(16000, 16001)));
    REQUIRE(!is_exportable(QSize(24000, 24000)));
    REQUIRE(!is_exportable(QSize(0, 100)));
  }

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
    REQUIRE_THROWS_AS(offscreen.set_tile_size(0), std::invalid_argument);
    REQUIRE_THROWS_AS(
      offscreen.set_tile_size(limit + 1), std::invalid_argument);
  }

  TEST_CASE("tiling") {
    auto renderer = CanvasRenderer();
    renderer.set_background(QColor(0, 0, 0, 0));
    renderer.set_draw_list(make_list());
    auto offscreen = OffscreenRenderer(renderer);
    auto whole = offscreen.get_tile_size();
    for(auto& view : {make_translation(-3.25, -7.5),
        make_translation(4.6, 2.2) * make_scale(1.5, 1.5),
        make_translation(-91.3, -45.8) * make_scale(3.3, 3.3)}) {
      renderer.set_view(view);
      for(auto scale : {1.0, 2.0}) {
        auto size = QSize(static_cast<int>(200 * scale),
          static_cast<int>(160 * scale));
        offscreen.set_tile_size(whole);
        auto image = offscreen.render(size, scale);
        for(auto tile : {37, 64, 170}) {
          offscreen.set_tile_size(tile);
          REQUIRE(offscreen.render(size, scale) == image);
        }
      }
    }
  }
}

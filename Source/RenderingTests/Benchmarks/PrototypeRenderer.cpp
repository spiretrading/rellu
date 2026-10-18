#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <clipper2/clipper.h>
#include <clipper2/clipper.triangulation.h>
#include <QByteArray>
#include <QMatrix4x4>
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "PrototypeRenderer.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 0.25;
  constexpr auto FIXED_SCALE = 256.0;

  auto load(std::string_view name) {
    auto package = find_benchmark_shader(name);
    return QShader::fromSerialized(
      QByteArray::fromRawData(package.data(), package.size()));
  }

  void create(auto& resource) {
    if(!resource.create()) {
      throw std::runtime_error("Unable to create a graphics resource.");
    }
  }

  auto get_premultiplied(const FillCommand& command) {
    auto& color = std::get<SolidPaint>(command.m_paint).m_color;
    return std::array<float, 4>({float(color.m_red * color.m_alpha),
      float(color.m_green * color.m_alpha),
      float(color.m_blue * color.m_alpha), float(color.m_alpha)});
  }

  auto to_vertex(double x, double y, const std::array<float, 4>& color) {
    return Vertex(float(x), float(y), color[0], color[1], color[2], color[3]);
  }

  auto get_polyline_bounds(const std::vector<Polyline>& polylines) {
    auto left = std::numeric_limits<double>::infinity();
    auto top = left;
    auto right = -left;
    auto bottom = -left;
    for(auto& polyline : polylines) {
      for(auto& point : polyline.m_points) {
        left = std::min(left, point.m_x);
        top = std::min(top, point.m_y);
        right = std::max(right, point.m_x);
        bottom = std::max(bottom, point.m_y);
      }
    }
    return Rect(left, top, right - left, bottom - top);
  }

  auto is_visible(const Rect& bounds, QSize size) {
    return bounds.get_right() > 0 && bounds.get_left() < size.width() &&
      bounds.get_bottom() > 0 && bounds.get_top() < size.height();
  }

  auto make_layout(int stride) {
    auto layout = QRhiVertexInputLayout();
    layout.setBindings({QRhiVertexInputBinding(stride)});
    if(stride == sizeof(Vertex)) {
      layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float4,
          2 * sizeof(float))});
    } else {
      layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float4, 0)});
    }
    return layout;
  }

  auto make_blend() {
    auto blend = QRhiGraphicsPipeline::TargetBlend();
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    return blend;
  }
}

void Rellu::prepare_stencil(const DrawList& list, const Matrix& view,
    QSize size, PrototypeScene& scene) {
  scene.m_vertices.clear();
  scene.m_covers.clear();
  scene.m_draws.clear();
  for(auto& command : list) {
    auto& fill = std::get<FillCommand>(command);
    auto polylines =
      flatten(transform(*fill.m_path, view * fill.m_transform), TOLERANCE);
    auto bounds = get_polyline_bounds(polylines);
    if(!is_visible(bounds, size)) {
      continue;
    }
    auto color = get_premultiplied(fill);
    auto first = static_cast<int>(scene.m_vertices.size());
    for(auto& polyline : polylines) {
      auto& points = polyline.m_points;
      for(auto i = std::size_t(1); i + 1 < points.size(); ++i) {
        scene.m_vertices.push_back(
          to_vertex(points[0].m_x, points[0].m_y, color));
        scene.m_vertices.push_back(
          to_vertex(points[i].m_x, points[i].m_y, color));
        scene.m_vertices.push_back(
          to_vertex(points[i + 1].m_x, points[i + 1].m_y, color));
      }
    }
    auto count = static_cast<int>(scene.m_vertices.size()) - first;
    if(count == 0) {
      continue;
    }
    for(auto& corner : {bounds.get_top_left(),
        Point(bounds.get_right(), bounds.get_top()),
        Point(bounds.get_left(), bounds.get_bottom()),
        Point(bounds.get_right(), bounds.get_top()), bounds.get_bottom_right(),
        Point(bounds.get_left(), bounds.get_bottom())}) {
      scene.m_covers.push_back(to_vertex(corner.m_x, corner.m_y, color));
    }
    scene.m_draws.push_back(
      PrototypeDraw(first, count, fill.m_path->get_fill_rule()));
  }
}

void Rellu::prepare_triangulation(const DrawList& list, const Matrix& view,
    QSize size, PrototypeScene& scene) {
  scene.m_vertices.clear();
  scene.m_covers.clear();
  scene.m_draws.clear();
  for(auto& command : list) {
    auto& fill = std::get<FillCommand>(command);
    auto polylines =
      flatten(transform(*fill.m_path, view * fill.m_transform), TOLERANCE);
    auto bounds = get_polyline_bounds(polylines);
    if(!is_visible(bounds, size)) {
      continue;
    }
    auto paths = Clipper2Lib::Paths64();
    for(auto& polyline : polylines) {
      auto& path = paths.emplace_back();
      for(auto& point : polyline.m_points) {
        path.emplace_back(std::llround(point.m_x * FIXED_SCALE),
          std::llround(point.m_y * FIXED_SCALE));
      }
    }
    auto rule = Clipper2Lib::FillRule::NonZero;
    if(fill.m_path->get_fill_rule() == FillRule::EVEN_ODD) {
      rule = Clipper2Lib::FillRule::EvenOdd;
    }
    auto solution = Clipper2Lib::Union(paths, rule);
    auto triangles = Clipper2Lib::Paths64();
    if(Clipper2Lib::Triangulate(solution, triangles, false) !=
        Clipper2Lib::TriangulateResult::success) {
      continue;
    }
    auto color = get_premultiplied(fill);
    for(auto& triangle : triangles) {
      for(auto& point : triangle) {
        scene.m_vertices.push_back(to_vertex(point.x / FIXED_SCALE,
          point.y / FIXED_SCALE, color));
      }
    }
  }
}

PrototypeRenderer::PrototypeRenderer(Approach approach, int sample_count)
  : m_approach(approach),
    m_sample_count(sample_count),
    m_is_prepared(false),
    m_rhi(nullptr),
    m_output(nullptr) {}

PrototypeRenderer::~PrototypeRenderer() = default;

void PrototypeRenderer::set_draw_list(std::shared_ptr<const DrawList> list) {
  m_list = std::move(list);
  m_is_prepared = false;
}

void PrototypeRenderer::set_view(const Matrix& view) {
  m_view = view;
  m_is_prepared = false;
}

const PrototypeScene& PrototypeRenderer::get_scene() const {
  return m_scene;
}

void PrototypeRenderer::initialize(
    QRhi& rhi, QRhiRenderPassDescriptor& descriptor, int sample_count) {
  m_rhi = &rhi;
  m_output = &descriptor;
  m_uniforms.reset(rhi.newBuffer(
    QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
  create(*m_uniforms);
  m_blit_vertices.reset(rhi.newBuffer(
    QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 12 * sizeof(float)));
  create(*m_blit_vertices);
  m_sampler.reset(rhi.newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
    QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  create(*m_sampler);
  m_bindings.reset(rhi.newShaderResourceBindings());
  m_bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
    0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get())});
  create(*m_bindings);
  m_is_prepared = false;
}

void PrototypeRenderer::release() {
  m_blit.reset();
  m_triangles.reset();
  m_cover.reset();
  m_even_odd.reset();
  m_non_zero.reset();
  m_blit_bindings.reset();
  m_bindings.reset();
  m_sampler.reset();
  m_blit_vertices.reset();
  m_covers.reset();
  m_vertices.reset();
  m_uniforms.reset();
  m_target.reset();
  m_descriptor.reset();
  m_resolve.reset();
  m_depth_stencil.reset();
  m_color.reset();
  m_rhi = nullptr;
}

void PrototypeRenderer::prepare_target(QSize size) {
  if(m_target && m_resolve->pixelSize() == size) {
    return;
  }
  if(m_target) {
    m_resolve->setPixelSize(size);
    create(*m_resolve);
    if(m_color) {
      m_color->setPixelSize(size);
      create(*m_color);
    }
    m_depth_stencil->setPixelSize(size);
    create(*m_depth_stencil);
    create(*m_target);
  } else {
    m_resolve.reset(m_rhi->newTexture(
      QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
    create(*m_resolve);
    auto attachment = QRhiColorAttachment(m_resolve.get());
    if(m_sample_count > 1) {
      m_color.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::Color, size,
        m_sample_count, {}, QRhiTexture::RGBA8));
      create(*m_color);
      attachment = QRhiColorAttachment(m_color.get());
      attachment.setResolveTexture(m_resolve.get());
    }
    m_depth_stencil.reset(m_rhi->newRenderBuffer(
      QRhiRenderBuffer::DepthStencil, size, m_sample_count));
    create(*m_depth_stencil);
    auto description = QRhiTextureRenderTargetDescription(attachment);
    description.setDepthStencilBuffer(m_depth_stencil.get());
    m_target.reset(m_rhi->newTextureRenderTarget(description));
    m_descriptor.reset(m_target->newCompatibleRenderPassDescriptor());
    m_target->setRenderPassDescriptor(m_descriptor.get());
    create(*m_target);
    auto make_pipeline = [&] (auto configure) {
      auto pipeline =
        std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
      pipeline->setShaderStages({
        QRhiShaderStage(QRhiShaderStage::Vertex, load("solid.vert")),
        QRhiShaderStage(QRhiShaderStage::Fragment, load("solid.frag"))});
      pipeline->setVertexInputLayout(make_layout(sizeof(Vertex)));
      pipeline->setShaderResourceBindings(m_bindings.get());
      pipeline->setRenderPassDescriptor(m_descriptor.get());
      pipeline->setSampleCount(m_sample_count);
      configure(*pipeline);
      create(*pipeline);
      return pipeline;
    };
    auto make_stencil = [&] (QRhiGraphicsPipeline::StencilOp front,
        QRhiGraphicsPipeline::StencilOp back) {
      return make_pipeline([&] (QRhiGraphicsPipeline& pipeline) {
        auto blend = QRhiGraphicsPipeline::TargetBlend();
        blend.colorWrite = {};
        pipeline.setTargetBlends({blend});
        pipeline.setStencilTest(true);
        pipeline.setStencilFront(QRhiGraphicsPipeline::StencilOpState(
          QRhiGraphicsPipeline::Keep, QRhiGraphicsPipeline::Keep, front,
          QRhiGraphicsPipeline::Always));
        pipeline.setStencilBack(QRhiGraphicsPipeline::StencilOpState(
          QRhiGraphicsPipeline::Keep, QRhiGraphicsPipeline::Keep, back,
          QRhiGraphicsPipeline::Always));
      });
    };
    m_non_zero = make_stencil(QRhiGraphicsPipeline::IncrementAndWrap,
      QRhiGraphicsPipeline::DecrementAndWrap);
    m_even_odd = make_stencil(
      QRhiGraphicsPipeline::Invert, QRhiGraphicsPipeline::Invert);
    m_cover = make_pipeline([&] (QRhiGraphicsPipeline& pipeline) {
      auto state = QRhiGraphicsPipeline::StencilOpState(
        QRhiGraphicsPipeline::Keep, QRhiGraphicsPipeline::Keep,
        QRhiGraphicsPipeline::StencilZero, QRhiGraphicsPipeline::NotEqual);
      pipeline.setFlags(QRhiGraphicsPipeline::UsesStencilRef);
      pipeline.setTargetBlends({make_blend()});
      pipeline.setStencilTest(true);
      pipeline.setStencilFront(state);
      pipeline.setStencilBack(state);
    });
    m_triangles = make_pipeline([&] (QRhiGraphicsPipeline& pipeline) {
      pipeline.setTargetBlends({make_blend()});
    });
  }
  m_blit_bindings.reset(m_rhi->newShaderResourceBindings());
  m_blit_bindings->setBindings({QRhiShaderResourceBinding::sampledTexture(0,
    QRhiShaderResourceBinding::FragmentStage, m_resolve.get(),
    m_sampler.get())});
  create(*m_blit_bindings);
  if(!m_blit) {
    m_blit.reset(m_rhi->newGraphicsPipeline());
    m_blit->setShaderStages({
      QRhiShaderStage(QRhiShaderStage::Vertex, load("blit.vert")),
      QRhiShaderStage(QRhiShaderStage::Fragment, load("blit.frag"))});
    m_blit->setVertexInputLayout(make_layout(4 * sizeof(float)));
    m_blit->setShaderResourceBindings(m_blit_bindings.get());
    m_blit->setRenderPassDescriptor(m_output);
    create(*m_blit);
  }
}

void PrototypeRenderer::upload(QRhiResourceUpdateBatch& batch,
    std::unique_ptr<QRhiBuffer>& buffer, const std::vector<Vertex>& data) {
  auto size = static_cast<quint32>(data.size() * sizeof(Vertex));
  if(size == 0) {
    return;
  }
  if(!buffer || buffer->size() < size) {
    buffer.reset(m_rhi->newBuffer(
      QRhiBuffer::Static, QRhiBuffer::VertexBuffer, size));
    create(*buffer);
  }
  batch.uploadStaticBuffer(buffer.get(), 0, size, data.data());
}

void PrototypeRenderer::render(
    QRhiCommandBuffer& buffer, QRhiRenderTarget& target, double scale) {
  auto size = target.pixelSize();
  auto is_new_target = !m_target || m_resolve->pixelSize() != size;
  prepare_target(size);
  auto batch = m_rhi->nextResourceUpdateBatch();
  if(is_new_target) {
    auto v = m_rhi->isYUpInFramebuffer() ? 0.0f : 1.0f;
    auto blit = std::array<float, 12>(
      {-1, -1, 0, v, 3, -1, 2, v, -1, 3, 0, 2 - 3 * v});
    batch->uploadStaticBuffer(m_blit_vertices.get(), blit.data());
    auto projection = m_rhi->clipSpaceCorrMatrix();
    projection.ortho(0, float(size.width()), float(size.height()), 0, -1, 1);
    batch->updateDynamicBuffer(
      m_uniforms.get(), 0, 16 * sizeof(float), projection.constData());
    m_is_prepared = false;
  }
  if(!m_is_prepared) {
    static const auto EMPTY = DrawList();
    auto& list = m_list ? *m_list : EMPTY;
    auto view = make_scale(scale, scale) * m_view;
    if(m_approach == Approach::STENCIL) {
      prepare_stencil(list, view, size, m_scene);
    } else {
      prepare_triangulation(list, view, size, m_scene);
    }
    upload(*batch, m_vertices, m_scene.m_vertices);
    upload(*batch, m_covers, m_scene.m_covers);
    m_is_prepared = true;
  }
  buffer.beginPass(m_target.get(), CANVAS_BACKGROUND,
    QRhiDepthStencilClearValue(1, 0), batch);
  auto viewport = QRhiViewport(0, 0, float(size.width()),
    float(size.height()));
  if(m_approach == Approach::STENCIL) {
    for(auto i = std::size_t(0); i != m_scene.m_draws.size(); ++i) {
      auto& draw = m_scene.m_draws[i];
      if(draw.m_rule == FillRule::NON_ZERO) {
        buffer.setGraphicsPipeline(m_non_zero.get());
      } else {
        buffer.setGraphicsPipeline(m_even_odd.get());
      }
      buffer.setViewport(viewport);
      buffer.setShaderResources(m_bindings.get());
      auto input = QRhiCommandBuffer::VertexInput(m_vertices.get(), 0);
      buffer.setVertexInput(0, 1, &input);
      buffer.draw(draw.m_count, 1, draw.m_first);
      buffer.setGraphicsPipeline(m_cover.get());
      buffer.setViewport(viewport);
      buffer.setShaderResources(m_bindings.get());
      buffer.setStencilRef(0);
      auto cover = QRhiCommandBuffer::VertexInput(m_covers.get(), 0);
      buffer.setVertexInput(0, 1, &cover);
      buffer.draw(6, 1, static_cast<quint32>(6 * i));
    }
  } else if(!m_scene.m_vertices.empty()) {
    buffer.setGraphicsPipeline(m_triangles.get());
    buffer.setViewport(viewport);
    buffer.setShaderResources(m_bindings.get());
    auto input = QRhiCommandBuffer::VertexInput(m_vertices.get(), 0);
    buffer.setVertexInput(0, 1, &input);
    buffer.draw(static_cast<quint32>(m_scene.m_vertices.size()));
  }
  buffer.endPass();
  buffer.beginPass(&target, QColor(0, 0, 0, 0),
    QRhiDepthStencilClearValue(1, 0));
  buffer.setGraphicsPipeline(m_blit.get());
  buffer.setViewport(viewport);
  buffer.setShaderResources(m_blit_bindings.get());
  auto input = QRhiCommandBuffer::VertexInput(m_blit_vertices.get(), 0);
  buffer.setVertexInput(0, 1, &input);
  buffer.draw(3);
  buffer.endPass();
}

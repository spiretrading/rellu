#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <QByteArray>
#include <QMatrix4x4>
#include "Rellu/Rendering/PathRenderer.hpp"
#include "Rellu/Rendering/Shaders.hpp"

using namespace Rellu;

namespace {
  const auto CORNERS = std::array<float, 8>({0, 0, 1, 0, 0, 1, 1, 1});
  const auto UNIFORMS_SIZE = 80;
  const auto MINIMUM_ATLAS_HEIGHT = 16;
  const auto MINIMUM_STRIPS_SIZE = 4096 * sizeof(Strip);

  void create(auto& resource) {
    if(!resource.create()) {
      throw std::runtime_error("Unable to create a graphics resource.");
    }
  }

  auto make_layout() {
    auto layout = QRhiVertexInputLayout();
    layout.setBindings({QRhiVertexInputBinding(2 * sizeof(float)),
      QRhiVertexInputBinding(
        sizeof(Strip), QRhiVertexInputBinding::PerInstance)});
    layout.setAttributes({
      QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
      QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::Float4,
        offsetof(Strip, m_x)),
      QRhiVertexInputAttribute(1, 2, QRhiVertexInputAttribute::Half4,
        offsetof(Strip, m_color)),
      QRhiVertexInputAttribute(1, 3, QRhiVertexInputAttribute::Float2,
        offsetof(Strip, m_column))});
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

PathRenderer::PathRenderer()
  : m_rhi(nullptr),
    m_is_corners_uploaded(false) {}

PathRenderer::~PathRenderer() = default;

void PathRenderer::initialize(
    QRhi& rhi, QRhiRenderPassDescriptor& descriptor, int sample_count) {
  release();
  m_rhi = &rhi;
  m_corners.reset(rhi.newBuffer(QRhiBuffer::Immutable,
    QRhiBuffer::VertexBuffer, sizeof(CORNERS)));
  create(*m_corners);
  m_uniforms.reset(rhi.newBuffer(
    QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UNIFORMS_SIZE));
  create(*m_uniforms);
  m_atlas.reset(rhi.newTexture(QRhiTexture::RGBA8,
    QSize(STRIP_ATLAS_WIDTH, MINIMUM_ATLAS_HEIGHT)));
  create(*m_atlas);
  m_sampler.reset(rhi.newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
    QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  create(*m_sampler);
  m_bindings.reset(rhi.newShaderResourceBindings());
  m_bindings->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(
      0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get()),
    QRhiShaderResourceBinding::sampledTexture(1,
      QRhiShaderResourceBinding::FragmentStage, m_atlas.get(),
      m_sampler.get())});
  create(*m_bindings);
  m_pipeline.reset(rhi.newGraphicsPipeline());
  m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
  m_pipeline->setTargetBlends({make_blend()});
  m_pipeline->setShaderStages({
    QRhiShaderStage(QRhiShaderStage::Vertex, load_shader("strip.vert")),
    QRhiShaderStage(QRhiShaderStage::Fragment, load_shader("strip.frag"))});
  m_pipeline->setVertexInputLayout(make_layout());
  m_pipeline->setShaderResourceBindings(m_bindings.get());
  m_pipeline->setRenderPassDescriptor(&descriptor);
  m_pipeline->setSampleCount(sample_count);
  create(*m_pipeline);
}

void PathRenderer::release() {
  m_pipeline.reset();
  m_bindings.reset();
  m_sampler.reset();
  m_atlas.reset();
  m_uniforms.reset();
  m_strips.reset();
  m_corners.reset();
  m_is_corners_uploaded = false;
  m_rhi = nullptr;
}

void PathRenderer::prepare(QRhiResourceUpdateBatch& batch,
    const DrawList& list, const Matrix& view, QSize size) {
  m_rasterizer.rasterize(list, view, size, m_scene);
  m_size = size;
  if(!m_is_corners_uploaded) {
    batch.uploadStaticBuffer(m_corners.get(), CORNERS.data());
    m_is_corners_uploaded = true;
  }
  reserve_atlas();
  auto projection = m_rhi->clipSpaceCorrMatrix();
  projection.ortho(0, static_cast<float>(size.width()),
    static_cast<float>(size.height()), 0, -1, 1);
  batch.updateDynamicBuffer(
    m_uniforms.get(), 0, 16 * sizeof(float), projection.constData());
  auto atlas_size = std::array<float, 2>({
    static_cast<float>(m_atlas->pixelSize().width()),
    static_cast<float>(m_atlas->pixelSize().height())});
  batch.updateDynamicBuffer(m_uniforms.get(), 16 * sizeof(float),
    sizeof(atlas_size), atlas_size.data());
  if(auto height = m_scene.get_atlas_height()) {
    auto description = QRhiTextureSubresourceUploadDescription(
      QByteArray::fromRawData(
        reinterpret_cast<const char*>(m_scene.m_atlas.data()),
        static_cast<qsizetype>(
          m_scene.m_atlas.size() * sizeof(std::uint32_t))));
    description.setSourceSize(QSize(STRIP_ATLAS_WIDTH, height));
    batch.uploadTexture(m_atlas.get(), QRhiTextureUploadDescription(
      QRhiTextureUploadEntry(0, 0, description)));
  }
  if(!m_scene.m_strips.empty()) {
    reserve_strips();
    batch.uploadStaticBuffer(m_strips.get(), 0,
      static_cast<quint32>(m_scene.m_strips.size() * sizeof(Strip)),
      m_scene.m_strips.data());
  }
}

void PathRenderer::draw(QRhiCommandBuffer& buffer) {
  if(m_scene.m_strips.empty()) {
    return;
  }
  buffer.setGraphicsPipeline(m_pipeline.get());
  buffer.setViewport(QRhiViewport(0, 0,
    static_cast<float>(m_size.width()), static_cast<float>(m_size.height())));
  buffer.setShaderResources(m_bindings.get());
  const auto inputs = std::array<QRhiCommandBuffer::VertexInput, 2>({
    QRhiCommandBuffer::VertexInput(m_corners.get(), 0),
    QRhiCommandBuffer::VertexInput(m_strips.get(), 0)});
  buffer.setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
  buffer.draw(4, static_cast<quint32>(m_scene.m_strips.size()));
}

const StripScene& PathRenderer::get_scene() const {
  return m_scene;
}

void PathRenderer::reserve_strips() {
  auto size = m_scene.m_strips.size() * sizeof(Strip);
  if(m_strips && m_strips->size() >= size) {
    return;
  }
  auto capacity = std::max(MINIMUM_STRIPS_SIZE, std::bit_ceil(size));
  m_strips.reset(m_rhi->newBuffer(QRhiBuffer::Static,
    QRhiBuffer::VertexBuffer, static_cast<quint32>(capacity)));
  create(*m_strips);
}

void PathRenderer::reserve_atlas() {
  auto height = m_scene.get_atlas_height();
  if(m_atlas->pixelSize().height() >= height) {
    return;
  }
  auto capacity = std::bit_ceil(static_cast<unsigned int>(height));
  auto limit = m_rhi->resourceLimit(QRhi::TextureSizeMax);
  if(static_cast<int>(capacity) > limit) {
    if(height > limit) {
      throw std::runtime_error("The coverage atlas is too large.");
    }
    capacity = static_cast<unsigned int>(limit);
  }
  m_atlas->setPixelSize(
    QSize(STRIP_ATLAS_WIDTH, static_cast<int>(capacity)));
  create(*m_atlas);
  m_bindings->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(
      0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get()),
    QRhiShaderResourceBinding::sampledTexture(1,
      QRhiShaderResourceBinding::FragmentStage, m_atlas.get(),
      m_sampler.get())});
  create(*m_bindings);
}

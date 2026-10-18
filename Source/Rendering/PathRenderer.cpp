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
  const auto UNIFORMS_SIZE = static_cast<int>(sizeof(PlanUniforms));
  const auto MINIMUM_ATLAS_HEIGHT = 16;
  const auto MINIMUM_STRIPS_SIZE = 4096 * sizeof(Strip);
  const auto MINIMUM_UNIFORM_COUNT = 16;

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
      QRhiVertexInputAttribute(1, 3, QRhiVertexInputAttribute::Float3,
        offsetof(Strip, m_column))});
    return layout;
  }

  auto make_blend(PlanPipeline pipeline) {
    auto blend = QRhiGraphicsPipeline::TargetBlend();
    if(pipeline == PlanPipeline::OVER || pipeline == PlanPipeline::SOLID) {
      blend.enable = true;
      blend.srcColor = QRhiGraphicsPipeline::One;
      blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
      blend.srcAlpha = QRhiGraphicsPipeline::One;
      blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    } else if(pipeline == PlanPipeline::ATOP) {
      blend.enable = true;
      blend.srcColor = QRhiGraphicsPipeline::DstAlpha;
      blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
      blend.srcAlpha = QRhiGraphicsPipeline::Zero;
      blend.dstAlpha = QRhiGraphicsPipeline::One;
    }
    return blend;
  }

  auto get_fragment_shader(PlanPipeline pipeline) {
    if(pipeline == PlanPipeline::SOLID) {
      return "solid.frag";
    } else if(pipeline == PlanPipeline::BLEND) {
      return "blend.frag";
    } else if(pipeline == PlanPipeline::BLUR) {
      return "blur.frag";
    }
    return "strip.frag";
  }

  auto make_pipeline(QRhi& rhi, PlanPipeline pipeline,
      QRhiShaderResourceBindings& layout,
      QRhiRenderPassDescriptor& descriptor, int sample_count) {
    auto result = std::unique_ptr<QRhiGraphicsPipeline>(
      rhi.newGraphicsPipeline());
    result->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    result->setTargetBlends({make_blend(pipeline)});
    result->setShaderStages({
      QRhiShaderStage(QRhiShaderStage::Vertex, load_shader(
      pipeline == PlanPipeline::SOLID ? "solid.vert" : "strip.vert")),
      QRhiShaderStage(QRhiShaderStage::Fragment,
        load_shader(get_fragment_shader(pipeline)))});
    result->setVertexInputLayout(make_layout());
    result->setShaderResourceBindings(&layout);
    result->setRenderPassDescriptor(&descriptor);
    result->setSampleCount(sample_count);
    create(*result);
    return result;
  }

  QImage to_upload(const QImage& image, int limit) {
    auto converted =
      image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if(converted.width() > limit || converted.height() > limit) {
      converted = converted.scaled(QSize(limit, limit), Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    }
    return converted;
  }
}

PathRenderer::PathRenderer()
  : m_rhi(nullptr),
    m_uniform_stride(0),
    m_is_corners_uploaded(false) {}

PathRenderer::~PathRenderer() = default;

void PathRenderer::initialize(
    QRhi& rhi, QRhiRenderPassDescriptor& descriptor, int sample_count) {
  release();
  m_rhi = &rhi;
  m_corners.reset(rhi.newBuffer(QRhiBuffer::Immutable,
    QRhiBuffer::VertexBuffer, sizeof(CORNERS)));
  create(*m_corners);
  m_uniform_stride = static_cast<std::uint32_t>(
    rhi.ubufAligned(UNIFORMS_SIZE));
  reserve_uniforms(MINIMUM_UNIFORM_COUNT);
  m_atlas.reset(rhi.newTexture(QRhiTexture::RGBA8,
    QSize(STRIP_ATLAS_WIDTH, MINIMUM_ATLAS_HEIGHT)));
  create(*m_atlas);
  m_paints.reset(rhi.newTexture(QRhiTexture::RGBA32F,
    QSize(PAINT_TABLE_WIDTH, 1)));
  create(*m_paints);
  m_ramps.reset(rhi.newTexture(QRhiTexture::RGBA8,
    QSize(GRADIENT_RAMP_WIDTH, 1)));
  create(*m_ramps);
  m_blank.reset(rhi.newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
  create(*m_blank);
  m_nearest.reset(rhi.newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
    QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  create(*m_nearest);
  m_linear.reset(rhi.newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
    QRhiSampler::Linear, QRhiSampler::ClampToEdge,
    QRhiSampler::ClampToEdge));
  create(*m_linear);
  m_format_texture.reset(rhi.newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1,
    QRhiTexture::RenderTarget));
  create(*m_format_texture);
  m_format_target.reset(rhi.newTextureRenderTarget(
    QRhiTextureRenderTargetDescription(
      QRhiColorAttachment(m_format_texture.get()))));
  m_format_descriptor.reset(
    m_format_target->newCompatibleRenderPassDescriptor());
  m_format_target->setRenderPassDescriptor(m_format_descriptor.get());
  create(*m_format_target);
  m_layout_uniforms.reset(rhi.newBuffer(QRhiBuffer::Dynamic,
    QRhiBuffer::UniformBuffer, m_uniform_stride));
  create(*m_layout_uniforms);
  m_layout.reset(rhi.newShaderResourceBindings());
  m_layout->setBindings({
    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
      QRhiShaderResourceBinding::VertexStage |
        QRhiShaderResourceBinding::FragmentStage, m_layout_uniforms.get(),
      UNIFORMS_SIZE),
    QRhiShaderResourceBinding::sampledTexture(1,
      QRhiShaderResourceBinding::FragmentStage, m_atlas.get(),
      m_nearest.get()),
    QRhiShaderResourceBinding::sampledTexture(2,
      QRhiShaderResourceBinding::VertexStage, m_paints.get(),
      m_nearest.get()),
    QRhiShaderResourceBinding::sampledTexture(3,
      QRhiShaderResourceBinding::FragmentStage, m_ramps.get(),
      m_linear.get()),
    QRhiShaderResourceBinding::sampledTexture(4,
      QRhiShaderResourceBinding::FragmentStage, m_blank.get(),
      m_linear.get()),
    QRhiShaderResourceBinding::sampledTexture(5,
      QRhiShaderResourceBinding::FragmentStage, m_blank.get(),
      m_linear.get())});
  create(*m_layout);
  m_pipeline = make_pipeline(
    rhi, PlanPipeline::OVER, *m_layout, descriptor, sample_count);
  m_solid_pipeline = make_pipeline(
    rhi, PlanPipeline::SOLID, *m_layout, descriptor, sample_count);
  m_solid = make_pipeline(
    rhi, PlanPipeline::SOLID, *m_layout, *m_format_descriptor, 1);
  m_over = make_pipeline(
    rhi, PlanPipeline::OVER, *m_layout, *m_format_descriptor, 1);
  m_atop = make_pipeline(
    rhi, PlanPipeline::ATOP, *m_layout, *m_format_descriptor, 1);
  m_blend = make_pipeline(
    rhi, PlanPipeline::BLEND, *m_layout, *m_format_descriptor, 1);
  m_blur = make_pipeline(
    rhi, PlanPipeline::BLUR, *m_layout, *m_format_descriptor, 1);
}

void PathRenderer::release() {
  m_bindings.clear();
  m_blur.reset();
  m_blend.reset();
  m_atop.reset();
  m_over.reset();
  m_solid.reset();
  m_solid_pipeline.reset();
  m_pipeline.reset();
  m_layout.reset();
  m_layout_uniforms.reset();
  m_surfaces.clear();
  m_images.clear();
  m_format_target.reset();
  m_format_descriptor.reset();
  m_format_texture.reset();
  m_linear.reset();
  m_nearest.reset();
  m_blank.reset();
  m_ramps.reset();
  m_paints.reset();
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
  auto options = PlanOptions(
    m_rhi->clipSpaceCorrMatrix(), m_rhi->isYUpInFramebuffer());
  make_render_plan(list, m_scene, view, size, options, m_plan);
  m_size = size;
  if(!m_is_corners_uploaded) {
    batch.uploadStaticBuffer(m_corners.get(), CORNERS.data());
    m_is_corners_uploaded = true;
  }
  auto atlas_height = std::max(MINIMUM_ATLAS_HEIGHT,
    static_cast<int>(std::bit_ceil(
      static_cast<unsigned int>(m_scene.get_atlas_height()))));
  auto limit = m_rhi->resourceLimit(QRhi::TextureSizeMax);
  if(atlas_height > limit) {
    if(m_scene.get_atlas_height() > limit) {
      throw std::runtime_error("The coverage atlas is too large.");
    }
    atlas_height = limit;
  }
  reserve_texture(
    m_atlas, QRhiTexture::RGBA8, QSize(STRIP_ATLAS_WIDTH, atlas_height));
  auto paint_rows = std::max<std::size_t>(1,
    (m_plan.m_paints.size() + PAINT_TABLE_WIDTH - 1) / PAINT_TABLE_WIDTH);
  reserve_texture(m_paints, QRhiTexture::RGBA32F, QSize(PAINT_TABLE_WIDTH,
    static_cast<int>(std::bit_ceil(paint_rows))));
  reserve_texture(m_ramps, QRhiTexture::RGBA8, QSize(GRADIENT_RAMP_WIDTH,
    static_cast<int>(std::bit_ceil(static_cast<unsigned int>(
      std::max(1, m_plan.get_ramp_height()))))));
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
  if(!m_plan.m_paints.empty()) {
    m_plan.m_paints.resize(
      paint_rows * PAINT_TABLE_WIDTH, std::array<float, 4>({0, 0, 0, 0}));
    auto description = QRhiTextureSubresourceUploadDescription(
      QByteArray::fromRawData(
        reinterpret_cast<const char*>(m_plan.m_paints.data()),
        static_cast<qsizetype>(
          m_plan.m_paints.size() * sizeof(std::array<float, 4>))));
    description.setSourceSize(
      QSize(PAINT_TABLE_WIDTH, static_cast<int>(paint_rows)));
    batch.uploadTexture(m_paints.get(), QRhiTextureUploadDescription(
      QRhiTextureUploadEntry(0, 0, description)));
  }
  if(auto height = m_plan.get_ramp_height()) {
    auto description = QRhiTextureSubresourceUploadDescription(
      QByteArray::fromRawData(
        reinterpret_cast<const char*>(m_plan.m_ramps.data()),
        static_cast<qsizetype>(
          m_plan.m_ramps.size() * sizeof(std::uint32_t))));
    description.setSourceSize(QSize(GRADIENT_RAMP_WIDTH, height));
    batch.uploadTexture(m_ramps.get(), QRhiTextureUploadDescription(
      QRhiTextureUploadEntry(0, 0, description)));
  }
  auto strip_count = m_scene.m_strips.size() + m_plan.m_strips.size();
  if(strip_count != 0) {
    reserve_strips(strip_count * sizeof(Strip));
    if(!m_scene.m_strips.empty()) {
      batch.uploadStaticBuffer(m_strips.get(), 0,
        static_cast<quint32>(m_scene.m_strips.size() * sizeof(Strip)),
        m_scene.m_strips.data());
    }
    if(!m_plan.m_strips.empty()) {
      batch.uploadStaticBuffer(m_strips.get(),
        static_cast<quint32>(m_scene.m_strips.size() * sizeof(Strip)),
        static_cast<quint32>(m_plan.m_strips.size() * sizeof(Strip)),
        m_plan.m_strips.data());
    }
  }
  reserve_uniforms(m_plan.m_uniforms.size());
  auto sizes = std::array<float, 4>({
    static_cast<float>(m_atlas->pixelSize().width()),
    static_cast<float>(m_atlas->pixelSize().height()),
    static_cast<float>(m_paints->pixelSize().width()),
    static_cast<float>(m_paints->pixelSize().height())});
  for(auto i = std::size_t(0); i != m_plan.m_uniforms.size(); ++i) {
    auto& uniforms = m_plan.m_uniforms[i];
    std::copy(sizes.begin(), sizes.end(), uniforms.begin() + 16);
    uniforms[31] = static_cast<float>(m_ramps->pixelSize().height());
    batch.updateDynamicBuffer(m_uniforms.get(),
      static_cast<quint32>(i * m_uniform_stride), UNIFORMS_SIZE,
      uniforms.data());
  }
  prepare_surfaces();
  prepare_images(batch);
}

void PathRenderer::render(QRhiCommandBuffer& buffer,
    QRhiRenderTarget& target, const QColor& background,
    QRhiResourceUpdateBatch* batch) {
  for(auto& step : m_plan.m_steps) {
    if(auto copy = std::get_if<PlanCopy>(&step)) {
      if(!batch) {
        batch = m_rhi->nextResourceUpdateBatch();
      }
      auto description = QRhiTextureCopyDescription();
      description.setPixelSize(copy->m_region.size());
      description.setSourceTopLeft(copy->m_region.topLeft());
      description.setDestinationTopLeft(QPoint(0, 0));
      batch->copyTexture(
        m_surfaces[m_assignments[copy->m_destination]].m_texture.get(),
        m_surfaces[m_assignments[copy->m_source]].m_texture.get(),
        description);
      buffer.resourceUpdate(batch);
      batch = nullptr;
      continue;
    }
    auto& pass = std::get<PlanPass>(step);
    auto clear = QColor(0, 0, 0, 0);
    if(pass.m_clear == PlanClear::BACKGROUND) {
      clear = background;
    }
    if(pass.m_target < 0) {
      buffer.beginPass(&target, clear, QRhiDepthStencilClearValue(1, 0),
        batch);
    } else {
      auto& surface = m_surfaces[m_assignments[pass.m_target]];
      auto render_target = surface.m_clearing.get();
      if(pass.m_clear == PlanClear::KEEP) {
        render_target = surface.m_keeping.get();
      }
      buffer.beginPass(render_target, clear, QRhiDepthStencilClearValue(1, 0),
        batch);
    }
    batch = nullptr;
    record(buffer, pass, pass.m_target < 0);
    buffer.endPass();
  }
  if(batch) {
    buffer.resourceUpdate(batch);
  }
}

const StripScene& PathRenderer::get_scene() const {
  return m_scene;
}

const RenderPlan& PathRenderer::get_plan() const {
  return m_plan;
}

void PathRenderer::reserve_strips(std::size_t size) {
  if(m_strips && m_strips->size() >= size) {
    return;
  }
  auto capacity = std::max(MINIMUM_STRIPS_SIZE, std::bit_ceil(size));
  m_strips.reset(m_rhi->newBuffer(QRhiBuffer::Static,
    QRhiBuffer::VertexBuffer, static_cast<quint32>(capacity)));
  create(*m_strips);
}

void PathRenderer::reserve_uniforms(std::size_t count) {
  auto size = std::max<std::size_t>(count, 1) * m_uniform_stride;
  if(m_uniforms && m_uniforms->size() >= size) {
    return;
  }
  auto capacity = std::bit_ceil(std::max(size,
    static_cast<std::size_t>(MINIMUM_UNIFORM_COUNT * m_uniform_stride)));
  auto uniforms = std::unique_ptr<QRhiBuffer>(m_rhi->newBuffer(
    QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
    static_cast<quint32>(capacity)));
  create(*uniforms);
  m_bindings.clear();
  m_uniforms = std::move(uniforms);
}

void PathRenderer::reserve_texture(std::unique_ptr<QRhiTexture>& texture,
    QRhiTexture::Format format, QSize size) {
  if(texture->pixelSize().width() >= size.width() &&
      texture->pixelSize().height() >= size.height()) {
    return;
  }
  texture.reset(m_rhi->newTexture(format, size));
  create(*texture);
  m_bindings.clear();
}

void PathRenderer::prepare_surfaces() {
  for(auto& surface : m_surfaces) {
    surface.m_is_used = false;
  }
  m_assignments.assign(m_plan.m_textures.size(), -1);
  for(auto i = std::size_t(0); i != m_plan.m_textures.size(); ++i) {
    auto size = m_plan.m_textures[i];
    auto match = std::find_if(m_surfaces.begin(), m_surfaces.end(),
      [&] (const auto& surface) {
        return !surface.m_is_used && surface.m_size == size;
      });
    if(match == m_surfaces.end()) {
      auto surface = Surface();
      surface.m_size = size;
      surface.m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
      create(*surface.m_texture);
      surface.m_clearing.reset(m_rhi->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription(
          QRhiColorAttachment(surface.m_texture.get()))));
      surface.m_clearing_descriptor.reset(
        surface.m_clearing->newCompatibleRenderPassDescriptor());
      surface.m_clearing->setRenderPassDescriptor(
        surface.m_clearing_descriptor.get());
      create(*surface.m_clearing);
      surface.m_keeping.reset(m_rhi->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription(
          QRhiColorAttachment(surface.m_texture.get())),
        QRhiTextureRenderTarget::PreserveColorContents));
      surface.m_keeping_descriptor.reset(
        surface.m_keeping->newCompatibleRenderPassDescriptor());
      surface.m_keeping->setRenderPassDescriptor(
        surface.m_keeping_descriptor.get());
      create(*surface.m_keeping);
      m_surfaces.push_back(std::move(surface));
      match = m_surfaces.end() - 1;
    }
    match->m_is_used = true;
    m_assignments[i] = static_cast<int>(match - m_surfaces.begin());
  }
  if(std::any_of(m_surfaces.begin(), m_surfaces.end(),
      [] (const auto& surface) {
        return !surface.m_is_used;
      })) {
    m_bindings.clear();
    auto kept = std::vector<Surface>();
    auto indices = std::vector<int>(m_surfaces.size(), -1);
    for(auto i = std::size_t(0); i != m_surfaces.size(); ++i) {
      if(m_surfaces[i].m_is_used) {
        indices[i] = static_cast<int>(kept.size());
        kept.push_back(std::move(m_surfaces[i]));
      }
    }
    m_surfaces = std::move(kept);
    for(auto& assignment : m_assignments) {
      assignment = indices[assignment];
    }
  }
}

void PathRenderer::prepare_images(QRhiResourceUpdateBatch& batch) {
  for(auto& entry : m_images) {
    entry.second.m_is_used = false;
  }
  auto limit = m_rhi->resourceLimit(QRhi::TextureSizeMax);
  for(auto& image : m_plan.m_images) {
    auto& entry = m_images[image.get()];
    if(entry.m_texture && entry.m_image.lock() == image) {
      entry.m_is_used = true;
      continue;
    }
    auto upload = to_upload(*image, limit);
    entry.m_image = image;
    entry.m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8,
      upload.size(), 1,
      QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
    create(*entry.m_texture);
    batch.uploadTexture(entry.m_texture.get(), upload);
    batch.generateMips(entry.m_texture.get());
    entry.m_is_used = true;
  }
  auto is_released = false;
  for(auto entry = m_images.begin(); entry != m_images.end();) {
    if(!entry->second.m_is_used) {
      entry = m_images.erase(entry);
      is_released = true;
    } else {
      ++entry;
    }
  }
  if(is_released) {
    m_bindings.clear();
  }
}

QRhiTexture* PathRenderer::get_texture(const PlanSource& source) {
  if(source.m_texture >= 0) {
    return m_surfaces[m_assignments[source.m_texture]].m_texture.get();
  } else if(source.m_image >= 0) {
    auto entry = m_images.find(m_plan.m_images[source.m_image].get());
    if(entry != m_images.end()) {
      return entry->second.m_texture.get();
    }
  }
  return m_blank.get();
}

QRhiShaderResourceBindings& PathRenderer::get_bindings(
    QRhiTexture* source, QRhiTexture* backdrop, PlanPipeline pipeline) {
  auto& bindings = m_bindings[std::tuple(source, backdrop, pipeline)];
  if(bindings) {
    return *bindings;
  }
  bindings.reset(m_rhi->newShaderResourceBindings());
  bindings->setBindings({
    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
      QRhiShaderResourceBinding::VertexStage |
        QRhiShaderResourceBinding::FragmentStage, m_uniforms.get(),
      UNIFORMS_SIZE),
    QRhiShaderResourceBinding::sampledTexture(1,
      QRhiShaderResourceBinding::FragmentStage, m_atlas.get(),
      m_nearest.get()),
    QRhiShaderResourceBinding::sampledTexture(2,
      QRhiShaderResourceBinding::VertexStage, m_paints.get(),
      m_nearest.get()),
    QRhiShaderResourceBinding::sampledTexture(3,
      QRhiShaderResourceBinding::FragmentStage, m_ramps.get(),
      m_linear.get()),
    QRhiShaderResourceBinding::sampledTexture(4,
      QRhiShaderResourceBinding::FragmentStage, source, m_linear.get()),
    QRhiShaderResourceBinding::sampledTexture(5,
      QRhiShaderResourceBinding::FragmentStage, backdrop, m_linear.get())});
  create(*bindings);
  return *bindings;
}

QRhiGraphicsPipeline& PathRenderer::get_pipeline(
    PlanPipeline pipeline, bool is_external) {
  if(is_external) {
    if(pipeline == PlanPipeline::SOLID) {
      return *m_solid_pipeline;
    }
    return *m_pipeline;
  } else if(pipeline == PlanPipeline::SOLID) {
    return *m_solid;
  } else if(pipeline == PlanPipeline::ATOP) {
    return *m_atop;
  } else if(pipeline == PlanPipeline::BLEND) {
    return *m_blend;
  } else if(pipeline == PlanPipeline::BLUR) {
    return *m_blur;
  }
  return *m_over;
}

void PathRenderer::record(
    QRhiCommandBuffer& buffer, const PlanPass& pass, bool is_external) {
  auto size = m_size;
  if(!is_external) {
    size = m_surfaces[m_assignments[pass.m_target]].m_size;
  }
  auto offset = QRhiCommandBuffer::DynamicOffset(
    0, static_cast<quint32>(pass.m_uniforms * m_uniform_stride));
  auto current = static_cast<QRhiGraphicsPipeline*>(nullptr);
  for(auto& draw : pass.m_draws) {
    auto& pipeline = get_pipeline(draw.m_pipeline, is_external);
    if(&pipeline != current) {
      buffer.setGraphicsPipeline(&pipeline);
      buffer.setViewport(QRhiViewport(0, 0,
        static_cast<float>(size.width()), static_cast<float>(size.height())));
      current = &pipeline;
    }
    auto backdrop = m_blank.get();
    if(draw.m_backdrop >= 0) {
      backdrop = m_surfaces[m_assignments[draw.m_backdrop]].m_texture.get();
    }
    buffer.setShaderResources(
      &get_bindings(get_texture(draw.m_source), backdrop, draw.m_pipeline),
      1, &offset);
    const auto inputs = std::array<QRhiCommandBuffer::VertexInput, 2>({
      QRhiCommandBuffer::VertexInput(m_corners.get(), 0),
      QRhiCommandBuffer::VertexInput(m_strips.get(),
        static_cast<quint32>(draw.m_first * sizeof(Strip)))});
    buffer.setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
    buffer.draw(4, draw.m_count);
  }
}

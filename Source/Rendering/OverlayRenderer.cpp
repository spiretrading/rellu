#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>
#include <QMatrix4x4>
#include "Rellu/Rendering/OverlayRenderer.hpp"
#include "Rellu/Rendering/Shaders.hpp"

using namespace Rellu;

namespace {
  const auto CORNERS = std::array<float, 8>({0, 0, 1, 0, 0, 1, 1, 1});
  constexpr auto VERTEX_FLOATS = std::size_t(16);
  constexpr auto VERTICES_PER_SHAPE = std::size_t(6);
  constexpr auto PROJECTION_SIZE = 16 * sizeof(float);
  constexpr auto IMAGE_UNIFORMS_SIZE = 24 * sizeof(float);
  constexpr auto MINIMUM_SHAPES = std::size_t(64);
  constexpr auto MINIMUM_IMAGES = std::size_t(16);
  constexpr auto RIGHT_ANGLE_TOLERANCE = 1e-9;

  void create(auto& resource) {
    if(!resource.create()) {
      throw std::runtime_error("Unable to create a graphics resource.");
    }
  }

  QRhiGraphicsPipeline::TargetBlend make_blend() {
    auto blend = QRhiGraphicsPipeline::TargetBlend();
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    return blend;
  }

  std::array<float, 4> premultiply(Color color) {
    auto alpha = static_cast<float>(std::clamp(color.m_alpha, 0.0, 1.0));
    return std::array<float, 4>({
      static_cast<float>(color.m_red) * alpha,
      static_cast<float>(color.m_green) * alpha,
      static_cast<float>(color.m_blue) * alpha, alpha});
  }

  struct Placement {
    double m_x;
    double m_y;
    double m_half_width;
    double m_half_height;
    double m_cosine;
    double m_sine;
  };

  Placement place(const OverlayShape& shape, double scale) {
    auto quarter = std::numbers::pi / 2;
    auto turns = std::round(shape.m_angle / quarter);
    if(std::abs(shape.m_angle - turns * quarter) <= RIGHT_ANGLE_TOLERANCE) {
      auto width = shape.m_size.m_width;
      auto height = shape.m_size.m_height;
      if(static_cast<long long>(turns) % 2 != 0) {
        std::swap(width, height);
      }
      auto snap = [&] (double center, double extent) {
        auto low = std::round((center - extent / 2) * scale);
        auto high = std::round((center + extent / 2) * scale);
        if(high <= low) {
          high = low + 1;
        }
        return std::pair(low, high);
      };
      auto [left, right] = snap(shape.m_center.m_x, width);
      auto [top, bottom] = snap(shape.m_center.m_y, height);
      return Placement((left + right) / 2, (top + bottom) / 2,
        (right - left) / 2, (bottom - top) / 2, 1, 0);
    }
    return Placement(shape.m_center.m_x * scale, shape.m_center.m_y * scale,
      shape.m_size.m_width * scale / 2, shape.m_size.m_height * scale / 2,
      std::cos(shape.m_angle), std::sin(shape.m_angle));
  }

  void add_shape(std::vector<float>& vertices, const OverlayShape& shape,
      double scale) {
    auto placement = place(shape, scale);
    auto radius = std::clamp(shape.m_radius * scale, 0.0,
      std::min(placement.m_half_width, placement.m_half_height));
    auto stroke = 0.0;
    if(shape.m_stroke_width > 0) {
      stroke = std::max(1.0, std::round(shape.m_stroke_width * scale));
    }
    auto fill = premultiply(shape.m_fill);
    auto outline = premultiply(shape.m_stroke);
    auto extent_x = placement.m_half_width + 1;
    auto extent_y = placement.m_half_height + 1;
    auto add_vertex = [&] (double x, double y) {
      auto position_x = placement.m_x + placement.m_cosine * x -
        placement.m_sine * y;
      auto position_y = placement.m_y + placement.m_sine * x +
        placement.m_cosine * y;
      auto vertex = std::array<float, VERTEX_FLOATS>({
        static_cast<float>(position_x), static_cast<float>(position_y),
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(placement.m_half_width),
        static_cast<float>(placement.m_half_height),
        static_cast<float>(radius), static_cast<float>(stroke),
        fill[0], fill[1], fill[2], fill[3],
        outline[0], outline[1], outline[2], outline[3]});
      vertices.insert(vertices.end(), vertex.begin(), vertex.end());
    };
    add_vertex(-extent_x, -extent_y);
    add_vertex(extent_x, -extent_y);
    add_vertex(-extent_x, extent_y);
    add_vertex(-extent_x, extent_y);
    add_vertex(extent_x, -extent_y);
    add_vertex(extent_x, extent_y);
  }
}

OverlayRenderer::OverlayRenderer()
  : m_rhi(nullptr),
    m_texture(nullptr),
    m_native(0),
    m_format(QRhiTexture::UnknownFormat),
    m_vertex_capacity(0),
    m_image_capacity(0),
    m_uniform_stride(0),
    m_is_corners_uploaded(false) {}

OverlayRenderer::~OverlayRenderer() {
  release();
}

void OverlayRenderer::initialize(QRhi& rhi) {
  release();
  m_rhi = &rhi;
  m_uniform_stride =
    static_cast<std::uint32_t>(rhi.ubufAligned(IMAGE_UNIFORMS_SIZE));
  m_uniforms.reset(rhi.newBuffer(
    QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, PROJECTION_SIZE));
  create(*m_uniforms);
  m_corners.reset(rhi.newBuffer(QRhiBuffer::Immutable,
    QRhiBuffer::VertexBuffer, sizeof(CORNERS)));
  create(*m_corners);
  m_sampler.reset(rhi.newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
    QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  create(*m_sampler);
  m_blank.reset(rhi.newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
  create(*m_blank);
  m_layout_uniforms.reset(rhi.newBuffer(QRhiBuffer::Dynamic,
    QRhiBuffer::UniformBuffer, m_uniform_stride));
  create(*m_layout_uniforms);
  m_image_layout.reset(rhi.newShaderResourceBindings());
  m_image_layout->setBindings({
    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
      QRhiShaderResourceBinding::VertexStage, m_layout_uniforms.get(),
      IMAGE_UNIFORMS_SIZE),
    QRhiShaderResourceBinding::sampledTexture(1,
      QRhiShaderResourceBinding::FragmentStage, m_blank.get(),
      m_sampler.get())});
  create(*m_image_layout);
  reserve_images(MINIMUM_IMAGES);
  m_shape_bindings.reset(rhi.newShaderResourceBindings());
  m_shape_bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0,
    QRhiShaderResourceBinding::VertexStage, m_uniforms.get())});
  create(*m_shape_bindings);
}

void OverlayRenderer::release() {
  m_images.clear();
  m_image_pipeline.reset();
  m_shape_pipeline.reset();
  m_image_layout.reset();
  m_shape_bindings.reset();
  m_target.reset();
  m_descriptor.reset();
  m_blank.reset();
  m_sampler.reset();
  m_image_uniforms.reset();
  m_layout_uniforms.reset();
  m_corners.reset();
  m_uniforms.reset();
  m_vertices.reset();
  m_vertex_capacity = 0;
  m_image_capacity = 0;
  m_texture = nullptr;
  m_native = 0;
  m_size = QSize();
  m_format = QRhiTexture::UnknownFormat;
  m_is_corners_uploaded = false;
  m_rhi = nullptr;
}

void OverlayRenderer::render(QRhiCommandBuffer& buffer,
    QRhiRenderTarget& target, double scale, const CanvasOverlay& overlay) {
  if(!m_rhi || overlay.is_empty() || !prepare_target(target)) {
    return;
  }
  auto size = m_size;
  auto vertices = std::vector<float>();
  vertices.reserve(
    overlay.m_shapes.size() * VERTICES_PER_SHAPE * VERTEX_FLOATS);
  for(auto& shape : overlay.m_shapes) {
    add_shape(vertices, shape, scale);
  }
  auto batch = m_rhi->nextResourceUpdateBatch();
  if(!m_is_corners_uploaded) {
    batch->uploadStaticBuffer(m_corners.get(), CORNERS.data());
    m_is_corners_uploaded = true;
  }
  auto projection = m_rhi->clipSpaceCorrMatrix();
  projection.ortho(0, static_cast<float>(size.width()),
    static_cast<float>(size.height()), 0, -1, 1);
  batch->updateDynamicBuffer(
    m_uniforms.get(), 0, PROJECTION_SIZE, projection.constData());
  auto byte_count = vertices.size() * sizeof(float);
  if(byte_count != 0 && (!m_vertices || m_vertex_capacity < byte_count)) {
    m_vertex_capacity = std::bit_ceil(std::max(byte_count,
      MINIMUM_SHAPES * VERTICES_PER_SHAPE * VERTEX_FLOATS * sizeof(float)));
    m_vertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
      QRhiBuffer::VertexBuffer, static_cast<quint32>(m_vertex_capacity)));
    create(*m_vertices);
  }
  if(byte_count != 0) {
    batch->updateDynamicBuffer(m_vertices.get(), 0,
      static_cast<quint32>(byte_count), vertices.data());
  }
  for(auto& entry : m_images) {
    entry.second.m_is_used = false;
  }
  auto images = std::vector<std::pair<ImageTexture*, QRect>>();
  for(auto& image : overlay.m_images) {
    if(!image.m_image || image.m_image->isNull()) {
      continue;
    }
    auto& entry = m_images[image.m_image->cacheKey()];
    if(!entry.m_texture || entry.m_image.expired()) {
      auto upload = image.m_image->convertToFormat(
        QImage::Format_RGBA8888_Premultiplied);
      entry.m_image = image.m_image;
      entry.m_texture.reset(m_rhi->newTexture(
        QRhiTexture::RGBA8, upload.size()));
      create(*entry.m_texture);
      batch->uploadTexture(entry.m_texture.get(), upload);
      entry.m_bindings.reset();
    }
    entry.m_is_used = true;
    auto position = QPoint(
      static_cast<int>(std::lround(image.m_position.m_x * scale)),
      static_cast<int>(std::lround(image.m_position.m_y * scale)));
    images.emplace_back(&entry, QRect(position, image.m_image->size()));
  }
  reserve_images(images.size());
  for(auto i = std::size_t(0); i != images.size(); ++i) {
    auto& [entry, rect] = images[i];
    if(!entry->m_bindings) {
      entry->m_bindings.reset(m_rhi->newShaderResourceBindings());
      entry->m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
          QRhiShaderResourceBinding::VertexStage, m_image_uniforms.get(),
          IMAGE_UNIFORMS_SIZE),
        QRhiShaderResourceBinding::sampledTexture(1,
          QRhiShaderResourceBinding::FragmentStage, entry->m_texture.get(),
          m_sampler.get())});
      create(*entry->m_bindings);
    }
    auto uniforms = std::array<float, 24>();
    std::copy(projection.constData(), projection.constData() + 16,
      uniforms.begin());
    uniforms[16] = static_cast<float>(rect.x());
    uniforms[17] = static_cast<float>(rect.y());
    uniforms[18] = static_cast<float>(rect.width());
    uniforms[19] = static_cast<float>(rect.height());
    uniforms[20] = 0;
    uniforms[21] = 0;
    uniforms[22] = 1;
    uniforms[23] = 1;
    batch->updateDynamicBuffer(m_image_uniforms.get(),
      static_cast<quint32>(i * m_uniform_stride), IMAGE_UNIFORMS_SIZE,
      uniforms.data());
  }
  std::erase_if(m_images, [] (const auto& entry) {
    return !entry.second.m_is_used;
  });
  buffer.beginPass(m_target.get(), QColor(0, 0, 0, 0),
    QRhiDepthStencilClearValue(1, 0), batch);
  if(!images.empty()) {
    buffer.setGraphicsPipeline(m_image_pipeline.get());
    buffer.setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()),
      static_cast<float>(size.height())));
    auto input = QRhiCommandBuffer::VertexInput(m_corners.get(), 0);
    for(auto i = std::size_t(0); i != images.size(); ++i) {
      auto offset = QRhiCommandBuffer::DynamicOffset(
        0, static_cast<quint32>(i * m_uniform_stride));
      buffer.setShaderResources(images[i].first->m_bindings.get(), 1, &offset);
      buffer.setVertexInput(0, 1, &input);
      buffer.draw(4);
    }
  }
  if(byte_count != 0) {
    buffer.setGraphicsPipeline(m_shape_pipeline.get());
    buffer.setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()),
      static_cast<float>(size.height())));
    buffer.setShaderResources(m_shape_bindings.get());
    auto input = QRhiCommandBuffer::VertexInput(m_vertices.get(), 0);
    buffer.setVertexInput(0, 1, &input);
    buffer.draw(static_cast<quint32>(vertices.size() / VERTEX_FLOATS));
  }
  buffer.endPass();
}

bool OverlayRenderer::prepare_target(QRhiRenderTarget& target) {
  if(target.resourceType() != QRhiResource::TextureRenderTarget) {
    return false;
  }
  auto description =
    static_cast<QRhiTextureRenderTarget&>(target).description();
  if(description.colorAttachmentCount() == 0) {
    return false;
  }
  auto attachment = *description.cbeginColorAttachments();
  auto texture = attachment.texture();
  if(!texture || texture->sampleCount() > 1) {
    texture = attachment.resolveTexture();
  }
  if(!texture || texture->sampleCount() > 1) {
    return false;
  }
  auto native = texture->nativeTexture().object;
  if(m_target && texture == m_texture && native == m_native &&
      texture->pixelSize() == m_size) {
    return true;
  }
  auto color = QRhiColorAttachment(texture);
  if(texture == attachment.resolveTexture()) {
    color.setLevel(attachment.resolveLevel());
    color.setLayer(attachment.resolveLayer());
  } else {
    color.setLevel(attachment.level());
    color.setLayer(attachment.layer());
  }
  auto rendering = std::unique_ptr<QRhiTextureRenderTarget>(
    m_rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(color),
      QRhiTextureRenderTarget::PreserveColorContents));
  auto descriptor = std::unique_ptr<QRhiRenderPassDescriptor>(
    rendering->newCompatibleRenderPassDescriptor());
  auto is_compatible = m_descriptor && texture->format() == m_format &&
    m_descriptor->isCompatible(descriptor.get());
  if(is_compatible) {
    rendering->setRenderPassDescriptor(m_descriptor.get());
  } else {
    rendering->setRenderPassDescriptor(descriptor.get());
  }
  create(*rendering);
  m_target = std::move(rendering);
  if(!is_compatible) {
    m_shape_pipeline.reset();
    m_image_pipeline.reset();
    m_descriptor = std::move(descriptor);
    m_format = texture->format();
    make_pipelines();
  }
  m_texture = texture;
  m_native = native;
  m_size = texture->pixelSize();
  return true;
}

void OverlayRenderer::make_pipelines() {
  m_shape_pipeline.reset(m_rhi->newGraphicsPipeline());
  m_shape_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
  m_shape_pipeline->setTargetBlends({make_blend()});
  m_shape_pipeline->setShaderStages({
    QRhiShaderStage(QRhiShaderStage::Vertex, load_shader("overlay.vert")),
    QRhiShaderStage(QRhiShaderStage::Fragment, load_shader("overlay.frag"))});
  auto shape_layout = QRhiVertexInputLayout();
  shape_layout.setBindings(
    {QRhiVertexInputBinding(VERTEX_FLOATS * sizeof(float))});
  shape_layout.setAttributes({
    QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
    QRhiVertexInputAttribute(
      0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)),
    QRhiVertexInputAttribute(
      0, 2, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)),
    QRhiVertexInputAttribute(
      0, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)),
    QRhiVertexInputAttribute(
      0, 4, QRhiVertexInputAttribute::Float4, 12 * sizeof(float))});
  m_shape_pipeline->setVertexInputLayout(shape_layout);
  m_shape_pipeline->setShaderResourceBindings(m_shape_bindings.get());
  m_shape_pipeline->setRenderPassDescriptor(m_descriptor.get());
  create(*m_shape_pipeline);
  m_image_pipeline.reset(m_rhi->newGraphicsPipeline());
  m_image_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
  m_image_pipeline->setTargetBlends({make_blend()});
  m_image_pipeline->setShaderStages({
    QRhiShaderStage(QRhiShaderStage::Vertex, load_shader("blit.vert")),
    QRhiShaderStage(QRhiShaderStage::Fragment, load_shader("blit.frag"))});
  auto image_layout = QRhiVertexInputLayout();
  image_layout.setBindings({QRhiVertexInputBinding(2 * sizeof(float))});
  image_layout.setAttributes({
    QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0)});
  m_image_pipeline->setVertexInputLayout(image_layout);
  m_image_pipeline->setShaderResourceBindings(m_image_layout.get());
  m_image_pipeline->setRenderPassDescriptor(m_descriptor.get());
  create(*m_image_pipeline);
}

void OverlayRenderer::reserve_images(std::size_t count) {
  if(m_image_uniforms && m_image_capacity >= count) {
    return;
  }
  auto capacity = std::bit_ceil(std::max(count, MINIMUM_IMAGES));
  m_image_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
    QRhiBuffer::UniformBuffer,
    static_cast<quint32>(capacity * m_uniform_stride)));
  create(*m_image_uniforms);
  m_image_capacity = capacity;
  for(auto& entry : m_images) {
    entry.second.m_bindings.reset();
  }
}

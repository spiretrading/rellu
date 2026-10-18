#include <utility>
#include "Rellu/Rendering/CanvasRenderer.hpp"

using namespace Rellu;

CanvasRenderer::CanvasRenderer()
  : m_background(CANVAS_BACKGROUND),
    m_rhi(nullptr),
    m_is_prepared(false),
    m_scale(0) {}

const std::shared_ptr<const DrawList>& CanvasRenderer::get_draw_list() const {
  return m_list;
}

void CanvasRenderer::set_draw_list(std::shared_ptr<const DrawList> list) {
  m_list = std::move(list);
  m_is_prepared = false;
}

const Matrix& CanvasRenderer::get_view() const {
  return m_view;
}

void CanvasRenderer::set_view(const Matrix& view) {
  m_view = view;
  m_is_prepared = false;
}

const QColor& CanvasRenderer::get_background() const {
  return m_background;
}

void CanvasRenderer::set_background(const QColor& background) {
  m_background = background;
}

void CanvasRenderer::initialize(
    QRhi& rhi, QRhiRenderPassDescriptor& descriptor, int sample_count) {
  m_rhi = &rhi;
  m_paths.initialize(rhi, descriptor, sample_count);
  m_is_prepared = false;
}

void CanvasRenderer::release() {
  m_paths.release();
  m_rhi = nullptr;
  m_is_prepared = false;
}

void CanvasRenderer::render(
    QRhiCommandBuffer& buffer, QRhiRenderTarget& target, double scale) {
  auto size = target.pixelSize();
  auto batch = static_cast<QRhiResourceUpdateBatch*>(nullptr);
  if(!m_is_prepared || size != m_size || scale != m_scale) {
    static const auto EMPTY = DrawList();
    batch = m_rhi->nextResourceUpdateBatch();
    m_paths.prepare(*batch, m_list ? *m_list : EMPTY,
      make_scale(scale, scale) * m_view, size);
    m_is_prepared = true;
    m_size = size;
    m_scale = scale;
  }
  buffer.beginPass(
    &target, m_background, QRhiDepthStencilClearValue(1, 0), batch);
  m_paths.draw(buffer);
  buffer.endPass();
}

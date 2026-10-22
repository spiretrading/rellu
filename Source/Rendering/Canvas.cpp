#include "Rellu/Rendering/Canvas.hpp"

using namespace Rellu;

Canvas::Canvas(QWidget* parent)
  : QRhiWidget(parent),
    m_rhi(nullptr) {}

CanvasRenderer& Canvas::get_renderer() {
  return m_renderer;
}

Rect Canvas::get_view_region() const {
  if(m_view_region) {
    return *m_view_region;
  }
  return Rect(0, 0, width(), height());
}

void Canvas::set_view_region(const std::optional<Rect>& region) {
  m_view_region = region;
}

void Canvas::initialize(QRhiCommandBuffer* buffer) {
  if(m_rhi == rhi()) {
    return;
  }
  m_renderer.release();
  m_rhi = rhi();
  m_renderer.initialize(
    *m_rhi, *renderTarget()->renderPassDescriptor(), sampleCount());
}

void Canvas::render(QRhiCommandBuffer* buffer) {
  m_renderer.render(*buffer, *renderTarget(), devicePixelRatio());
  if(!m_renderer.is_complete()) {
    update();
  }
}

void Canvas::releaseResources() {
  m_renderer.release();
  m_rhi = nullptr;
}

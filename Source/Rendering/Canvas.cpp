#include "Rellu/Rendering/Canvas.hpp"

using namespace Rellu;

Canvas::Canvas(QWidget* parent)
  : QRhiWidget(parent),
    m_rhi(nullptr) {}

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
}

void Canvas::releaseResources() {
  m_renderer.release();
  m_rhi = nullptr;
}

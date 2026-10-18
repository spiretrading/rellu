#include "Rellu/Rendering/CanvasRenderer.hpp"

using namespace Rellu;

void CanvasRenderer::initialize(
  QRhi& rhi, QRhiRenderPassDescriptor& descriptor, int sample_count) {}

void CanvasRenderer::release() {}

void CanvasRenderer::render(
    QRhiCommandBuffer& buffer, QRhiRenderTarget& target, double scale) {
  buffer.beginPass(
    &target, CANVAS_BACKGROUND, QRhiDepthStencilClearValue(1, 0));
  buffer.endPass();
}

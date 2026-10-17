#include <rhi/qrhi.h>
#include "Gesso/Canvas.hpp"

using namespace Gesso;

Canvas::Canvas(QWidget* parent)
  : QRhiWidget(parent) {}

void Canvas::render(QRhiCommandBuffer* buffer) {
  buffer->beginPass(
    renderTarget(), QColor(232, 232, 232), QRhiDepthStencilClearValue(1, 0));
  buffer->endPass();
}

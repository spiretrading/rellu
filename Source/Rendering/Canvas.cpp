#include <rhi/qrhi.h>
#include "Rellu/Rendering/Canvas.hpp"

using namespace Rellu;

Canvas::Canvas(QWidget* parent)
  : QRhiWidget(parent) {}

void Canvas::render(QRhiCommandBuffer* buffer) {
  buffer->beginPass(
    renderTarget(), QColor(232, 232, 232), QRhiDepthStencilClearValue(1, 0));
  buffer->endPass();
}

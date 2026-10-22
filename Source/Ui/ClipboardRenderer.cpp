#include <cmath>
#include <exception>
#include <QColor>
#include <QPainter>
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"
#include "Rellu/Ui/ClipboardRenderer.hpp"

using namespace Rellu;

struct ClipboardRenderer::State {
  CanvasRenderer m_renderer;
  OffscreenRenderer m_offscreen;

  State()
      : m_offscreen(m_renderer) {
    m_renderer.set_background(QColor(0, 0, 0, 0));
  }
};

ClipboardRenderer::ClipboardRenderer() = default;

ClipboardRenderer::~ClipboardRenderer() = default;

QImage ClipboardRenderer::render(const Document& document,
    const std::vector<NodeId>& nodes, const Rect& bounds) {
  auto size = QSize(static_cast<int>(std::ceil(bounds.m_width - 0.01)),
    static_cast<int>(std::ceil(bounds.m_height - 0.01)));
  if(size.isEmpty()) {
    return QImage();
  }
  try {
    if(!m_state) {
      m_state = std::make_unique<State>();
    }
    auto image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    auto painter = QPainter(&image);
    auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE);
    builder.set_device_transform(make_translation(-bounds.m_x, -bounds.m_y));
    for(auto& id : nodes) {
      m_state->m_renderer.set_draw_list(
        std::make_shared<DrawList>(builder.build(id)));
      m_state->m_renderer.set_view(
        make_translation(-bounds.m_x, -bounds.m_y));
      auto layer = m_state->m_offscreen.render(size, 1);
      m_state->m_renderer.set_draw_list(nullptr);
      layer.setDevicePixelRatio(1);
      painter.drawImage(0, 0, layer);
    }
    painter.end();
    return image;
  } catch(const std::exception&) {
    m_state.reset();
    return QImage();
  }
}

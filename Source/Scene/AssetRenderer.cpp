#include <cmath>
#include <QColor>
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/AssetRenderer.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

using namespace Rellu;

struct AssetRenderer::State {
  SceneBuilder m_builder;
  CanvasRenderer m_renderer;
  OffscreenRenderer m_offscreen;

  explicit State(const Document& document)
      : m_builder(document, StripRasterizer::TOLERANCE),
        m_offscreen(m_renderer) {
    m_renderer.set_background(QColor(0, 0, 0, 0));
  }
};

QSize Rellu::get_asset_size(const Rect& bounds, double scale) {
  return QSize(static_cast<int>(std::ceil(bounds.m_width * scale - 0.01)),
    static_cast<int>(std::ceil(bounds.m_height * scale - 0.01)));
}

AssetRenderer::AssetRenderer(const Document& document)
  : m_document(&document) {}

AssetRenderer::~AssetRenderer() = default;

QImage AssetRenderer::render(
    const NodeId& id, const Rect& bounds, double scale) {
  auto size = get_asset_size(bounds, scale);
  if(size.isEmpty()) {
    return QImage();
  }
  if(!m_state) {
    m_state = std::make_unique<State>(*m_document);
  }
  auto device = make_scale(scale, scale) *
    make_translation(-bounds.m_x, -bounds.m_y);
  m_state->m_builder.set_tolerance(StripRasterizer::TOLERANCE / scale);
  m_state->m_builder.set_device_transform(device);
  m_state->m_renderer.set_draw_list(
    std::make_shared<DrawList>(m_state->m_builder.build(id)));
  m_state->m_renderer.set_view(make_translation(-bounds.m_x, -bounds.m_y));
  auto image = m_state->m_offscreen.render(size, scale);
  m_state->m_renderer.set_draw_list(nullptr);
  image.setDevicePixelRatio(1);
  return image;
}

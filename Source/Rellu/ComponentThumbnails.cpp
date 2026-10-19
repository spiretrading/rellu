#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"
#include "ComponentThumbnails.hpp"

using namespace Rellu;

ComponentThumbnails::ComponentThumbnails() {
  m_renderer.set_background(QColor(0, 0, 0, 0));
}

ComponentThumbnails::~ComponentThumbnails() = default;

QImage ComponentThumbnails::render(
    const Document& document, const ComponentId& component, int size) {
  auto& components = document.get_resources().m_components;
  auto entry = components.find(component);
  if(entry == components.end() || size <= 0) {
    return QImage();
  }
  auto& master = entry->second.m_master;
  auto bounds = get_world_bounds(document, master, BoundsType::VISUAL);
  if(!bounds || !(bounds->m_width > 0) || !(bounds->m_height > 0)) {
    return QImage();
  }
  auto scale = size / std::max(bounds->m_width, bounds->m_height);
  auto pixels = QSize(
    std::max(1, static_cast<int>(std::ceil(bounds->m_width * scale))),
    std::max(1, static_cast<int>(std::ceil(bounds->m_height * scale))));
  try {
    if(!m_offscreen) {
      m_offscreen = std::make_unique<OffscreenRenderer>(m_renderer);
    }
    auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE / scale);
    builder.set_device_transform(make_scale(scale, scale) *
      make_translation(-bounds->m_x, -bounds->m_y));
    m_renderer.set_draw_list(std::make_shared<DrawList>(builder.build(master)));
    m_renderer.set_view(make_translation(-bounds->m_x, -bounds->m_y));
    auto image = m_offscreen->render(pixels, scale);
    image.setDevicePixelRatio(1);
    return image;
  } catch(const std::exception&) {
    return QImage();
  }
}

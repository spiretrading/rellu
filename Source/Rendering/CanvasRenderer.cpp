#include <stdexcept>
#include <utility>
#include "Rellu/Rendering/CanvasRenderer.hpp"

using namespace Rellu;

namespace {
  bool is_tileable(const Matrix& view) {
    return view.m_b == 0 && view.m_c == 0 && view.m_a == view.m_d &&
      view.m_a > 0;
  }
}

CanvasRenderer::CanvasRenderer()
  : m_background(CANVAS_BACKGROUND),
    m_rhi(nullptr),
    m_descriptor(nullptr),
    m_sample_count(1),
    m_is_initialized(false),
    m_is_prepared(false),
    m_is_complete(true),
    m_scale(0) {}

const std::shared_ptr<const DrawList>& CanvasRenderer::get_draw_list() const {
  return m_list;
}

void CanvasRenderer::set_draw_list(std::shared_ptr<const DrawList> list) {
  m_list = std::move(list);
  m_drawing = nullptr;
  m_list_drawing = nullptr;
  m_tiles.clear();
  m_is_prepared = false;
}

const std::shared_ptr<const Drawing>& CanvasRenderer::get_drawing() const {
  return m_drawing;
}

void CanvasRenderer::set_drawing(std::shared_ptr<const Drawing> drawing) {
  m_drawing = std::move(drawing);
  m_list = nullptr;
  m_list_drawing = nullptr;
  m_tiles.clear();
  m_is_prepared = false;
}

void CanvasRenderer::invalidate(const std::vector<DrawRegion>& regions) {
  if(regions.empty()) {
    return;
  }
  m_tiles.invalidate(regions);
  m_is_prepared = false;
}

TileCache& CanvasRenderer::get_tiles() {
  return m_tiles;
}

bool CanvasRenderer::is_complete() const {
  return m_is_complete;
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

const CanvasOverlay& CanvasRenderer::get_overlay() const {
  return m_overlay;
}

void CanvasRenderer::set_overlay(CanvasOverlay overlay) {
  m_overlay = std::move(overlay);
}

void CanvasRenderer::initialize(
    QRhi& rhi, QRhiRenderPassDescriptor& descriptor, int sample_count) {
  m_rhi = &rhi;
  m_descriptor = &descriptor;
  m_sample_count = sample_count;
  m_is_initialized = false;
  m_tiles.initialize(rhi, descriptor, sample_count);
  m_overlays.initialize(rhi);
  m_is_prepared = false;
}

void CanvasRenderer::release() {
  m_overlays.release();
  m_tiles.release();
  m_paths.release();
  m_is_initialized = false;
  m_descriptor = nullptr;
  m_rhi = nullptr;
  m_is_prepared = false;
}

void CanvasRenderer::render(
    QRhiCommandBuffer& buffer, QRhiRenderTarget& target, double scale) {
  auto size = target.pixelSize();
  auto view = make_scale(scale, scale) * m_view;
  if(m_drawing && is_tileable(view)) {
    m_tiles.render(buffer, target, *m_drawing, view, m_background);
    m_is_complete = m_tiles.is_complete();
    m_is_prepared = false;
    m_overlays.render(buffer, target, scale, m_overlay);
    return;
  }
  m_is_complete = true;
  auto batch = static_cast<QRhiResourceUpdateBatch*>(nullptr);
  if(!m_is_prepared || size != m_size || scale != m_scale) {
    m_commands.clear();
    if(m_drawing) {
      for(auto& list : m_drawing->get_lists()) {
        for(auto& command : *list) {
          m_commands.push_back(&command);
        }
      }
    } else if(m_list) {
      m_commands = to_sequence(*m_list);
    }
    batch = m_rhi->nextResourceUpdateBatch();
    get_paths().prepare(*batch, m_commands, view, size);
    m_is_prepared = true;
    m_size = size;
    m_scale = scale;
  }
  get_paths().render(buffer, target, m_background, batch);
  m_overlays.render(buffer, target, scale, m_overlay);
}

void CanvasRenderer::render(QRhiCommandBuffer& buffer, QRhiTexture& texture,
    double scale, QSize size, const QRect& region) {
  auto view = make_scale(scale, scale) * m_view;
  if(m_drawing || !is_tileable(view)) {
    throw std::invalid_argument("Only draw lists record regions.");
  }
  if(!m_list_drawing) {
    m_list_drawing = std::make_unique<Drawing>();
    if(m_list) {
      m_list_drawing->set_lists({m_list});
    }
  }
  auto bounds = Rect((region.x() - view.m_tx) / view.m_a,
    (region.y() - view.m_ty) / view.m_a, region.width() / view.m_a,
    region.height() / view.m_a);
  auto area = m_list_drawing->query(
    inflate(bounds, 1 / view.m_a), view.m_a, m_commands);
  auto extent = (to_pixels(DrawRegion(area, 1), view) &
    QRect(QPoint(0, 0), size)) | region;
  auto batch = m_rhi->nextResourceUpdateBatch();
  get_paths().prepare(*batch, m_commands, view, size, extent);
  m_paths.render(buffer, m_background, batch);
  m_is_prepared = false;
  m_is_complete = true;
  auto& source = m_paths.get_region_texture();
  auto origin = region.topLeft() - extent.topLeft();
  auto destination = QPoint(0, 0);
  if(m_rhi->isYUpInFramebuffer()) {
    origin.setY(source.pixelSize().height() - origin.y() - region.height());
    destination.setY(texture.pixelSize().height() - region.height());
  }
  auto description = QRhiTextureCopyDescription();
  description.setPixelSize(region.size());
  description.setSourceTopLeft(origin);
  description.setDestinationTopLeft(destination);
  auto copies = m_rhi->nextResourceUpdateBatch();
  copies->copyTexture(&texture, &source, description);
  buffer.resourceUpdate(copies);
}

PathRenderer& CanvasRenderer::get_paths() {
  if(!m_is_initialized) {
    m_paths.initialize(*m_rhi, *m_descriptor, m_sample_count);
    m_is_initialized = true;
  }
  return m_paths;
}

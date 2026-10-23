#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <QColor>
#include "Rellu/Io/XdPreview.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

using namespace Rellu;

namespace {
  double get_scale(const Rect& bounds, int limit) {
    auto extent = std::max(bounds.m_width, bounds.m_height);
    if(!(extent > 0)) {
      return 1;
    }
    return std::min(1.0, limit / extent);
  }

  const Node* find_top_left(const std::vector<const Node*>& artboards) {
    auto top = static_cast<const Node*>(nullptr);
    for(auto node : artboards) {
      auto& frame = node->get_artboard()->m_frame;
      if(!top) {
        top = node;
        continue;
      }
      auto& best = top->get_artboard()->m_frame;
      if(frame.m_y < best.m_y ||
          (frame.m_y == best.m_y && frame.m_x < best.m_x)) {
        top = node;
      }
    }
    return top;
  }

  QImage render(SceneBuilder& builder, CanvasRenderer& renderer,
      OffscreenRenderer& offscreen, const XdPreviewSource& source,
      int limit) {
    auto size = get_xd_preview_size(source.m_bounds, limit);
    if(size.isEmpty()) {
      return QImage();
    }
    auto scale = get_scale(source.m_bounds, limit);
    auto& bounds = source.m_bounds;
    builder.set_tolerance(StripRasterizer::TOLERANCE / scale);
    builder.set_device_transform(make_scale(scale, scale) *
      make_translation(-bounds.m_x, -bounds.m_y));
    renderer.set_draw_list(std::make_shared<DrawList>(
      source.m_artboard.is_nil() ? builder.build() :
        builder.build(source.m_artboard)));
    renderer.set_view(make_translation(-bounds.m_x, -bounds.m_y));
    auto image = offscreen.render(size, scale);
    renderer.set_draw_list(nullptr);
    image.setDevicePixelRatio(1);
    return image.convertToFormat(QImage::Format_ARGB32);
  }
}

std::optional<XdPreviewSource> Rellu::get_xd_preview_source(
    const Document& document, const TextMeasurer& measurer) {
  auto artboards = std::vector<const Node*>();
  auto roots = std::vector<const Node*>();
  for(auto& id : document.get_canvas()) {
    auto& node = document.get(id);
    if(node.get_artboard()) {
      artboards.push_back(&node);
      if(std::any_of(document.get_prototype().m_flows.begin(),
          document.get_prototype().m_flows.end(), [&] (const auto& flow) {
            return flow.m_artboard == id;
          })) {
        roots.push_back(&node);
      }
    }
  }
  if(auto top = find_top_left(roots.empty() ? artboards : roots)) {
    return XdPreviewSource(top->get_id(), top->get_artboard()->m_frame);
  }
  auto bounds = get_world_bounds(
    document, document.get_canvas(), BoundsType::DRAW, measurer);
  if(!bounds || !(bounds->m_width > 0) || !(bounds->m_height > 0)) {
    return std::nullopt;
  }
  return XdPreviewSource(NodeId(), *bounds);
}

QSize Rellu::get_xd_preview_size(const Rect& bounds, int limit) {
  auto scale = get_scale(bounds, limit);
  return QSize(
    std::max(1, static_cast<int>(std::round(bounds.m_width * scale))),
    std::max(1, static_cast<int>(std::round(bounds.m_height * scale))));
}

XdPreviews Rellu::render_xd_previews(
    const Document& document, const TextMeasurer& measurer) {
  auto source = get_xd_preview_source(document, measurer);
  if(source) {
    try {
      auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE);
      auto renderer = CanvasRenderer();
      renderer.set_background(QColor(0xFF, 0xFF, 0xFF));
      auto offscreen = OffscreenRenderer(renderer);
      auto previews = XdPreviews();
      previews.m_preview =
        render(builder, renderer, offscreen, *source, XD_PREVIEW_SIZE);
      if(get_xd_preview_size(source->m_bounds, XD_PREVIEW_SIZE) ==
          get_xd_preview_size(source->m_bounds, XD_THUMBNAIL_SIZE)) {
        previews.m_thumbnail = previews.m_preview;
      } else {
        previews.m_thumbnail =
          render(builder, renderer, offscreen, *source, XD_THUMBNAIL_SIZE);
      }
      if(!previews.m_preview.isNull() && !previews.m_thumbnail.isNull()) {
        return previews;
      }
    } catch(const std::exception&) {
    }
  }
  auto blank = QImage(
    XD_BLANK_PREVIEW_SIZE, XD_BLANK_PREVIEW_SIZE, QImage::Format_ARGB32);
  blank.fill(QColor(0xFF, 0xFF, 0xFF));
  return XdPreviews(blank, blank);
}

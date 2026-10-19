#include <utility>
#include "Rellu/Prototype/PreviewComposer.hpp"

using namespace Rellu;

namespace {
  Rect move(const Rect& rect, Point offset) {
    return Rect(rect.m_x + offset.m_x, rect.m_y + offset.m_y, rect.m_width,
      rect.m_height);
  }

  Rect clip_to(const std::optional<Rect>& rect, Point offset,
      const Rect& clip) {
    if(!rect) {
      return clip;
    }
    return intersect(move(*rect, offset), clip);
  }
}

void Rellu::translate(const DrawList& list, Point offset, const Rect& clip,
    DrawList& out) {
  auto translation = make_translation(offset.m_x, offset.m_y);
  for(auto& command : list) {
    if(auto fill = std::get_if<FillCommand>(&command)) {
      auto moved = *fill;
      moved.m_transform = translation * moved.m_transform;
      moved.m_clip = clip_to(moved.m_clip, offset, clip);
      out.push_back(std::move(moved));
    } else if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
      auto moved = *layer;
      if(moved.m_mask) {
        moved.m_mask->m_transform = translation * moved.m_mask->m_transform;
      }
      moved.m_clip = clip_to(moved.m_clip, offset, clip);
      out.push_back(std::move(moved));
    } else if(auto blur = std::get_if<BackdropBlurCommand>(&command)) {
      auto moved = *blur;
      moved.m_transform = translation * moved.m_transform;
      moved.m_clip = clip_to(moved.m_clip, offset, clip);
      out.push_back(std::move(moved));
    } else {
      out.push_back(command);
    }
  }
}

PreviewComposer::PreviewComposer(Document& stage, double tolerance)
    : m_stage(&stage),
      m_builder(stage, tolerance) {
  m_subscription = stage.subscribe([this] (const auto& changes) {
    for(auto& id : changes.m_inserted) {
      m_builder.invalidate(id);
    }
    for(auto& id : changes.m_moved) {
      m_builder.invalidate(id);
    }
    for(auto& change : changes.m_modified) {
      m_builder.invalidate(change.m_id);
    }
    for(auto& id : changes.m_removed) {
      m_builder.invalidate(id);
    }
    for(auto& id : changes.m_images) {
      m_builder.invalidate(id);
    }
  });
}

void PreviewComposer::set_tolerance(double tolerance) {
  m_builder.set_tolerance(tolerance);
}

std::shared_ptr<const DrawList> PreviewComposer::compose(
    const std::vector<PreviewLayer>& layers) {
  auto frame = DrawList();
  for(auto& layer : layers) {
    auto node = m_stage->find(layer.m_artboard);
    if(!node || !node->get_artboard() || !(layer.m_opacity > 0)) {
      continue;
    }
    auto corner = node->get_artboard()->m_frame.get_top_left();
    auto clip = Rect(layer.m_position.m_x, layer.m_position.m_y,
      layer.m_size.m_width, layer.m_size.m_height);
    auto is_layered = layer.m_opacity < 1;
    if(is_layered) {
      auto begin = BeginLayerCommand();
      begin.m_opacity = layer.m_opacity;
      begin.m_clip = clip;
      frame.push_back(std::move(begin));
    }
    add(layer.m_artboard, Point(layer.m_position.m_x - corner.m_x,
      layer.m_position.m_y - corner.m_y - layer.m_scroll), clip, frame);
    if(!layer.m_fixed.is_nil()) {
      add(layer.m_fixed, Point(layer.m_position.m_x - corner.m_x,
        layer.m_position.m_y - corner.m_y), clip, frame);
    }
    if(is_layered) {
      frame.push_back(EndLayerCommand());
    }
  }
  return std::make_shared<const DrawList>(std::move(frame));
}

void PreviewComposer::add(const NodeId& artboard, Point offset,
    const Rect& clip, DrawList& frame) {
  auto dependencies = SceneDependencies();
  for(auto& list : m_builder.build_lists(artboard, dependencies)) {
    translate(*list, offset, clip, frame);
  }
}

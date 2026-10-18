#include "Rellu/Scene/SceneBuilder.hpp"
#include "Rellu/Text/FontLibrary.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_RASTERS = 4;

  std::optional<Rect> get_clip(const TextLayout& layout,
      const Matrix& transform, const std::optional<Rect>& parent) {
    if(!layout.m_clip) {
      return parent;
    }
    auto frame = transform.map(*layout.m_clip);
    if(parent) {
      return intersect(*parent, frame);
    }
    return frame;
  }

  Path make_level(const CoverageMask& mask, int level) {
    auto path = Path();
    for(auto row = 0; row != mask.m_height; ++row) {
      auto coverage = mask.m_coverage.data() + row * mask.m_width;
      auto column = 0;
      while(column != mask.m_width) {
        if(coverage[column] != level) {
          ++column;
          continue;
        }
        auto start = column;
        while(column != mask.m_width && coverage[column] == level) {
          ++column;
        }
        auto left = static_cast<double>(mask.m_left + start);
        auto right = static_cast<double>(mask.m_left + column);
        auto top = static_cast<double>(mask.m_top + row);
        path.move_to(Point(left, top));
        path.line_to(Point(right, top));
        path.line_to(Point(right, top + 1));
        path.line_to(Point(left, top + 1));
        path.close();
      }
    }
    return path;
  }
}

const std::optional<Matrix>& SceneBuilder::get_device_transform() const {
  return m_device_transform;
}

void SceneBuilder::set_device_transform(
    const std::optional<Matrix>& transform) {
  m_device_transform = transform;
}

void SceneBuilder::add_text(
    const Node& node, const Placement& placement, DrawList& list) {
  auto& outline = get_text_outline(node);
  auto clip = get_clip(outline.m_layout, placement.m_transform,
    placement.m_clip);
  if(m_device_transform) {
    auto inverse = invert(*m_device_transform);
    auto transform = *m_device_transform * placement.m_transform;
    if(inverse && TextRasterizer::is_supported(transform)) {
      for(auto& raster : get_rasters(outline, transform)) {
        for(auto level = 1; level <= 16; ++level) {
          auto path = make_level(raster.m_mask, level);
          if(path.is_empty()) {
            continue;
          }
          auto color = raster.m_color;
          color.m_alpha *=
            placement.m_opacity * get_text_opacity(level, raster.m_color);
          list.push_back(FillCommand(
            std::move(path), *inverse, SolidPaint(color), clip));
        }
      }
      return;
    }
  }
  for(auto& fill : outline.m_fills) {
    if(fill.m_path.is_empty()) {
      continue;
    }
    auto color = fill.m_color;
    color.m_alpha *= placement.m_opacity;
    list.push_back(FillCommand(
      fill.m_path, placement.m_transform, SolidPaint(color), clip));
  }
}

SceneBuilder::TextOutline& SceneBuilder::get_text_outline(const Node& node) {
  auto& outline = m_texts[node.get_id()];
  if(outline.m_record.get() != &node) {
    outline.m_record = m_document->get_nodes().get_record(node.get_id());
    outline.m_layout = lay_out(*node.get_text(), get_default_font_library());
    outline.m_fills = Rellu::outline(outline.m_layout);
    outline.m_rasters.clear();
  }
  outline.m_generation = m_generation;
  return outline;
}

const std::vector<TextRaster>& SceneBuilder::get_rasters(
    TextOutline& outline, const Matrix& transform) {
  for(auto& rasters : outline.m_rasters) {
    if(rasters.first == transform) {
      return rasters.second;
    }
  }
  if(outline.m_rasters.size() == MAXIMUM_RASTERS) {
    outline.m_rasters.erase(outline.m_rasters.begin());
  }
  return outline.m_rasters.emplace_back(transform,
    get_default_text_rasterizer().rasterize(outline.m_layout, transform)).
    second;
}

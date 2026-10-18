#include <cmath>
#include <variant>
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

  Matrix to_single(const Matrix& matrix) {
    auto round = [] (double value) {
      return static_cast<double>(static_cast<float>(value));
    };
    return Matrix(round(matrix.m_a), round(matrix.m_b), round(matrix.m_c),
      round(matrix.m_d), round(matrix.m_tx), round(matrix.m_ty));
  }

  double get_scale(const Matrix& matrix) {
    return std::sqrt(std::abs(matrix.get_determinant()));
  }

  Shadow to_shadow(
      Point offset, double blur, Color color, const Matrix& transform) {
    return Shadow(Point(transform.m_a * offset.m_x + transform.m_c * offset.m_y,
      transform.m_b * offset.m_x + transform.m_d * offset.m_y),
      blur * get_scale(transform), color);
  }

  void add_effects(DrawList& list, std::size_t first,
      const std::vector<Effect>& effects, const Matrix& transform,
      const std::optional<Rect>& clip) {
    if(first == list.size()) {
      return;
    }
    auto outer = BeginLayerCommand();
    outer.m_clip = clip;
    auto inner = outer;
    for(auto& effect : effects) {
      if(auto shadow = std::get_if<DropShadow>(&effect)) {
        if(shadow->m_is_visible && shadow->m_color.m_alpha > 0) {
          outer.m_drop_shadows.push_back(to_shadow(
            shadow->m_offset, shadow->m_blur, shadow->m_color, transform));
        }
      } else if(auto shadow = std::get_if<InnerShadow>(&effect)) {
        if(shadow->m_is_visible && shadow->m_color.m_alpha > 0) {
          inner.m_inner_shadows.push_back(to_shadow(
            shadow->m_offset, shadow->m_blur, shadow->m_color, transform));
        }
      } else if(auto blur = std::get_if<Blur>(&effect)) {
        if(blur->m_is_visible && !blur->m_is_background) {
          outer.m_blur = blur->m_amount * get_scale(transform);
        }
      }
    }
    if(!inner.m_inner_shadows.empty()) {
      list.insert(list.begin() + first, std::move(inner));
      list.push_back(EndLayerCommand());
    }
    if(!outer.m_drop_shadows.empty() || outer.m_blur > 0) {
      list.insert(list.begin() + first, std::move(outer));
      list.push_back(EndLayerCommand());
    }
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
  auto first = list.size();
  auto inverse = std::optional<Matrix>();
  auto transform = Matrix();
  if(m_device_transform) {
    inverse = invert(*m_device_transform);
    transform = *m_device_transform * to_single(placement.m_transform);
  }
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
        list.push_back(FillCommand(std::make_shared<const Path>(
          std::move(path)), *inverse, SolidPaint(color), clip));
      }
    }
    auto outlined = TextLayout();
    auto local = invert(transform);
    for(auto& glyph : outline.m_layout.m_glyphs) {
      if(TextRasterizer::is_outlined(glyph, transform)) {
        outlined.m_glyphs.push_back(glyph);
      } else if(!glyph.m_typeface->get_color_layers(glyph.m_index).empty()) {
        auto& colored = outlined.m_glyphs.emplace_back(glyph);
        colored.m_origin =
          local->map(TextRasterizer::get_origin(glyph, transform));
      }
    }
    for(auto& fill : Rellu::outline(outlined)) {
      auto color = fill.m_color;
      color.m_alpha *= placement.m_opacity;
      list.push_back(FillCommand(std::make_shared<const Path>(
        std::move(fill.m_path)), to_single(placement.m_transform),
        SolidPaint(color), clip));
    }
  } else {
    for(auto i = std::size_t(0); i != outline.m_fills.size(); ++i) {
      auto& fill = outline.m_fills[i];
      if(outline.m_paths[i]->is_empty()) {
        continue;
      }
      auto color = fill.m_color;
      color.m_alpha *= placement.m_opacity;
      list.push_back(FillCommand(outline.m_paths[i], placement.m_transform,
        SolidPaint(color), clip));
    }
  }
  add_effects(list, first, node.get_text()->m_style.m_effects,
    placement.m_transform, placement.m_clip);
}

SceneBuilder::TextOutline& SceneBuilder::get_text_outline(const Node& node) {
  auto& outline = m_texts[node.get_id()];
  if(outline.m_record.get() != &node) {
    outline.m_record = m_document->get_nodes().get_record(node.get_id());
    outline.m_layout = lay_out(*node.get_text(), get_default_font_library());
    outline.m_fills = Rellu::outline(outline.m_layout);
    outline.m_paths.clear();
    for(auto& fill : outline.m_fills) {
      outline.m_paths.push_back(
        std::make_shared<const Path>(std::move(fill.m_path)));
    }
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

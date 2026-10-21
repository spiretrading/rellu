#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>
#include <variant>
#include <vector>
#include "Rellu/Scene/SceneBuilder.hpp"
#include "Rellu/Text/FontLibrary.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_RASTERS = std::size_t(4);
  constexpr auto LEVELS = 16;

  bool is_similar(const Matrix& left, const Matrix& right) {
    return left.m_a == right.m_a && left.m_b == right.m_b &&
      left.m_c == right.m_c && left.m_d == right.m_d;
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

  void add_fills(const TextLayout& layout, const Matrix& transform,
      double opacity, const std::optional<Rect>& clip, DrawList& list) {
    for(auto& fill : outline(layout)) {
      if(fill.m_path.is_empty()) {
        continue;
      }
      auto color = fill.m_color;
      color.m_alpha *= opacity;
      list.push_back(FillCommand(std::make_shared<const Path>(
        std::move(fill.m_path)), transform, SolidPaint(color), clip));
    }
  }

  void add_glyphs(const TextLayout& layout, const Matrix& transform,
      double opacity, const std::optional<Rect>& clip, DrawList& list) {
    auto single = TextLayout();
    for(auto& glyph : layout.m_glyphs) {
      single.m_glyphs.assign(1, glyph);
      add_fills(single, transform, opacity, clip, list);
    }
    if(!layout.m_decorations.empty()) {
      single.m_glyphs.clear();
      single.m_decorations = layout.m_decorations;
      add_fills(single, transform, opacity, clip, list);
    }
  }

  struct Run {
    int m_left;
    int m_right;
    int m_level;
    int m_top;
  };

  struct Block {
    int m_left;
    int m_top;
    int m_right;
    int m_bottom;
  };

  struct Coverage {
    Path m_path;
    Rect m_bounds;
  };

  std::array<Coverage, LEVELS + 1> make_levels(const CoverageMask& mask) {
    auto blocks = std::array<std::vector<Block>, LEVELS + 1>();
    auto open = std::vector<Run>();
    auto next = std::vector<Run>();
    auto finish = [&] (const Run& run, int bottom) {
      blocks[run.m_level].push_back(
        Block(run.m_left, run.m_top, run.m_right, bottom));
    };
    for(auto row = 0; row <= mask.m_height; ++row) {
      next.clear();
      auto index = std::size_t(0);
      if(row != mask.m_height) {
        auto coverage = mask.m_coverage.data() + row * mask.m_width;
        auto column = 0;
        while(column != mask.m_width) {
          auto level = static_cast<int>(coverage[column]);
          auto start = column;
          while(column != mask.m_width && coverage[column] == level) {
            ++column;
          }
          if(level == 0 || level > LEVELS) {
            continue;
          }
          while(index != open.size() && open[index].m_left < start) {
            finish(open[index], row);
            ++index;
          }
          if(index != open.size() && open[index].m_left == start &&
              open[index].m_right == column && open[index].m_level == level) {
            next.push_back(open[index]);
            ++index;
          } else {
            next.push_back(Run(start, column, level, row));
          }
        }
      }
      while(index != open.size()) {
        finish(open[index], row);
        ++index;
      }
      std::swap(open, next);
    }
    auto levels = std::array<Coverage, LEVELS + 1>();
    for(auto level = 1; level <= LEVELS; ++level) {
      auto& path = levels[level].m_path;
      path.reserve(5 * blocks[level].size());
      auto extent = std::array<int, 4>({std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(), std::numeric_limits<int>::min(),
        std::numeric_limits<int>::min()});
      for(auto& block : blocks[level]) {
        extent[0] = std::min(extent[0], block.m_left);
        extent[1] = std::min(extent[1], block.m_top);
        extent[2] = std::max(extent[2], block.m_right);
        extent[3] = std::max(extent[3], block.m_bottom);
        auto left = static_cast<double>(mask.m_left + block.m_left);
        auto top = static_cast<double>(mask.m_top + block.m_top);
        auto right = static_cast<double>(mask.m_left + block.m_right);
        auto bottom = static_cast<double>(mask.m_top + block.m_bottom);
        path.move_to(Point(left, top));
        path.line_to(Point(right, top));
        path.line_to(Point(right, bottom));
        path.line_to(Point(left, bottom));
        path.close();
      }
      if(!blocks[level].empty()) {
        levels[level].m_bounds = Rect(mask.m_left + extent[0],
          mask.m_top + extent[1], extent[2] - extent[0],
          extent[3] - extent[1]);
      }
    }
    return levels;
  }
}

const std::optional<Matrix>& SceneBuilder::get_device_transform() const {
  return m_device_transform;
}

void SceneBuilder::set_device_transform(
    const std::optional<Matrix>& transform) {
  if(transform != m_device_transform) {
    clear_retained();
  }
  m_device_transform = transform;
}

const std::optional<Matrix>& SceneBuilder::get_text_transform() const {
  return m_text_transform;
}

void SceneBuilder::set_text_transform(const std::optional<Matrix>& transform) {
  if(transform && (!m_raster_transform ||
      !is_similar(*m_raster_transform, *transform))) {
    ++m_raster_epoch;
    m_raster_transform = transform;
  }
  m_text_transform = transform;
}

const NodeId& SceneBuilder::get_edited_text() const {
  return m_edited_text;
}

void SceneBuilder::set_edited_text(const NodeId& id) {
  if(id == m_edited_text) {
    return;
  }
  auto previous = std::exchange(m_edited_text, id);
  for(auto& changed : {previous, id}) {
    if(!changed.is_nil()) {
      m_texts.erase(changed);
      invalidate(changed);
    }
  }
}

void SceneBuilder::add_text(
    const Node& node, const Placement& placement, DrawList& list) {
  auto& outline = get_text_outline(node);
  auto& clip = placement.m_clip;
  auto first = list.size();
  auto local = placement.m_transform * outline.m_mirror;
  auto inverse = std::optional<Matrix>();
  auto transform = Matrix();
  auto& device_transform = get_text_device();
  if(device_transform) {
    inverse = invert(*device_transform);
    transform = *device_transform * to_single(local);
  }
  if(m_dependencies && !placement.m_projection) {
    m_dependencies->m_is_device_dependent = true;
    m_dependencies->m_device_transform = device_transform;
  }
  auto is_rasterized = inverse && !placement.m_projection &&
    TextRasterizer::is_supported(transform);
  auto rasters = static_cast<const std::vector<TextLevel>*>(nullptr);
  if(is_rasterized) {
    rasters = find_rasters(outline, transform);
    if(!rasters && m_is_retaining && m_is_text_deferred &&
        !m_device_transform) {
      defer(node.get_id(), transform, placement.m_is_referenced);
      is_rasterized = false;
    } else if(!rasters) {
      rasters = &add_rasters(outline, transform, placement.m_is_referenced);
    }
  } else if(!placement.m_is_referenced) {
    m_latest.erase(node.get_id());
    outline.m_rasters.clear();
  }
  if(is_rasterized) {
    if(m_dependencies) {
      m_dependencies->m_is_rasterized = true;
    }
    for(auto& level : *rasters) {
      auto color = level.m_color;
      color.m_alpha *=
        placement.m_opacity * get_text_opacity(level.m_level, level.m_color);
      list.push_back(
        FillCommand(level.m_path, *inverse, SolidPaint(color), clip,
          std::nullopt, level.m_bounds));
    }
    auto outlined = TextLayout();
    auto device = invert(transform);
    for(auto& glyph : outline.m_layout.m_glyphs) {
      if(TextRasterizer::is_outlined(glyph, transform)) {
        outlined.m_glyphs.push_back(glyph);
      } else if(!glyph.m_typeface->get_color_layers(glyph.m_index).empty()) {
        auto& colored = outlined.m_glyphs.emplace_back(glyph);
        colored.m_origin =
          device->map(TextRasterizer::get_origin(glyph, transform));
      }
    }
    add_glyphs(outlined, to_single(local), placement.m_opacity, clip, list);
  } else if(placement.m_projection) {
    add_glyphs(outline.m_layout, local, placement.m_opacity, clip, list);
  } else {
    for(auto i = std::size_t(0); i != outline.m_fills.size(); ++i) {
      auto& fill = outline.m_fills[i];
      if(outline.m_paths[i]->is_empty()) {
        continue;
      }
      auto color = fill.m_color;
      color.m_alpha *= placement.m_opacity;
      list.push_back(
        FillCommand(outline.m_paths[i], local, SolidPaint(color), clip,
          std::nullopt, outline.m_bounds[i]));
    }
  }
  add_effects(list, first, node.get_text()->m_style.m_effects, local,
    placement.m_clip);
}

SceneBuilder::TextOutline& SceneBuilder::get_text_outline(const Node& node) {
  auto& outline = m_texts[node.get_id()];
  if(outline.m_record.get() != &node) {
    outline.m_record = m_document->get_nodes().get_record(node.get_id());
    outline.m_layout = lay_out(*node.get_text(), get_default_font_library(),
      node.get_id() == m_edited_text);
    outline.m_fills = Rellu::outline(outline.m_layout);
    outline.m_paths.clear();
    outline.m_bounds.clear();
    for(auto& fill : outline.m_fills) {
      outline.m_bounds.push_back(get_bounds(fill.m_path));
      outline.m_paths.push_back(
        std::make_shared<const Path>(std::move(fill.m_path)));
    }
    outline.m_rasters.clear();
    outline.m_mirror = Matrix();
    if(auto& text = *node.get_text();
        text.m_is_flipped_vertically && node.get_id() != m_edited_text) {
      auto bounds = measure_text(text, get_default_font_library());
      outline.m_mirror =
        Matrix(1, 0, 0, -1, 0, 2 * bounds.m_y + bounds.m_height);
    }
  }
  if(outline.m_raster_epoch != m_raster_epoch) {
    outline.m_rasters.clear();
    outline.m_raster_epoch = m_raster_epoch;
  }
  outline.m_generation = m_generation;
  return outline;
}

const std::vector<SceneBuilder::TextLevel>* SceneBuilder::find_rasters(
    const TextOutline& outline, const Matrix& transform) const {
  for(auto& rasters : outline.m_rasters) {
    if(rasters.first == transform) {
      return &rasters.second;
    }
  }
  return nullptr;
}

const std::vector<SceneBuilder::TextLevel>& SceneBuilder::add_rasters(
    TextOutline& outline, const Matrix& transform, bool is_referenced) {
  if(!is_referenced) {
    outline.m_rasters.clear();
  } else if(!m_is_text_deferred) {
    std::erase_if(outline.m_rasters, [&] (const auto& rasters) {
      return !is_similar(rasters.first, transform);
    });
    if(outline.m_rasters.size() >= MAXIMUM_RASTERS) {
      outline.m_rasters.erase(outline.m_rasters.begin());
    }
  }
  auto levels = std::vector<TextLevel>();
  for(auto& raster : get_default_text_rasterizer().rasterize(
      outline.m_layout, transform)) {
    auto coverages = make_levels(raster.m_mask);
    for(auto level = 1; level <= LEVELS; ++level) {
      auto& coverage = coverages[level];
      if(!coverage.m_path.is_empty()) {
        levels.push_back(TextLevel(std::make_shared<const Path>(
          std::move(coverage.m_path)), coverage.m_bounds, raster.m_color,
          level));
      }
    }
  }
  return outline.m_rasters.emplace_back(transform, std::move(levels)).second;
}

void SceneBuilder::defer(
    const NodeId& id, const Matrix& transform, bool is_referenced) {
  if(!is_referenced) {
    auto latest = m_latest.find(id);
    if(latest != m_latest.end() && latest->second == transform) {
      return;
    }
    m_latest.insert_or_assign(id, transform);
  }
  m_pending.push_back(PendingText(id, transform, is_referenced));
}

bool SceneBuilder::is_text_deferred() const {
  return m_is_text_deferred;
}

void SceneBuilder::set_text_deferred(bool is_deferred) {
  m_is_text_deferred = is_deferred;
}

bool SceneBuilder::has_pending_text() const {
  return !m_pending.empty();
}

std::vector<NodeId> SceneBuilder::rasterize_text(
    std::chrono::nanoseconds budget) {
  auto start = std::chrono::steady_clock::now();
  auto deadline = std::chrono::steady_clock::time_point::max();
  if(budget < deadline - start) {
    deadline = start + budget;
  }
  auto ids = std::vector<NodeId>();
  auto is_started = false;
  while(!m_pending.empty() &&
      (!is_started || std::chrono::steady_clock::now() < deadline)) {
    auto& pending = m_pending.front();
    auto latest = m_latest.find(pending.m_id);
    auto node = m_document->find(pending.m_id);
    if((!pending.m_is_referenced && (latest == m_latest.end() ||
        latest->second != pending.m_transform)) || !node ||
        !node->get_text()) {
      m_pending.pop_front();
      continue;
    }
    is_started = true;
    auto& outline = get_text_outline(*node);
    if(!find_rasters(outline, pending.m_transform)) {
      if(!get_default_text_rasterizer().prepare(
          outline.m_layout, pending.m_transform, deadline)) {
        break;
      }
      add_rasters(outline, pending.m_transform, pending.m_is_referenced);
    }
    if(!pending.m_is_referenced) {
      m_latest.erase(latest);
    }
    ids.push_back(pending.m_id);
    m_pending.pop_front();
  }
  return ids;
}

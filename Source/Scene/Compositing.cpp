#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Scene/Compositing.hpp"

using namespace Rellu;

namespace {
  double to_brightness(double amount) {
    auto clamped = std::clamp(amount, -50.0, 50.0);
    return std::floor(clamped * 2.55 + 0.5) / 255;
  }

  double get_scale(const Matrix& matrix) {
    return std::sqrt(std::abs(matrix.get_determinant()));
  }

  Point map_vector(const Matrix& matrix, Point vector) {
    return Point(matrix.m_a * vector.m_x + matrix.m_c * vector.m_y,
      matrix.m_b * vector.m_x + matrix.m_d * vector.m_y);
  }

  Shadow to_shadow(
      Point offset, double blur, Color color, const Matrix& transform) {
    return Shadow(map_vector(transform, offset), blur * get_scale(transform),
      color);
  }

  void multiply_opacity(Paint& paint, double opacity) {
    if(auto solid = std::get_if<SolidPaint>(&paint)) {
      solid->m_color.m_alpha *= opacity;
    } else if(auto gradient = std::get_if<GradientPaint>(&paint)) {
      for(auto& stop : gradient->m_stops) {
        stop.m_color.m_alpha *= opacity;
      }
    } else if(auto image = std::get_if<ImagePaint>(&paint)) {
      image->m_opacity *= opacity;
    }
  }

  bool is_blending(const DrawList& list, std::size_t first) {
    return std::any_of(list.begin() + first, list.end(),
      [] (const auto& command) {
        if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
          return layer->m_blend_mode != BlendMode::NORMAL;
        }
        return std::holds_alternative<BackdropBlurCommand>(command);
      });
  }

  BeginLayerCommand* find_lone_layer(DrawList& list, std::size_t first) {
    auto layer = std::get_if<BeginLayerCommand>(&list[first]);
    if(!layer) {
      return nullptr;
    }
    auto depth = 0;
    for(auto i = first; i != list.size(); ++i) {
      if(std::holds_alternative<BeginLayerCommand>(list[i])) {
        ++depth;
      } else if(std::holds_alternative<EndLayerCommand>(list[i])) {
        --depth;
        if(depth == 0) {
          if(i + 1 == list.size()) {
            return layer;
          }
          return nullptr;
        }
      }
    }
    return nullptr;
  }

  bool is_rectangle(const ShapeGeometry& geometry) {
    auto rectangle = std::get_if<RectangleShape>(&geometry);
    if(!rectangle) {
      return false;
    }
    auto& radii = rectangle->m_radii;
    return radii.m_top_left <= 0 && radii.m_top_right <= 0 &&
      radii.m_bottom_right <= 0 && radii.m_bottom_left <= 0;
  }
}

void Rellu::add_styled(DrawList& list, std::optional<FillCommand> fill,
    std::optional<FillCommand> stroke, const std::vector<Effect>& effects) {
  auto outline = static_cast<FillCommand*>(nullptr);
  if(fill) {
    outline = &*fill;
  } else if(stroke) {
    outline = &*stroke;
  } else {
    return;
  }
  auto& transform = outline->m_transform;
  auto outer = BeginLayerCommand();
  outer.m_clip = outline->m_clip;
  auto inner = outer;
  auto backdrop = std::optional<BackdropBlurCommand>();
  auto fill_opacity = 1.0;
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
      if(!blur->m_is_visible) {
        continue;
      } else if(blur->m_is_background) {
        if(fill) {
          backdrop.emplace(fill->m_path, transform,
            blur->m_amount * get_scale(transform),
            to_brightness(blur->m_brightness), fill->m_clip);
          fill_opacity = std::clamp(blur->m_fill_opacity, 0.0, 1.0);
        }
      } else {
        outer.m_blur = blur->m_amount * get_scale(transform);
      }
    }
  }
  auto is_outer = !outer.m_drop_shadows.empty() || outer.m_blur > 0;
  auto is_inner = fill && !inner.m_inner_shadows.empty();
  if(is_outer) {
    list.push_back(std::move(outer));
  }
  if(backdrop) {
    list.push_back(std::move(*backdrop));
  }
  if(fill) {
    if(fill_opacity < 1) {
      multiply_opacity(fill->m_paint, fill_opacity);
    }
    if(is_inner) {
      list.push_back(std::move(inner));
    }
    list.push_back(std::move(*fill));
    if(is_inner) {
      list.push_back(EndLayerCommand());
    }
  }
  if(stroke) {
    list.push_back(std::move(*stroke));
  }
  if(is_outer) {
    list.push_back(EndLayerCommand());
  }
}

std::optional<LayerMask> Rellu::make_mask(const Mask& mask,
    const Matrix& transform, double tolerance, std::optional<Rect>& clip) {
  if(mask.m_shapes.size() == 1 && is_rectangle(mask.m_shapes[0].m_geometry)) {
    auto matrix = transform * mask.m_shapes[0].m_transform;
    if(matrix.m_b == 0 && matrix.m_c == 0) {
      auto rectangle = matrix.map(
        std::get<RectangleShape>(mask.m_shapes[0].m_geometry).m_bounds);
      if(clip) {
        clip = intersect(*clip, rectangle);
      } else {
        clip = rectangle;
      }
      return std::nullopt;
    }
  }
  if(mask.m_shapes.size() == 1) {
    return LayerMask(
      std::make_shared<const Path>(to_path(mask.m_shapes[0].m_geometry)),
      transform * mask.m_shapes[0].m_transform);
  }
  auto paths = std::vector<Path>();
  auto scale = std::max(get_scale(transform), 1e-9);
  for(auto& shape : mask.m_shapes) {
    paths.push_back(::transform(to_path(shape.m_geometry), shape.m_transform));
  }
  return LayerMask(std::make_shared<const Path>(
    combine(paths, BooleanOperation::ADD, tolerance / scale)), transform);
}

void Rellu::add_composite(DrawList& list, std::size_t first,
    const NodeHeader& header, std::optional<LayerMask> mask,
    const std::optional<Rect>& clip) {
  if(first >= list.size()) {
    return;
  }
  auto opacity = std::clamp(header.m_opacity, 0.0, 1.0);
  auto mode = header.m_blend_mode;
  if(!mask && mode == BlendMode::NORMAL &&
      !(header.m_is_isolated && is_blending(list, first))) {
    if(opacity >= 1) {
      return;
    } else if(list.size() - first == 1) {
      if(auto fill = std::get_if<FillCommand>(&list[first])) {
        multiply_opacity(fill->m_paint, opacity);
        return;
      }
    }
    if(auto layer = find_lone_layer(list, first)) {
      if(layer->m_blend_mode == BlendMode::NORMAL) {
        layer->m_opacity *= opacity;
        return;
      }
    }
  }
  auto layer = BeginLayerCommand();
  layer.m_opacity = opacity;
  layer.m_blend_mode = mode;
  layer.m_mask = std::move(mask);
  layer.m_clip = clip;
  list.insert(list.begin() + first, std::move(layer));
  list.push_back(EndLayerCommand());
}

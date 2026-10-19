#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include "Rellu/Prototype/Interpolation.hpp"

using namespace Rellu;

namespace {
  struct Decomposition {
    double m_angle;
    double m_x_scale;
    double m_y_scale;
    double m_shear;
  };

  Decomposition decompose(const Matrix& matrix) {
    auto x_scale = std::hypot(matrix.m_a, matrix.m_b);
    if(x_scale == 0) {
      return Decomposition(0, 0, matrix.m_d, matrix.m_c);
    }
    auto angle = std::atan2(matrix.m_b, matrix.m_a);
    auto cosine = matrix.m_a / x_scale;
    auto sine = matrix.m_b / x_scale;
    return Decomposition(angle, x_scale,
      -sine * matrix.m_c + cosine * matrix.m_d,
      cosine * matrix.m_c + sine * matrix.m_d);
  }

  double interpolate_angle(double start, double end, double progress) {
    auto delta = std::remainder(end - start, 2 * std::numbers::pi);
    return start + delta * progress;
  }

  CornerRadii interpolate_radii(
      const CornerRadii& start, const CornerRadii& end, double progress) {
    return CornerRadii(
      std::max(0.0, interpolate(start.m_top_left, end.m_top_left, progress)),
      std::max(0.0, interpolate(start.m_top_right, end.m_top_right, progress)),
      std::max(0.0,
        interpolate(start.m_bottom_right, end.m_bottom_right, progress)),
      std::max(0.0,
        interpolate(start.m_bottom_left, end.m_bottom_left, progress)));
  }

  Rect interpolate_rect(const Rect& start, const Rect& end, double progress) {
    return Rect(interpolate(start.m_x, end.m_x, progress),
      interpolate(start.m_y, end.m_y, progress),
      std::max(0.0, interpolate(start.m_width, end.m_width, progress)),
      std::max(0.0, interpolate(start.m_height, end.m_height, progress)));
  }

  Size interpolate_size(Size start, Size end, double progress) {
    return Size(
      std::max(0.0, interpolate(start.m_width, end.m_width, progress)),
      std::max(0.0, interpolate(start.m_height, end.m_height, progress)));
  }

  std::optional<GradientGeometry> interpolate_gradient(
      const GradientGeometry& start, const GradientGeometry& end,
      double progress) {
    if(auto linear = std::get_if<LinearGradient>(&start)) {
      if(auto target = std::get_if<LinearGradient>(&end)) {
        return LinearGradient(
          interpolate(linear->m_start, target->m_start, progress),
          interpolate(linear->m_end, target->m_end, progress));
      }
      return std::nullopt;
    }
    auto& radial = std::get<RadialGradient>(start);
    if(auto target = std::get_if<RadialGradient>(&end)) {
      return RadialGradient(
        interpolate(radial.m_center, target->m_center, progress),
        std::max(0.0, interpolate(radial.m_radius, target->m_radius, progress)),
        interpolate(radial.m_focus, target->m_focus, progress));
    }
    return std::nullopt;
  }

  ShapeGeometry interpolate_geometry(const ShapeGeometry& start,
      const ShapeGeometry& end, double progress) {
    if(start.index() != end.index()) {
      return end;
    }
    if(auto rectangle = std::get_if<RectangleShape>(&start)) {
      auto& target = std::get<RectangleShape>(end);
      return RectangleShape(
        interpolate_rect(rectangle->m_bounds, target.m_bounds, progress),
        interpolate_radii(rectangle->m_radii, target.m_radii, progress));
    } else if(auto ellipse = std::get_if<EllipseShape>(&start)) {
      auto& target = std::get<EllipseShape>(end);
      return EllipseShape(
        interpolate(ellipse->m_center, target.m_center, progress),
        std::max(0.0,
          interpolate(ellipse->m_x_radius, target.m_x_radius, progress)),
        std::max(0.0,
          interpolate(ellipse->m_y_radius, target.m_y_radius, progress)));
    } else if(auto line = std::get_if<LineShape>(&start)) {
      auto& target = std::get<LineShape>(end);
      return LineShape(interpolate(line->m_start, target.m_start, progress),
        interpolate(line->m_end, target.m_end, progress));
    } else if(auto polygon = std::get_if<PolygonShape>(&start)) {
      auto target = std::get<PolygonShape>(end);
      if(polygon->m_corner_count != target.m_corner_count) {
        return target;
      }
      target.m_size =
        interpolate_size(polygon->m_size, target.m_size, progress);
      target.m_corner_radius = std::max(0.0, interpolate(
        polygon->m_corner_radius, target.m_corner_radius, progress));
      if(polygon->m_vertices.size() == target.m_vertices.size()) {
        for(auto i = std::size_t(0); i != target.m_vertices.size(); ++i) {
          target.m_vertices[i] = interpolate(
            polygon->m_vertices[i], target.m_vertices[i], progress);
        }
      }
      return target;
    } else if(auto path = std::get_if<PathShape>(&start)) {
      auto target = std::get<PathShape>(end);
      if(auto morph = interpolate(*path->m_path, *target.m_path, progress)) {
        target.m_path = Shared<Path>(std::move(*morph));
      }
      return target;
    } else if(auto compound = std::get_if<CompoundShape>(&start)) {
      auto target = std::get<CompoundShape>(end);
      if(auto morph =
          interpolate(*compound->m_path, *target.m_path, progress)) {
        target.m_path = Shared<Path>(std::move(*morph));
      }
      return target;
    }
    return end;
  }

  std::optional<Effect> interpolate_effect(
      const Effect& start, const Effect& end, double progress) {
    if(start.index() != end.index()) {
      return std::nullopt;
    }
    if(auto drop = std::get_if<DropShadow>(&start)) {
      auto target = std::get<DropShadow>(end);
      target.m_offset = interpolate(drop->m_offset, target.m_offset, progress);
      target.m_blur =
        std::max(0.0, interpolate(drop->m_blur, target.m_blur, progress));
      target.m_color = interpolate(drop->m_color, target.m_color, progress);
      return target;
    } else if(auto inner = std::get_if<InnerShadow>(&start)) {
      auto target = std::get<InnerShadow>(end);
      target.m_offset =
        interpolate(inner->m_offset, target.m_offset, progress);
      target.m_blur =
        std::max(0.0, interpolate(inner->m_blur, target.m_blur, progress));
      target.m_color = interpolate(inner->m_color, target.m_color, progress);
      return target;
    }
    auto& blur = std::get<Blur>(start);
    auto target = std::get<Blur>(end);
    target.m_amount =
      std::max(0.0, interpolate(blur.m_amount, target.m_amount, progress));
    target.m_brightness =
      interpolate(blur.m_brightness, target.m_brightness, progress);
    target.m_fill_opacity = std::clamp(
      interpolate(blur.m_fill_opacity, target.m_fill_opacity, progress), 0.0,
      1.0);
    return target;
  }
}

double Rellu::interpolate(double start, double end, double progress) {
  return start + (end - start) * progress;
}

Color Rellu::interpolate(
    const Color& start, const Color& end, double progress) {
  auto mix = [&] (double first, double second) {
    return std::clamp(interpolate(first, second, progress), 0.0, 1.0);
  };
  return Color(mix(start.m_red, end.m_red), mix(start.m_green, end.m_green),
    mix(start.m_blue, end.m_blue), mix(start.m_alpha, end.m_alpha));
}

Matrix Rellu::interpolate(
    const Matrix& start, const Matrix& end, double progress) {
  if(progress == 0) {
    return start;
  } else if(progress == 1) {
    return end;
  }
  auto first = decompose(start);
  auto second = decompose(end);
  if(first.m_x_scale == 0) {
    first.m_angle = second.m_angle;
  } else if(second.m_x_scale == 0) {
    second.m_angle = first.m_angle;
  }
  auto angle = interpolate_angle(first.m_angle, second.m_angle, progress);
  auto x_scale = interpolate(first.m_x_scale, second.m_x_scale, progress);
  auto y_scale = interpolate(first.m_y_scale, second.m_y_scale, progress);
  auto shear = interpolate(first.m_shear, second.m_shear, progress);
  auto linear = make_rotation(angle) * Matrix(x_scale, 0, shear, y_scale, 0, 0);
  linear.m_tx = interpolate(start.m_tx, end.m_tx, progress);
  linear.m_ty = interpolate(start.m_ty, end.m_ty, progress);
  return linear;
}

std::optional<Fill> Rellu::interpolate(
    const Fill& start, const Fill& end, double progress) {
  if(auto solid = std::get_if<SolidFill>(&start)) {
    if(auto target = std::get_if<SolidFill>(&end)) {
      auto fill = *target;
      fill.m_color = interpolate(solid->m_color, target->m_color, progress);
      return fill;
    }
    return std::nullopt;
  } else if(auto gradient = std::get_if<GradientFill>(&start)) {
    auto target = std::get_if<GradientFill>(&end);
    if(!target || target->m_stops.size() != gradient->m_stops.size()) {
      return std::nullopt;
    }
    auto geometry =
      interpolate_gradient(gradient->m_geometry, target->m_geometry, progress);
    if(!geometry) {
      return std::nullopt;
    }
    auto fill = *target;
    fill.m_geometry = std::move(*geometry);
    for(auto i = std::size_t(0); i != fill.m_stops.size(); ++i) {
      fill.m_stops[i].m_offset = std::clamp(interpolate(
        gradient->m_stops[i].m_offset, target->m_stops[i].m_offset, progress),
        0.0, 1.0);
      fill.m_stops[i].m_color = interpolate(
        gradient->m_stops[i].m_color, target->m_stops[i].m_color, progress);
    }
    fill.m_transform =
      interpolate(gradient->m_transform, target->m_transform, progress);
    return fill;
  }
  return std::nullopt;
}

std::optional<Path> Rellu::interpolate(
    const Path& start, const Path& end, double progress) {
  auto& first = start.get_segments();
  auto& second = end.get_segments();
  if(first.size() != second.size()) {
    return std::nullopt;
  }
  for(auto i = std::size_t(0); i != first.size(); ++i) {
    if(first[i].m_type != second[i].m_type) {
      return std::nullopt;
    }
  }
  auto path = Path(end.get_fill_rule());
  for(auto i = std::size_t(0); i != first.size(); ++i) {
    auto point = [&] (std::size_t index) {
      return interpolate(first[i].m_points[index], second[i].m_points[index],
        progress);
    };
    switch(second[i].m_type) {
      case SegmentType::MOVE:
        path.move_to(point(0));
        break;
      case SegmentType::LINE:
        path.line_to(point(0));
        break;
      case SegmentType::QUADRATIC:
        path.quadratic_to(point(0), point(1));
        break;
      case SegmentType::CUBIC:
        path.cubic_to(point(0), point(1), point(2));
        break;
      case SegmentType::CLOSE:
        path.close();
        break;
    }
  }
  return path;
}

Style Rellu::interpolate(
    const Style& start, const Style& end, double progress) {
  auto style = end;
  if(auto fill = interpolate(start.m_fill, end.m_fill, progress)) {
    style.m_fill = std::move(*fill);
  }
  if(start.m_stroke && style.m_stroke) {
    style.m_stroke->m_color =
      interpolate(start.m_stroke->m_color, style.m_stroke->m_color, progress);
    style.m_stroke->m_width = std::max(0.0,
      interpolate(start.m_stroke->m_width, style.m_stroke->m_width, progress));
  }
  if(start.m_effects.size() == style.m_effects.size()) {
    for(auto i = std::size_t(0); i != style.m_effects.size(); ++i) {
      if(auto effect = interpolate_effect(
          start.m_effects[i], style.m_effects[i], progress)) {
        style.m_effects[i] = std::move(*effect);
      }
    }
  }
  return style;
}

Payload Rellu::interpolate(
    const Payload& start, const Payload& end, double progress) {
  if(start.index() != end.index()) {
    return end;
  }
  if(auto shape = std::get_if<Box<Shape>>(&start)) {
    auto& target = **std::get_if<Box<Shape>>(&end);
    auto result = Shape();
    result.m_geometry =
      interpolate_geometry((*shape)->m_geometry, target.m_geometry, progress);
    result.m_style = interpolate((*shape)->m_style, target.m_style, progress);
    return Box<Shape>(std::move(result));
  } else if(auto artboard = std::get_if<Box<Artboard>>(&start)) {
    auto result = **std::get_if<Box<Artboard>>(&end);
    if(auto fill = interpolate(
        (*artboard)->m_background, result.m_background, progress)) {
      result.m_background = std::move(*fill);
    }
    return Box<Artboard>(std::move(result));
  }
  return end;
}

NodeHeader Rellu::interpolate(
    const NodeHeader& start, const NodeHeader& end, double progress) {
  auto header = end;
  header.m_transform =
    interpolate(start.m_transform, end.m_transform, progress);
  header.m_opacity =
    std::clamp(interpolate(start.m_opacity, end.m_opacity, progress), 0.0, 1.0);
  return header;
}

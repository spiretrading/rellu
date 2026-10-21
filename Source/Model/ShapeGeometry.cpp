#include <type_traits>
#include <variant>
#include "Rellu/Model/ShapeGeometry.hpp"

using namespace Rellu;

namespace {
  Path make_star(const PolygonShape& shape) {
    auto star = make_polygon_vertices(shape.m_corner_count,
      Rect(0, 0, shape.m_size.m_width, shape.m_size.m_height),
      shape.m_star_ratio);
    auto& corners = shape.m_vertices;
    auto source = Matrix(star[2].m_x - star[0].m_x, star[2].m_y - star[0].m_y,
      star[4].m_x - star[0].m_x, star[4].m_y - star[0].m_y, 0, 0);
    auto inverse = invert(source);
    if(!inverse) {
      return make_polygon(corners, shape.m_corner_radius);
    }
    auto target = Matrix(corners[1].m_x - corners[0].m_x,
      corners[1].m_y - corners[0].m_y, corners[2].m_x - corners[0].m_x,
      corners[2].m_y - corners[0].m_y, 0, 0);
    auto mapping = make_translation(corners[0].m_x, corners[0].m_y) *
      target * *inverse * make_translation(-star[0].m_x, -star[0].m_y);
    for(auto& vertex : star) {
      vertex = mapping.map(vertex);
    }
    return make_polygon(star, shape.m_corner_radius);
  }
}

Path Rellu::to_path(const ShapeGeometry& geometry) {
  return std::visit([] (const auto& shape) {
    using Type = std::decay_t<decltype(shape)>;
    if constexpr(std::is_same_v<Type, RectangleShape>) {
      return make_rectangle(shape.m_bounds, shape.m_radii);
    } else if constexpr(std::is_same_v<Type, EllipseShape>) {
      return make_ellipse(shape.m_center, shape.m_x_radius, shape.m_y_radius);
    } else if constexpr(std::is_same_v<Type, LineShape>) {
      return make_line(shape.m_start, shape.m_end);
    } else if constexpr(std::is_same_v<Type, PolygonShape>) {
      if(shape.m_vertices.empty()) {
        return make_polygon(shape.m_corner_count,
          Rect(0, 0, shape.m_size.m_width, shape.m_size.m_height),
          shape.m_corner_radius, shape.m_star_ratio);
      }
      if(shape.m_star_ratio < 100 && shape.m_corner_count >= 3 &&
          shape.m_vertices.size() ==
            static_cast<std::size_t>(shape.m_corner_count)) {
        return make_star(shape);
      }
      return make_polygon(shape.m_vertices, shape.m_corner_radius);
    } else if constexpr(std::is_same_v<Type, PathShape>) {
      auto path = *shape.m_path;
      path.set_fill_rule(shape.m_fill_rule);
      return path;
    } else {
      return *shape.m_path;
    }
  }, geometry);
}

#include <type_traits>
#include <variant>
#include "Rellu/Model/ShapeGeometry.hpp"

using namespace Rellu;

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
          shape.m_corner_radius);
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

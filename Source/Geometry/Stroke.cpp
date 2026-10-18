#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>
#include <clipper2/clipper.h>
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Geometry/Stroke.hpp"

using namespace Rellu;

namespace {
  struct Vertex {
    Point m_point;
    double m_continuity;
  };

  struct Contour {
    std::vector<Vertex> m_vertices;
    Point m_direction;
    bool m_is_closed;
  };

  struct Pen {
    double m_radius;
    double m_tolerance;
    StrokeCap m_cap;
    StrokeJoin m_join;
    double m_miter_limit;
  };

  struct Outline {
    Clipper2Lib::Paths64 m_open;
    Clipper2Lib::Paths64 m_closed;
    Clipper2Lib::Paths64 m_shapes;
  };

  int get_precision(double tolerance) {
    return std::clamp(
      2 - static_cast<int>(std::floor(std::log10(tolerance))), 2,
      Clipper2Lib::CLIPPER2_MAX_DEC_PRECISION);
  }

  Point snap(Point point) {
    return Point(std::round(point.m_x), std::round(point.m_y));
  }

  Point get_normal(Point start, Point end) {
    auto x = end.m_x - start.m_x;
    auto y = end.m_y - start.m_y;
    auto inverse = 1.0 / std::sqrt(x * x + y * y);
    return Point(y * inverse, -x * inverse);
  }

  Point rotate(Point point, double angle) {
    auto cosine = std::cos(angle);
    auto sine = std::sin(angle);
    return Point(point.m_x * cosine - point.m_y * sine,
      point.m_x * sine + point.m_y * cosine);
  }

  Point get_end(const Segment& segment) {
    if(segment.m_type == SegmentType::QUADRATIC) {
      return segment.m_points[1];
    } else if(segment.m_type == SegmentType::CUBIC) {
      return segment.m_points[2];
    }
    return segment.m_points[0];
  }

  Point get_first_nonzero(std::initializer_list<Point> points) {
    for(auto& point : points) {
      if(point != Point()) {
        return point;
      }
    }
    return Point();
  }

  std::pair<Point, Point> get_tangents(Point start, const Segment& segment) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::QUADRATIC) {
      return std::pair(
        get_first_nonzero({points[0] - start, points[1] - start}),
        get_first_nonzero({points[1] - points[0], points[1] - start}));
    } else if(segment.m_type == SegmentType::CUBIC) {
      return std::pair(get_first_nonzero(
        {points[0] - start, points[1] - start, points[2] - start}),
        get_first_nonzero({points[2] - points[1], points[2] - points[0],
          points[2] - start}));
    }
    return std::pair(points[0] - start, points[0] - start);
  }

  double get_continuity(Point arrival, Point departure) {
    auto cosine =
      dot(arrival, departure) / (length(arrival) * length(departure));
    return std::sqrt(std::clamp((1 + cosine) / 2, 0.0, 1.0));
  }

  std::vector<Point> flatten_segment(
      Point start, const Segment& segment, double tolerance) {
    if(segment.m_type != SegmentType::QUADRATIC &&
        segment.m_type != SegmentType::CUBIC) {
      return std::vector{segment.m_points[0]};
    }
    auto path = Path();
    path.move_to(start);
    if(segment.m_type == SegmentType::QUADRATIC) {
      path.quadratic_to(segment.m_points[0], segment.m_points[1]);
    } else {
      path.cubic_to(
        segment.m_points[0], segment.m_points[1], segment.m_points[2]);
    }
    auto points = std::move(flatten(path, tolerance).front().m_points);
    points.erase(points.begin());
    return points;
  }

  void add_vertex(Contour& contour, Point point, double continuity) {
    if(!contour.m_vertices.empty() &&
        contour.m_vertices.back().m_point == point) {
      return;
    }
    contour.m_vertices.push_back(Vertex(point, continuity));
  }

  void push_contour(std::vector<Contour>& contours, Contour contour) {
    if(contour.m_vertices.empty()) {
      return;
    }
    if(contour.m_vertices.size() == 1) {
      contour.m_is_closed = false;
    }
    contours.push_back(std::move(contour));
  }

  std::vector<Contour> make_contours(
      const Path& path, double tolerance, double scale) {
    auto contours = std::vector<Contour>();
    auto contour = Contour();
    auto start = Point();
    auto current = Point();
    auto first = std::optional<Point>();
    auto last = std::optional<Point>();
    auto is_drawn = false;
    for(auto& segment : path.get_segments()) {
      if(segment.m_type == SegmentType::MOVE) {
        if(is_drawn) {
          push_contour(contours, std::move(contour));
        }
        start = segment.m_points[0];
        current = start;
        contour = Contour(std::vector{Vertex(snap(start * scale), 1)},
          Point(1, 0), false);
        first = std::nullopt;
        last = std::nullopt;
        is_drawn = false;
        continue;
      }
      is_drawn = true;
      auto [departure, arrival] = get_tangents(current, segment);
      if(departure != Point()) {
        if(last) {
          contour.m_vertices.back().m_continuity =
            std::min(contour.m_vertices.back().m_continuity,
              get_continuity(*last, departure));
        }
        if(!first) {
          first = departure;
        }
        last = arrival;
        for(auto& point : flatten_segment(current, segment, tolerance)) {
          add_vertex(contour, snap(point * scale), 1);
        }
        current = get_end(segment);
      }
      if(segment.m_type == SegmentType::CLOSE) {
        auto& vertices = contour.m_vertices;
        if(vertices.size() > 1 && vertices.back().m_point ==
            vertices.front().m_point) {
          vertices.pop_back();
        }
        if(first && last) {
          vertices.front().m_continuity = get_continuity(*last, *first);
        }
        contour.m_is_closed = true;
      }
    }
    if(is_drawn) {
      push_contour(contours, std::move(contour));
    }
    return contours;
  }

  std::vector<double> make_pattern(const StrokeGeometry& stroke, double scale) {
    auto pattern = std::vector<double>();
    auto total = 0.0;
    for(auto entry : stroke.m_dashes) {
      if(!(entry >= 0) || !std::isfinite(entry)) {
        return std::vector<double>();
      }
      pattern.push_back(entry * scale);
      total += entry;
    }
    if(!(total > 0)) {
      return std::vector<double>();
    }
    if(pattern.size() % 2 == 1) {
      auto size = pattern.size();
      for(auto i = std::size_t(0); i != size; ++i) {
        pattern.push_back(pattern[i]);
      }
    }
    return pattern;
  }

  std::vector<Contour> dash(const Contour& contour,
      const std::vector<double>& pattern, double offset) {
    auto total = 0.0;
    for(auto entry : pattern) {
      total += entry;
    }
    auto phase = std::fmod(offset, total);
    if(phase < 0) {
      phase += total;
    }
    auto index = std::size_t(0);
    while(phase > 0 && phase >= pattern[index]) {
      phase -= pattern[index];
      index = (index + 1) % pattern.size();
    }
    auto remaining = pattern[index] - phase;
    auto is_on = index % 2 == 0;
    auto is_initially_on = is_on;
    auto is_split = false;
    auto& vertices = contour.m_vertices;
    auto dashes = std::vector<Contour>();
    auto current = Contour(std::vector<Vertex>(), contour.m_direction, false);
    if(is_on) {
      current.m_vertices.push_back(vertices.front());
    }
    auto count = vertices.size() - 1;
    if(contour.m_is_closed) {
      ++count;
    }
    for(auto i = std::size_t(0); i != count; ++i) {
      auto start = vertices[i].m_point;
      auto& end = vertices[(i + 1) % vertices.size()];
      auto span = distance(start, end.m_point);
      auto position = 0.0;
      while(span - position >= remaining) {
        position += remaining;
        auto point =
          snap(interpolate(start, end.m_point, position / span));
        if(is_on) {
          add_vertex(current, point, 1);
          current.m_direction = end.m_point - start;
          dashes.push_back(std::move(current));
        }
        current = Contour(std::vector<Vertex>(), end.m_point - start, false);
        if(!is_on) {
          current.m_vertices.push_back(Vertex(point, 1));
        }
        index = (index + 1) % pattern.size();
        remaining = pattern[index];
        is_on = !is_on;
        is_split = true;
      }
      remaining -= span - position;
      if(is_on) {
        add_vertex(current, end.m_point, end.m_continuity);
      }
    }
    if(!is_split) {
      if(is_on) {
        return std::vector{contour};
      }
      return std::vector<Contour>();
    }
    if(is_on) {
      if(contour.m_is_closed && is_initially_on) {
        auto& front = dashes.front().m_vertices;
        for(auto i = std::size_t(1); i < front.size(); ++i) {
          add_vertex(current, front[i].m_point, front[i].m_continuity);
        }
        dashes.front() = std::move(current);
      } else if(current.m_vertices.size() > 1) {
        dashes.push_back(std::move(current));
      }
    }
    return dashes;
  }

  void add_shape(Outline& outline, Clipper2Lib::Path64 shape) {
    auto area = Clipper2Lib::Area(shape);
    if(area == 0) {
      return;
    } else if(area < 0) {
      std::reverse(shape.begin(), shape.end());
    }
    outline.m_shapes.push_back(std::move(shape));
  }

  void add_arc(Clipper2Lib::Path64& shape, Point center, Point start,
      double sweep, const Pen& pen) {
    auto step = 2 * std::acos(
      std::max(0.0, 1 - pen.m_tolerance / pen.m_radius));
    auto count = std::max(1, static_cast<int>(
      std::ceil(sweep / std::min(step, std::numbers::pi / 2))));
    for(auto i = 1; i < count; ++i) {
      auto point = center + rotate(start, sweep * i / count);
      shape.emplace_back(point.m_x, point.m_y);
    }
  }

  void add_cap(Outline& outline, Point point, Point normal, const Pen& pen) {
    if(pen.m_cap == StrokeCap::BUTT) {
      return;
    }
    auto start = point + normal * pen.m_radius;
    auto end = point - normal * pen.m_radius;
    auto shape = Clipper2Lib::Path64();
    shape.emplace_back(start.m_x, start.m_y);
    if(pen.m_cap == StrokeCap::ROUND) {
      add_arc(shape, point, normal * pen.m_radius, std::numbers::pi, pen);
    } else {
      auto extension = Point(-normal.m_y, normal.m_x) * pen.m_radius;
      shape.emplace_back(start.m_x + extension.m_x,
        start.m_y + extension.m_y);
      shape.emplace_back(end.m_x + extension.m_x, end.m_y + extension.m_y);
    }
    shape.emplace_back(end.m_x, end.m_y);
    add_shape(outline, std::move(shape));
  }

  void add_dot(Outline& outline, Point point, Point direction,
      const Pen& pen) {
    auto normal = Point(direction.m_y, -direction.m_x) / length(direction);
    add_cap(outline, point, normal, pen);
    add_cap(outline, point, -normal, pen);
  }

  void add_join(Outline& outline, Point previous, Point vertex, Point next,
      const Pen& pen) {
    auto turn = cross(vertex - previous, next - vertex);
    if(turn == 0) {
      return;
    }
    auto side = turn > 0 ? pen.m_radius : -pen.m_radius;
    auto first = get_normal(previous, vertex);
    auto second = get_normal(vertex, next);
    auto start = vertex + first * side;
    auto end = vertex + second * side;
    auto shape = Clipper2Lib::Path64();
    shape.emplace_back(vertex.m_x, vertex.m_y);
    shape.emplace_back(start.m_x, start.m_y);
    auto cosine = dot(first, second);
    if(pen.m_join == StrokeJoin::MITER &&
        pen.m_miter_limit * pen.m_miter_limit * (1 + cosine) >= 2) {
      auto tip = vertex + (first + second) * (side / (1 + cosine));
      shape.emplace_back(tip.m_x, tip.m_y);
    }
    shape.emplace_back(end.m_x, end.m_y);
    add_shape(outline, std::move(shape));
  }

  Clipper2Lib::Path64 to_clipper(
      std::vector<Vertex>::const_iterator first,
      std::vector<Vertex>::const_iterator last) {
    auto path = Clipper2Lib::Path64();
    path.reserve(last - first);
    for(auto i = first; i != last; ++i) {
      path.emplace_back(i->m_point.m_x, i->m_point.m_y);
    }
    return path;
  }

  bool is_corner(const Vertex& vertex, const Pen& pen) {
    return pen.m_join != StrokeJoin::ROUND && vertex.m_continuity *
      (pen.m_radius + pen.m_tolerance) < pen.m_radius;
  }

  void add_run(
      Outline& outline, const std::vector<Vertex>& vertices, const Pen& pen) {
    auto start = vertices.begin();
    for(auto i = std::next(vertices.begin()); i + 1 < vertices.end(); ++i) {
      if(is_corner(*i, pen)) {
        add_join(outline, std::prev(i)->m_point, i->m_point,
          std::next(i)->m_point, pen);
        outline.m_open.push_back(to_clipper(start, std::next(i)));
        start = i;
      }
    }
    outline.m_open.push_back(to_clipper(start, vertices.end()));
  }

  void add_contour(Outline& outline, const Contour& contour, const Pen& pen) {
    auto& vertices = contour.m_vertices;
    if(vertices.size() == 1) {
      add_dot(outline, vertices.front().m_point, contour.m_direction, pen);
      return;
    }
    if(!contour.m_is_closed) {
      add_cap(outline, vertices.front().m_point,
        -get_normal(vertices[0].m_point, vertices[1].m_point), pen);
      add_cap(outline, vertices.back().m_point,
        get_normal(vertices[vertices.size() - 2].m_point,
          vertices.back().m_point), pen);
      add_run(outline, vertices, pen);
      return;
    }
    auto corner = std::find_if(vertices.begin(), vertices.end(),
      [&] (const auto& vertex) {
        return is_corner(vertex, pen);
      });
    if(corner == vertices.end()) {
      outline.m_closed.push_back(to_clipper(vertices.begin(), vertices.end()));
      return;
    }
    auto run = std::vector<Vertex>(corner, vertices.end());
    run.insert(run.end(), vertices.begin(), std::next(corner));
    add_join(outline, run[run.size() - 2].m_point, run.front().m_point,
      run[1].m_point, pen);
    add_run(outline, run, pen);
  }

  Clipper2Lib::Paths64 trace(
      const std::vector<Contour>& contours, const Pen& pen) {
    auto outline = Outline();
    for(auto& contour : contours) {
      add_contour(outline, contour, pen);
    }
    auto offset = Clipper2Lib::ClipperOffset(2, pen.m_tolerance);
    offset.AddPaths(outline.m_open, Clipper2Lib::JoinType::Round,
      Clipper2Lib::EndType::Butt);
    offset.AddPaths(outline.m_closed, Clipper2Lib::JoinType::Round,
      Clipper2Lib::EndType::Joined);
    auto solution = Clipper2Lib::Paths64();
    offset.Execute(pen.m_radius, solution);
    return Clipper2Lib::Union(
      solution, outline.m_shapes, Clipper2Lib::FillRule::NonZero);
  }

  Clipper2Lib::Paths64 trace(const std::vector<Contour>& contours,
      const std::vector<double>& pattern, double offset, const Pen& pen) {
    if(pattern.empty()) {
      return trace(contours, pen);
    }
    auto dashes = std::vector<Contour>();
    for(auto& contour : contours) {
      for(auto& piece : dash(contour, pattern, offset)) {
        dashes.push_back(std::move(piece));
      }
    }
    return trace(dashes, pen);
  }

  Path to_path(const Clipper2Lib::Paths64& polygons, double scale) {
    auto path = Path(FillRule::NON_ZERO);
    for(auto& polygon : polygons) {
      path.move_to(Point(polygon.front().x / scale, polygon.front().y / scale));
      for(auto i = std::size_t(1); i != polygon.size(); ++i) {
        path.line_to(Point(polygon[i].x / scale, polygon[i].y / scale));
      }
      path.close();
    }
    return path;
  }
}

Path Rellu::outline_stroke(
    const Path& path, const StrokeGeometry& stroke, double tolerance) {
  auto scale = std::pow(10.0, get_precision(tolerance));
  if(!(stroke.m_width * scale >= 2)) {
    return Path();
  }
  auto contours = make_contours(path, tolerance, scale);
  auto pattern = make_pattern(stroke, scale);
  auto offset = stroke.m_dash_offset * scale;
  auto pen = Pen(stroke.m_width * scale / 2, tolerance * scale, stroke.m_cap,
    stroke.m_join, std::max(1.0, stroke.m_miter_limit));
  if(stroke.m_alignment == StrokeAlignment::CENTER) {
    return to_path(trace(contours, pattern, offset, pen), scale);
  }
  auto closed = std::vector<Contour>();
  auto open = std::vector<Contour>();
  for(auto& contour : contours) {
    if(contour.m_is_closed) {
      closed.push_back(contour);
    } else {
      open.push_back(contour);
    }
  }
  if(closed.empty()) {
    return to_path(trace(open, pattern, offset, pen), scale);
  }
  auto shape = Clipper2Lib::Paths64();
  for(auto& contour : closed) {
    shape.push_back(to_clipper(
      contour.m_vertices.begin(), contour.m_vertices.end()));
  }
  auto band = pen;
  band.m_radius = stroke.m_width * scale;
  auto operation = Clipper2Lib::ClipType::Intersection;
  if(stroke.m_alignment == StrokeAlignment::OUTSIDE) {
    operation = Clipper2Lib::ClipType::Difference;
  }
  auto rule = Clipper2Lib::FillRule::NonZero;
  if(path.get_fill_rule() == FillRule::EVEN_ODD) {
    rule = Clipper2Lib::FillRule::EvenOdd;
  }
  auto region = Clipper2Lib::BooleanOp(
    operation, rule, trace(closed, pattern, offset, band), shape);
  if(!open.empty()) {
    region = Clipper2Lib::Union(region, trace(open, pattern, offset, pen),
      Clipper2Lib::FillRule::NonZero);
  }
  return to_path(region, scale);
}

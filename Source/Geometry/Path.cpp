#include <algorithm>
#include <cmath>
#include <limits>
#include "Rellu/Geometry/Path.hpp"

using namespace Rellu;

namespace {
  struct Roots {
    std::array<double, 2> m_values;
    int m_count;
  };

  Roots solve_quadratic(double a, double b, double c) {
    auto roots = Roots(std::array<double, 2>(), 0);
    if(a == 0) {
      if(b != 0) {
        roots.m_values[roots.m_count++] = -c / b;
      }
      return roots;
    }
    auto discriminant = b * b - 4 * a * c;
    if(discriminant < 0) {
      return roots;
    }
    auto q = -(b + std::copysign(std::sqrt(discriminant), b)) / 2;
    roots.m_values[roots.m_count++] = q / a;
    if(q != 0) {
      roots.m_values[roots.m_count++] = c / q;
    }
    return roots;
  }

  Point evaluate_quadratic(Point start, Point control, Point end, double t) {
    auto s = 1 - t;
    return s * s * start + 2 * s * t * control + t * t * end;
  }

  Point evaluate_cubic(
      Point start, Point first, Point second, Point end, double t) {
    auto s = 1 - t;
    return s * s * s * start + 3 * s * s * t * first +
      3 * s * t * t * second + t * t * t * end;
  }

  double get_coordinate(Point point, bool is_vertical) {
    if(is_vertical) {
      return point.m_y;
    }
    return point.m_x;
  }

  Roots get_quadratic_extrema(
      Point start, Point control, Point end, bool is_vertical) {
    auto p0 = get_coordinate(start, is_vertical);
    auto p1 = get_coordinate(control, is_vertical);
    auto p2 = get_coordinate(end, is_vertical);
    return solve_quadratic(0, 2 * (p0 - 2 * p1 + p2), 2 * (p1 - p0));
  }

  Roots get_cubic_extrema(Point start, Point first, Point second, Point end,
      bool is_vertical) {
    auto p0 = get_coordinate(start, is_vertical);
    auto p1 = get_coordinate(first, is_vertical);
    auto p2 = get_coordinate(second, is_vertical);
    auto p3 = get_coordinate(end, is_vertical);
    return solve_quadratic(-p0 + 3 * p1 - 3 * p2 + p3,
      2 * (p0 - 2 * p1 + p2), p1 - p0);
  }

  Segment make_segment(SegmentType type, Point first,
      Point second = Point(), Point third = Point()) {
    auto segment = Segment(type);
    segment.m_points[0] = first;
    segment.m_points[1] = second;
    segment.m_points[2] = third;
    return segment;
  }

  class BoundsBuilder {
    public:
      BoundsBuilder() noexcept;

      void add(Point point);
      Rect get_bounds() const;

    private:
      double m_left;
      double m_top;
      double m_right;
      double m_bottom;
  };

  BoundsBuilder::BoundsBuilder() noexcept
    : m_left(std::numeric_limits<double>::infinity()),
      m_top(std::numeric_limits<double>::infinity()),
      m_right(-std::numeric_limits<double>::infinity()),
      m_bottom(-std::numeric_limits<double>::infinity()) {}

  void BoundsBuilder::add(Point point) {
    m_left = std::min(m_left, point.m_x);
    m_top = std::min(m_top, point.m_y);
    m_right = std::max(m_right, point.m_x);
    m_bottom = std::max(m_bottom, point.m_y);
  }

  Rect BoundsBuilder::get_bounds() const {
    if(m_left > m_right) {
      return Rect();
    }
    return Rect(m_left, m_top, m_right - m_left, m_bottom - m_top);
  }

  class WindingCounter {
    public:
      explicit WindingCounter(Point point) noexcept;

      int get_winding() const;
      void add_line(Point start, Point end);
      void add_quadratic(Point start, Point control, Point end);
      void add_cubic(Point start, Point first, Point second, Point end);

    private:
      Point m_point;
      int m_winding;

      void add_monotonic(auto evaluate, double start, double end);
  };

  WindingCounter::WindingCounter(Point point) noexcept
    : m_point(point),
      m_winding(0) {}

  int WindingCounter::get_winding() const {
    return m_winding;
  }

  void WindingCounter::add_line(Point start, Point end) {
    auto is_upward = start.m_y <= m_point.m_y && m_point.m_y < end.m_y;
    auto is_downward = end.m_y <= m_point.m_y && m_point.m_y < start.m_y;
    if(!is_upward && !is_downward) {
      return;
    }
    auto t = (m_point.m_y - start.m_y) / (end.m_y - start.m_y);
    if(start.m_x + t * (end.m_x - start.m_x) > m_point.m_x) {
      if(is_upward) {
        ++m_winding;
      } else {
        --m_winding;
      }
    }
  }

  void WindingCounter::add_quadratic(Point start, Point control, Point end) {
    if(std::min({start.m_y, control.m_y, end.m_y}) > m_point.m_y ||
        std::max({start.m_y, control.m_y, end.m_y}) < m_point.m_y ||
        std::max({start.m_x, control.m_x, end.m_x}) <= m_point.m_x) {
      return;
    }
    auto evaluate = [&] (double t) {
      return evaluate_quadratic(start, control, end, t);
    };
    auto roots = get_quadratic_extrema(start, control, end, true);
    auto previous = 0.0;
    for(auto i = 0; i != roots.m_count; ++i) {
      if(roots.m_values[i] > 0 && roots.m_values[i] < 1) {
        add_monotonic(evaluate, previous, roots.m_values[i]);
        previous = roots.m_values[i];
      }
    }
    add_monotonic(evaluate, previous, 1);
  }

  void WindingCounter::add_cubic(
      Point start, Point first, Point second, Point end) {
    if(std::min({start.m_y, first.m_y, second.m_y, end.m_y}) > m_point.m_y ||
        std::max({start.m_y, first.m_y, second.m_y, end.m_y}) <
          m_point.m_y ||
        std::max({start.m_x, first.m_x, second.m_x, end.m_x}) <=
          m_point.m_x) {
      return;
    }
    auto evaluate = [&] (double t) {
      return evaluate_cubic(start, first, second, end, t);
    };
    auto roots = get_cubic_extrema(start, first, second, end, true);
    std::sort(roots.m_values.begin(), roots.m_values.begin() + roots.m_count);
    auto previous = 0.0;
    for(auto i = 0; i != roots.m_count; ++i) {
      if(roots.m_values[i] > previous && roots.m_values[i] < 1) {
        add_monotonic(evaluate, previous, roots.m_values[i]);
        previous = roots.m_values[i];
      }
    }
    add_monotonic(evaluate, previous, 1);
  }

  void WindingCounter::add_monotonic(auto evaluate, double start, double end) {
    auto start_point = evaluate(start);
    auto end_point = evaluate(end);
    auto is_upward =
      start_point.m_y <= m_point.m_y && m_point.m_y < end_point.m_y;
    auto is_downward =
      end_point.m_y <= m_point.m_y && m_point.m_y < start_point.m_y;
    if(!is_upward && !is_downward) {
      return;
    }
    auto low = start;
    auto high = end;
    for(auto i = 0; i != 64; ++i) {
      auto middle = (low + high) / 2;
      auto y = evaluate(middle).m_y;
      if((y <= m_point.m_y) == is_upward) {
        low = middle;
      } else {
        high = middle;
      }
    }
    if(evaluate((low + high) / 2).m_x > m_point.m_x) {
      if(is_upward) {
        ++m_winding;
      } else {
        --m_winding;
      }
    }
  }
}

Path::Path() noexcept
  : Path(FillRule::NON_ZERO) {}

Path::Path(FillRule rule) noexcept
  : m_fill_rule(rule),
    m_start(),
    m_current(),
    m_is_open(false) {}

FillRule Path::get_fill_rule() const {
  return m_fill_rule;
}

void Path::set_fill_rule(FillRule rule) {
  m_fill_rule = rule;
}

const std::vector<Segment>& Path::get_segments() const {
  return m_segments;
}

bool Path::is_empty() const {
  return m_segments.empty();
}

void Path::reserve(std::size_t count) {
  m_segments.reserve(count);
}

void Path::move_to(Point point) {
  if(!m_segments.empty() && m_segments.back().m_type == SegmentType::MOVE) {
    m_segments.pop_back();
  }
  m_segments.push_back(make_segment(SegmentType::MOVE, point));
  m_start = point;
  m_current = point;
  m_is_open = true;
}

void Path::line_to(Point point) {
  open();
  m_segments.push_back(make_segment(SegmentType::LINE, point));
  m_current = point;
}

void Path::quadratic_to(Point control, Point end) {
  open();
  m_segments.push_back(make_segment(SegmentType::QUADRATIC, control, end));
  m_current = end;
}

void Path::cubic_to(Point first, Point second, Point end) {
  open();
  m_segments.push_back(
    make_segment(SegmentType::CUBIC, first, second, end));
  m_current = end;
}

void Path::arc_to(Point corner, Point end) {
  open();
  auto segment = make_segment(SegmentType::CUBIC,
    m_current + ARC_CONTROL * (corner - m_current),
    end + ARC_CONTROL * (corner - end), end);
  segment.m_is_arc = true;
  m_segments.push_back(segment);
  m_current = end;
}

void Path::close() {
  if(!m_is_open) {
    return;
  }
  m_segments.push_back(make_segment(SegmentType::CLOSE, m_start));
  m_current = m_start;
  m_is_open = false;
}

void Path::open() {
  if(!m_is_open) {
    move_to(m_current);
  }
}

Path Rellu::transform(const Path& path, const Matrix& matrix) {
  auto result = Path(path.get_fill_rule());
  auto start = Point();
  auto current = Point();
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      result.move_to(matrix.map(points[0]));
      start = points[0];
      current = start;
    } else if(segment.m_type == SegmentType::LINE) {
      result.line_to(matrix.map(points[0]));
      current = points[0];
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      result.quadratic_to(matrix.map(points[0]), matrix.map(points[1]));
      current = points[1];
    } else if(segment.m_type == SegmentType::CUBIC) {
      if(segment.m_is_arc) {
        result.arc_to(matrix.map(current + (points[0] - current) /
          ARC_CONTROL), matrix.map(points[2]));
      } else {
        result.cubic_to(matrix.map(points[0]), matrix.map(points[1]),
          matrix.map(points[2]));
      }
      current = points[2];
    } else {
      result.close();
      current = start;
    }
  }
  return result;
}

Rect Rellu::get_bounds(const Path& path) {
  auto bounds = BoundsBuilder();
  auto current = Point();
  auto is_pending = false;
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      current = points[0];
      is_pending = true;
      continue;
    }
    if(is_pending) {
      bounds.add(current);
      is_pending = false;
    }
    if(segment.m_type == SegmentType::LINE) {
      current = points[0];
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      for(auto is_vertical : {false, true}) {
        auto roots =
          get_quadratic_extrema(current, points[0], points[1], is_vertical);
        for(auto i = 0; i != roots.m_count; ++i) {
          if(roots.m_values[i] > 0 && roots.m_values[i] < 1) {
            bounds.add(evaluate_quadratic(
              current, points[0], points[1], roots.m_values[i]));
          }
        }
      }
      current = points[1];
    } else if(segment.m_type == SegmentType::CUBIC) {
      for(auto is_vertical : {false, true}) {
        auto roots = get_cubic_extrema(
          current, points[0], points[1], points[2], is_vertical);
        for(auto i = 0; i != roots.m_count; ++i) {
          if(roots.m_values[i] > 0 && roots.m_values[i] < 1) {
            bounds.add(evaluate_cubic(current, points[0], points[1],
              points[2], roots.m_values[i]));
          }
        }
      }
      current = points[2];
    } else {
      current = points[0];
    }
    bounds.add(current);
  }
  return bounds.get_bounds();
}

Rect Rellu::get_bounds(const Path& path, const Matrix& matrix) {
  return get_bounds(transform(path, matrix));
}

bool Rellu::contains(const Path& path, Point point, FillRule rule) {
  auto counter = WindingCounter(point);
  auto start = Point();
  auto current = Point();
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      counter.add_line(current, start);
      start = points[0];
      current = points[0];
    } else if(segment.m_type == SegmentType::LINE) {
      counter.add_line(current, points[0]);
      current = points[0];
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      counter.add_quadratic(current, points[0], points[1]);
      current = points[1];
    } else if(segment.m_type == SegmentType::CUBIC) {
      counter.add_cubic(current, points[0], points[1], points[2]);
      current = points[2];
    } else {
      counter.add_line(current, start);
      current = start;
    }
  }
  counter.add_line(current, start);
  if(rule == FillRule::NON_ZERO) {
    return counter.get_winding() != 0;
  }
  return counter.get_winding() % 2 != 0;
}

bool Rellu::contains(const Path& path, Point point) {
  return contains(path, point, path.get_fill_rule());
}

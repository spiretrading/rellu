#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>
#include "Rellu/Geometry/Polyline.hpp"

using namespace Rellu;

namespace {
  int get_subdivisions(double deviation, double tolerance) {
    return std::max(1, static_cast<int>(std::ceil(
      std::sqrt(deviation / tolerance))));
  }

  void add_quadratic(std::vector<Point>& points, Point start, Point control,
      Point end, double tolerance) {
    auto count = get_subdivisions(
      length(start - 2 * control + end) / 4, tolerance);
    for(auto i = 1; i < count; ++i) {
      auto t = static_cast<double>(i) / count;
      auto s = 1 - t;
      points.push_back(s * s * start + 2 * s * t * control + t * t * end);
    }
    points.push_back(end);
  }

  void add_cubic(std::vector<Point>& points, Point start, Point first,
      Point second, Point end, double tolerance) {
    auto curvature = std::max(length(start - 2 * first + second),
      length(first - 2 * second + end));
    auto count = get_subdivisions(3 * curvature / 4, tolerance);
    for(auto i = 1; i < count; ++i) {
      auto t = static_cast<double>(i) / count;
      auto s = 1 - t;
      points.push_back(s * s * s * start + 3 * s * s * t * first +
        3 * s * t * t * second + t * t * t * end);
    }
    points.push_back(end);
  }

  void add_polyline(std::vector<Polyline>& polylines, Polyline polyline) {
    if(polyline.m_is_closed && polyline.m_points.size() > 1 &&
        polyline.m_points.back() == polyline.m_points.front()) {
      polyline.m_points.pop_back();
    }
    if(polyline.m_points.size() > 1 || polyline.m_is_closed) {
      polylines.push_back(std::move(polyline));
    }
  }
}

namespace {
  constexpr auto CORNER_ANGLE = 0.7;
  constexpr auto CORNER_EXTENT = 24.0;
  constexpr auto MAXIMUM_DEPTH = 24;
  constexpr auto REPARAMETERIZATIONS = 4;
  constexpr auto REPEAT_SEPARATION = 1e-3;

  using Cubic = std::array<Point, 4>;

  Point normalize(Point vector) {
    auto magnitude = length(vector);
    if(magnitude == 0) {
      return vector;
    }
    return vector / magnitude;
  }

  Point evaluate(const Cubic& cubic, double t) {
    auto s = 1 - t;
    return s * s * s * cubic[0] + 3 * s * s * t * cubic[1] +
      3 * s * t * t * cubic[2] + t * t * t * cubic[3];
  }

  Point get_derivative(const Cubic& cubic, double t) {
    auto s = 1 - t;
    return 3 * s * s * (cubic[1] - cubic[0]) +
      6 * s * t * (cubic[2] - cubic[1]) + 3 * t * t * (cubic[3] - cubic[2]);
  }

  Point get_second_derivative(const Cubic& cubic, double t) {
    return 6 * (1 - t) * (cubic[2] - 2 * cubic[1] + cubic[0]) +
      6 * t * (cubic[3] - 2 * cubic[2] + cubic[1]);
  }

  double get_distance(Point point, Point start, Point end) {
    auto chord = end - start;
    auto squared = dot(chord, chord);
    if(squared == 0) {
      return distance(point, start);
    }
    auto t = std::clamp(dot(point - start, chord) / squared, 0.0, 1.0);
    return distance(point, start + chord * t);
  }

  std::vector<double> parameterize(
      const std::vector<Point>& points, std::size_t first, std::size_t last) {
    auto parameters = std::vector<double>(1, 0);
    for(auto i = first + 1; i <= last; ++i) {
      parameters.push_back(
        parameters.back() + distance(points[i], points[i - 1]));
    }
    if(parameters.back() > 0) {
      for(auto& parameter : parameters) {
        parameter /= parameters.back();
      }
    }
    return parameters;
  }

  Cubic generate(const std::vector<Point>& points, std::size_t first,
      std::size_t last, const std::vector<double>& parameters, Point start,
      Point end) {
    auto& p0 = points[first];
    auto& p3 = points[last];
    auto c00 = 0.0;
    auto c01 = 0.0;
    auto c11 = 0.0;
    auto x0 = 0.0;
    auto x1 = 0.0;
    for(auto i = std::size_t(0); i != parameters.size(); ++i) {
      auto t = parameters[i];
      auto s = 1 - t;
      auto b0 = s * s * s;
      auto b1 = 3 * s * s * t;
      auto b2 = 3 * s * t * t;
      auto b3 = t * t * t;
      auto a1 = start * b1;
      auto a2 = end * b2;
      c00 += dot(a1, a1);
      c01 += dot(a1, a2);
      c11 += dot(a2, a2);
      auto residual = points[first + i] - (p0 * (b0 + b1) + p3 * (b2 + b3));
      x0 += dot(a1, residual);
      x1 += dot(a2, residual);
    }
    auto chord = distance(p0, p3);
    auto determinant = c00 * c11 - c01 * c01;
    auto alpha1 = chord / 3;
    auto alpha2 = chord / 3;
    if(std::abs(determinant) > 1e-12) {
      auto first_alpha = (x0 * c11 - x1 * c01) / determinant;
      auto second_alpha = (c00 * x1 - c01 * x0) / determinant;
      if(first_alpha > 1e-6 * chord && second_alpha > 1e-6 * chord) {
        alpha1 = first_alpha;
        alpha2 = second_alpha;
      }
    }
    return Cubic({p0, p0 + start * alpha1, p3 + end * alpha2, p3});
  }

  std::pair<double, std::size_t> get_error(const std::vector<Point>& points,
      std::size_t first, const Cubic& cubic,
      const std::vector<double>& parameters) {
    auto error = 0.0;
    auto index = first + parameters.size() / 2;
    for(auto i = std::size_t(1); i + 1 < parameters.size(); ++i) {
      auto deviation = distance(evaluate(cubic, parameters[i]),
        points[first + i]);
      if(deviation > error) {
        error = deviation;
        index = first + i;
      }
    }
    return std::pair(error, index);
  }

  void reparameterize(const std::vector<Point>& points, std::size_t first,
      const Cubic& cubic, std::vector<double>& parameters) {
    for(auto i = std::size_t(0); i != parameters.size(); ++i) {
      auto t = parameters[i];
      auto offset = evaluate(cubic, t) - points[first + i];
      auto derivative = get_derivative(cubic, t);
      auto denominator = dot(derivative, derivative) +
        dot(offset, get_second_derivative(cubic, t));
      if(denominator != 0) {
        parameters[i] =
          std::clamp(t - dot(offset, derivative) / denominator, 0.0, 1.0);
      }
    }
  }

  void fit_run(Path& path, const std::vector<Point>& points,
      std::size_t first, std::size_t last, Point start, Point end,
      double tolerance, int depth) {
    auto chord = points[last] - points[first];
    auto direction = normalize(chord);
    auto is_straight = std::max(std::abs(cross(start, direction)),
      std::abs(cross(end, direction))) * length(chord) / 4 <= tolerance;
    for(auto i = first + 1; i < last && is_straight; ++i) {
      is_straight = get_distance(points[i], points[first], points[last]) <=
        tolerance;
    }
    if(is_straight) {
      path.line_to(points[last]);
      return;
    }
    if(last - first == 1) {
      auto handle = length(chord) / 3;
      path.cubic_to(points[first] + start * handle,
        points[last] + end * handle, points[last]);
      return;
    }
    auto parameters = parameterize(points, first, last);
    auto cubic = generate(points, first, last, parameters, start, end);
    auto [error, split] = get_error(points, first, cubic, parameters);
    for(auto i = 0; error > tolerance && error <= 4 * tolerance &&
        i != REPARAMETERIZATIONS; ++i) {
      reparameterize(points, first, cubic, parameters);
      cubic = generate(points, first, last, parameters, start, end);
      std::tie(error, split) = get_error(points, first, cubic, parameters);
    }
    if(error <= tolerance) {
      path.cubic_to(cubic[1], cubic[2], cubic[3]);
      return;
    }
    if(depth == MAXIMUM_DEPTH) {
      for(auto i = first + 1; i <= last; ++i) {
        path.line_to(points[i]);
      }
      return;
    }
    auto tangent = normalize(points[split - 1] - points[split + 1]);
    fit_run(path, points, first, split, start, tangent, tolerance, depth + 1);
    fit_run(path, points, split, last, -tangent, end, tolerance, depth + 1);
  }

  std::vector<Point> remove_repeats(
      const Polyline& polyline, double tolerance) {
    auto separation = REPEAT_SEPARATION * tolerance;
    auto points = std::vector<Point>();
    for(auto& point : polyline.m_points) {
      if(points.empty() || distance(point, points.back()) > separation) {
        points.push_back(point);
      }
    }
    if(polyline.m_is_closed && points.size() > 1 &&
        distance(points.back(), points.front()) <= separation) {
      points.pop_back();
    }
    return points;
  }

  bool is_corner(Point previous, Point point, Point next, double tolerance) {
    auto before = point - previous;
    auto after = next - point;
    auto angle =
      std::atan2(std::abs(cross(before, after)), dot(before, after));
    return angle > CORNER_ANGLE || std::min(length(before), length(after)) *
      angle > CORNER_EXTENT * tolerance;
  }

  std::vector<std::size_t> find_anchors(
      const std::vector<Point>& points, double tolerance) {
    auto count = points.size();
    auto anchors = std::vector<std::size_t>();
    for(auto i = std::size_t(0); i != count; ++i) {
      if(is_corner(points[(i + count - 1) % count], points[i],
          points[(i + 1) % count], tolerance)) {
        anchors.push_back(i);
      }
    }
    if(!anchors.empty()) {
      return anchors;
    }
    auto extremes = std::array<std::size_t, 4>({0, 0, 0, 0});
    for(auto i = std::size_t(1); i != count; ++i) {
      if(points[i].m_y < points[extremes[0]].m_y) {
        extremes[0] = i;
      }
      if(points[i].m_x > points[extremes[1]].m_x) {
        extremes[1] = i;
      }
      if(points[i].m_y > points[extremes[2]].m_y) {
        extremes[2] = i;
      }
      if(points[i].m_x < points[extremes[3]].m_x) {
        extremes[3] = i;
      }
    }
    anchors.assign(extremes.begin(), extremes.end());
    std::sort(anchors.begin(), anchors.end());
    anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
    return anchors;
  }

  void fit_closed(Path& path, const std::vector<Point>& points,
      double tolerance) {
    auto count = points.size();
    auto anchors = find_anchors(points, tolerance);
    auto corners = std::vector<bool>(count, false);
    for(auto i = std::size_t(0); i != count; ++i) {
      corners[i] = is_corner(points[(i + count - 1) % count], points[i],
        points[(i + 1) % count], tolerance);
    }
    auto get_tangent = [&] (std::size_t index, bool is_forward) {
      auto previous = points[(index + count - 1) % count];
      auto next = points[(index + 1) % count];
      if(corners[index]) {
        return normalize((is_forward ? next : previous) - points[index]);
      }
      auto tangent = normalize(next - previous);
      return is_forward ? tangent : -tangent;
    };
    path.move_to(points[anchors.front()]);
    for(auto i = std::size_t(0); i != anchors.size(); ++i) {
      auto from = anchors[i];
      auto to = anchors[(i + 1) % anchors.size()];
      auto run = std::vector<Point>();
      for(auto j = from; ; j = (j + 1) % count) {
        run.push_back(points[j]);
        if(j == to && run.size() > 1) {
          break;
        }
      }
      fit_run(path, run, 0, run.size() - 1, get_tangent(from, true),
        get_tangent(to, false), tolerance, 0);
    }
    path.close();
  }
}

Path Rellu::fit_curves(
    const std::vector<Polyline>& polylines, double tolerance) {
  auto path = Path();
  for(auto& polyline : polylines) {
    auto points = remove_repeats(polyline, tolerance);
    if(points.empty()) {
      continue;
    }
    if(polyline.m_is_closed && points.size() > 2) {
      fit_closed(path, points, tolerance);
      continue;
    }
    path.move_to(points.front());
    if(points.size() > 1) {
      auto run = points;
      fit_run(path, run, 0, run.size() - 1,
        normalize(run[1] - run[0]),
        normalize(run[run.size() - 2] - run.back()), tolerance, 0);
    }
    if(polyline.m_is_closed) {
      path.close();
    }
  }
  return path;
}

std::vector<Polyline> Rellu::flatten(const Path& path, double tolerance) {
  auto polylines = std::vector<Polyline>();
  auto polyline = Polyline();
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      add_polyline(polylines, std::move(polyline));
      polyline = Polyline();
      polyline.m_points.push_back(points[0]);
    } else if(segment.m_type == SegmentType::LINE) {
      polyline.m_points.push_back(points[0]);
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      add_quadratic(polyline.m_points, polyline.m_points.back(), points[0],
        points[1], tolerance);
    } else if(segment.m_type == SegmentType::CUBIC) {
      add_cubic(polyline.m_points, polyline.m_points.back(), points[0],
        points[1], points[2], tolerance);
    } else {
      polyline.m_is_closed = true;
    }
  }
  add_polyline(polylines, std::move(polyline));
  return polylines;
}

#include <algorithm>
#include <cmath>
#include <limits>
#include "Rellu/Geometry/Contour.hpp"

using namespace Rellu;

namespace {
  constexpr auto SMOOTH_TOLERANCE = 1e-3;
  constexpr auto SAMPLES = 32;
  constexpr auto REFINEMENTS = 40;

  bool is_line(const Anchor& start, const Anchor& end) {
    return start.m_out == start.m_point && end.m_in == end.m_point;
  }

  const Anchor& get_end(const Contour& contour, std::size_t segment) {
    return contour.m_anchors[(segment + 1) % contour.m_anchors.size()];
  }

  Point evaluate_cubic(
      Point start, Point first, Point second, Point end, double t) {
    auto s = 1 - t;
    return s * s * s * start + 3 * s * s * t * first +
      3 * s * t * t * second + t * t * t * end;
  }

  ContourPosition find_nearest_line(
      Point start, Point end, std::size_t segment, Point point) {
    auto direction = end - start;
    auto squared = dot(direction, direction);
    auto t = 0.0;
    if(squared != 0) {
      t = std::clamp(dot(point - start, direction) / squared, 0.0, 1.0);
    }
    return ContourPosition(segment, t, interpolate(start, end, t));
  }

  ContourPosition find_nearest_curve(
      const Contour& contour, std::size_t segment, Point point) {
    auto distance_at = [&] (double t) {
      return distance(evaluate(contour, segment, t), point);
    };
    auto best = 0.0;
    auto best_distance = distance_at(0);
    for(auto i = 1; i <= SAMPLES; ++i) {
      auto t = static_cast<double>(i) / SAMPLES;
      if(auto d = distance_at(t); d < best_distance) {
        best = t;
        best_distance = d;
      }
    }
    auto low = std::max(0.0, best - 1.0 / SAMPLES);
    auto high = std::min(1.0, best + 1.0 / SAMPLES);
    for(auto i = 0; i != REFINEMENTS; ++i) {
      auto first = low + (high - low) / 3;
      auto second = high - (high - low) / 3;
      if(distance_at(first) < distance_at(second)) {
        high = second;
      } else {
        low = first;
      }
    }
    auto t = (low + high) / 2;
    if(distance_at(t) > best_distance) {
      t = best;
    }
    return ContourPosition(segment, t, evaluate(contour, segment, t));
  }

  Point get_direction(Point vector) {
    auto magnitude = length(vector);
    if(magnitude == 0) {
      return Point(0, 0);
    }
    return vector / magnitude;
  }
}

bool Rellu::is_smooth(const Anchor& anchor) {
  auto in = anchor.m_in - anchor.m_point;
  auto out = anchor.m_out - anchor.m_point;
  auto in_length = length(in);
  auto out_length = length(out);
  if(in_length == 0 || out_length == 0) {
    return false;
  }
  return dot(in, out) < 0 &&
    std::abs(cross(in, out)) <= SMOOTH_TOLERANCE * in_length * out_length;
}

std::vector<Contour> Rellu::to_contours(const Path& path) {
  auto contours = std::vector<Contour>();
  auto finish = [&] {
    if(contours.empty()) {
      return;
    }
    auto& contour = contours.back();
    auto& anchors = contour.m_anchors;
    if(contour.m_is_closed && anchors.size() > 1 &&
        anchors.back().m_point == anchors.front().m_point) {
      anchors.front().m_in = anchors.back().m_in;
      anchors.pop_back();
    }
  };
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      finish();
      contours.emplace_back();
      contours.back().m_anchors.push_back(make_anchor(points[0]));
      continue;
    }
    if(contours.empty() || contours.back().m_is_closed) {
      continue;
    }
    auto& anchors = contours.back().m_anchors;
    if(segment.m_type == SegmentType::LINE) {
      anchors.push_back(make_anchor(points[0]));
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      auto start = anchors.back().m_point;
      anchors.back().m_out = start + (points[0] - start) * (2.0 / 3);
      anchors.push_back(Anchor(points[1],
        points[1] + (points[0] - points[1]) * (2.0 / 3), points[1]));
    } else if(segment.m_type == SegmentType::CUBIC) {
      anchors.back().m_out = points[0];
      anchors.push_back(Anchor(points[2], points[1], points[2]));
    } else if(segment.m_type == SegmentType::CLOSE) {
      contours.back().m_is_closed = true;
    }
  }
  finish();
  return contours;
}

Path Rellu::to_path(std::span<const Contour> contours, FillRule rule) {
  auto path = Path(rule);
  auto add_segment = [&] (const Anchor& start, const Anchor& end) {
    if(is_line(start, end)) {
      path.line_to(end.m_point);
    } else {
      path.cubic_to(start.m_out, end.m_in, end.m_point);
    }
  };
  for(auto& contour : contours) {
    auto& anchors = contour.m_anchors;
    if(anchors.empty()) {
      continue;
    }
    path.move_to(anchors.front().m_point);
    for(auto i = std::size_t(1); i < anchors.size(); ++i) {
      add_segment(anchors[i - 1], anchors[i]);
    }
    if(contour.m_is_closed && anchors.size() > 1) {
      if(!is_line(anchors.back(), anchors.front())) {
        add_segment(anchors.back(), anchors.front());
      }
      path.close();
    }
  }
  return path;
}

Contour Rellu::transform(const Contour& contour, const Matrix& matrix) {
  auto result = contour;
  for(auto& anchor : result.m_anchors) {
    anchor.m_point = matrix.map(anchor.m_point);
    anchor.m_in = matrix.map(anchor.m_in);
    anchor.m_out = matrix.map(anchor.m_out);
  }
  return result;
}

std::size_t Rellu::get_segment_count(const Contour& contour) {
  auto count = contour.m_anchors.size();
  if(count < 2) {
    return 0;
  } else if(contour.m_is_closed) {
    return count;
  }
  return count - 1;
}

Point Rellu::evaluate(const Contour& contour, std::size_t segment, double t) {
  auto& start = contour.m_anchors[segment];
  auto& end = get_end(contour, segment);
  return evaluate_cubic(start.m_point, start.m_out, end.m_in, end.m_point, t);
}

std::optional<ContourPosition> Rellu::find_nearest(
    const Contour& contour, Point point) {
  auto nearest = std::optional<ContourPosition>();
  auto nearest_distance = std::numeric_limits<double>::infinity();
  for(auto i = std::size_t(0); i != get_segment_count(contour); ++i) {
    auto& start = contour.m_anchors[i];
    auto& end = get_end(contour, i);
    auto position = [&] {
      if(is_line(start, end)) {
        return find_nearest_line(start.m_point, end.m_point, i, point);
      }
      return find_nearest_curve(contour, i, point);
    }();
    if(auto d = distance(position.m_point, point); d < nearest_distance) {
      nearest = position;
      nearest_distance = d;
    }
  }
  return nearest;
}

std::size_t Rellu::split_segment(
    Contour& contour, std::size_t segment, double t) {
  auto& anchors = contour.m_anchors;
  auto end_index = (segment + 1) % anchors.size();
  auto start = anchors[segment];
  auto end = anchors[end_index];
  auto index = segment + 1;
  if(is_line(start, end)) {
    anchors.insert(anchors.begin() + index,
      make_anchor(interpolate(start.m_point, end.m_point, t)));
    return index;
  }
  auto a = interpolate(start.m_point, start.m_out, t);
  auto b = interpolate(start.m_out, end.m_in, t);
  auto c = interpolate(end.m_in, end.m_point, t);
  auto d = interpolate(a, b, t);
  auto e = interpolate(b, c, t);
  auto f = interpolate(d, e, t);
  anchors[segment].m_out = a;
  anchors[end_index].m_in = c;
  anchors.insert(anchors.begin() + index, Anchor(f, d, e));
  return index;
}

void Rellu::remove_anchor(Contour& contour, std::size_t index) {
  contour.m_anchors.erase(contour.m_anchors.begin() + index);
}

void Rellu::set_smooth(Contour& contour, std::size_t index, bool is_smooth) {
  auto& anchors = contour.m_anchors;
  auto& anchor = anchors[index];
  if(!is_smooth) {
    anchor.m_in = anchor.m_point;
    anchor.m_out = anchor.m_point;
    return;
  }
  auto count = anchors.size();
  auto previous = std::optional<Point>();
  auto next = std::optional<Point>();
  if(index != 0) {
    previous = anchors[index - 1].m_point;
  } else if(contour.m_is_closed && count > 1) {
    previous = anchors.back().m_point;
  }
  if(index + 1 != count) {
    next = anchors[index + 1].m_point;
  } else if(contour.m_is_closed && count > 1) {
    next = anchors.front().m_point;
  }
  if(!previous && !next) {
    return;
  }
  auto point = anchor.m_point;
  auto before = previous ? *previous : point - (*next - point);
  auto after = next ? *next : point + (point - *previous);
  auto direction = get_direction(after - before);
  if(direction == Point(0, 0)) {
    direction = get_direction(after - point);
  }
  if(direction == Point(0, 0)) {
    return;
  }
  anchor.m_in = point - direction * (distance(point, before) / 3);
  anchor.m_out = point + direction * (distance(point, after) / 3);
}

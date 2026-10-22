#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <variant>
#include <vector>
#include <QPainterPath>
#include <QPolygonF>
#include "Rellu/Ui/GradientHandles.hpp"

using namespace Rellu;

namespace {
  constexpr auto TRACK_SEGMENTS = 96;
  constexpr auto ANGLE_STEP = 15.0;
  constexpr auto MINIMUM_RADIUS = 0.01;
  constexpr auto IDENTITY_TOLERANCE = 1e-9;
  const auto HANDLE_COLOR = QColor(0x26, 0x80, 0xEB);
  const auto TRACK_SHADOW = QColor(0, 0, 0, 90);
  const auto GUIDE_COLOR = QColor(0, 0, 0, 72);

  struct Ellipse {
    Point m_center;
    double m_angle;
    double m_radius_x;
    double m_radius_y;
    double m_flip;
  };

  QPointF to_qpoint(Point point) {
    return QPointF(point.m_x, point.m_y);
  }

  double get_length(Point point) {
    return std::hypot(point.m_x, point.m_y);
  }

  Matrix get_box(const Rect& bounds) {
    return make_translation(bounds.m_x, bounds.m_y) *
      make_scale(bounds.m_width, bounds.m_height);
  }

  Matrix get_radial_mapping(const GradientFill& fill, const Rect& bounds) {
    if(bounds.m_width > 0 && bounds.m_height > 0) {
      return get_radial_transform(fill, bounds);
    }
    return get_box(bounds) * fill.m_transform;
  }

  Matrix get_unit_to_view(const GradientFrame& frame) {
    return frame.m_to_view * get_box(frame.m_bounds);
  }

  Point to_unit(const GradientFrame& frame, Point position) {
    if(auto inverse = invert(get_unit_to_view(frame))) {
      return inverse->map(position);
    }
    return Point(0, 0);
  }

  Point to_local(const GradientFrame& frame, Point position) {
    if(auto inverse = invert(frame.m_to_view)) {
      return inverse->map(position);
    }
    return Point(0, 0);
  }

  Color interpolate_color(const std::vector<GradientStop>& stops,
      double offset) {
    if(stops.empty()) {
      return Color(0, 0, 0, 0);
    } else if(offset <= stops.front().m_offset) {
      return stops.front().m_color;
    } else if(offset >= stops.back().m_offset) {
      return stops.back().m_color;
    }
    for(auto i = std::size_t(1); i != stops.size(); ++i) {
      auto& next = stops[i];
      if(offset > next.m_offset) {
        continue;
      }
      auto& previous = stops[i - 1];
      auto span = next.m_offset - previous.m_offset;
      auto t = span > 0 ? (offset - previous.m_offset) / span : 0;
      auto blend = [&] (double from, double to) {
        return from + t * (to - from);
      };
      return Color(blend(previous.m_color.m_red, next.m_color.m_red),
        blend(previous.m_color.m_green, next.m_color.m_green),
        blend(previous.m_color.m_blue, next.m_color.m_blue),
        blend(previous.m_color.m_alpha, next.m_color.m_alpha));
    }
    return stops.back().m_color;
  }

  int insert_stop(std::vector<GradientStop>& stops, GradientStop stop) {
    auto index = static_cast<int>(std::upper_bound(stops.begin(),
      stops.end(), stop.m_offset, [] (double offset, const auto& stop) {
        return offset < stop.m_offset;
      }) - stops.begin());
    stops.insert(stops.begin() + index, stop);
    return index;
  }

  int move_stop(std::vector<GradientStop>& stops, int index, double offset) {
    auto stop = stops[index];
    stop.m_offset = offset;
    stops.erase(stops.begin() + index);
    return insert_stop(stops, stop);
  }

  Ellipse get_ellipse(const GradientFill& fill, const Rect& bounds) {
    auto& radial = std::get<RadialGradient>(fill.m_geometry);
    auto mapping = get_radial_mapping(fill, bounds);
    auto scale_x = std::hypot(mapping.m_a, mapping.m_b);
    auto scale_y = scale_x > 0 ? mapping.get_determinant() / scale_x : 0;
    return Ellipse(mapping.map(radial.m_center),
      std::atan2(mapping.m_b, mapping.m_a), scale_x * radial.m_radius,
      std::abs(scale_y) * radial.m_radius, scale_y < 0 ? -1 : 1);
  }

  Point get_point(const Ellipse& ellipse, double x, double y) {
    return ellipse.m_center +
      make_rotation(ellipse.m_angle).map(Point(x, ellipse.m_flip * y));
  }

  bool is_identity(const Matrix& matrix) {
    return std::abs(matrix.m_a - 1) < IDENTITY_TOLERANCE &&
      std::abs(matrix.m_b) < IDENTITY_TOLERANCE &&
      std::abs(matrix.m_c) < IDENTITY_TOLERANCE &&
      std::abs(matrix.m_d - 1) < IDENTITY_TOLERANCE &&
      std::abs(matrix.m_tx) < IDENTITY_TOLERANCE &&
      std::abs(matrix.m_ty) < IDENTITY_TOLERANCE;
  }

  GradientFill make_radial(
      GradientFill fill, const Ellipse& ellipse, const Rect& bounds) {
    auto& radial = std::get<RadialGradient>(fill.m_geometry);
    if(!(radial.m_radius > 0)) {
      radial.m_radius = 0.5;
    }
    auto radius = radial.m_radius;
    auto center = Point(
      (ellipse.m_center.m_x - bounds.m_x) / bounds.m_width,
      (ellipse.m_center.m_y - bounds.m_y) / bounds.m_height);
    radial.m_center = center;
    radial.m_focus = center;
    auto transform = make_translation(center.m_x, center.m_y) *
      make_scale(1 / bounds.m_width, 1 / bounds.m_height) *
      make_rotation(ellipse.m_angle) * make_scale(ellipse.m_radius_x / radius,
        ellipse.m_flip * ellipse.m_radius_y / radius) *
      make_translation(-center.m_x, -center.m_y);
    if(is_identity(transform)) {
      transform = Matrix();
    }
    fill.m_transform = transform;
    return fill;
  }

  double get_radial_offset(
      const GradientFill& fill, const Rect& bounds, Point local) {
    auto& radial = std::get<RadialGradient>(fill.m_geometry);
    auto inverse = invert(get_radial_mapping(fill, bounds));
    if(!inverse || !(radial.m_radius > 0)) {
      return 0;
    }
    return std::clamp(get_length(inverse->map(local) - radial.m_center) /
      radial.m_radius, 0.0, 1.0);
  }

  Point snap_unit(const GradientFrame& frame, Point unit) {
    auto to_view = get_unit_to_view(frame);
    auto view = to_view.map(unit);
    auto snapped = unit;
    for(auto is_x : {true, false}) {
      auto best = GradientHandles::SNAP_DISTANCE;
      for(auto value : {0.0, 0.5, 1.0}) {
        auto candidate = unit;
        if(is_x) {
          candidate.m_x = value;
        } else {
          candidate.m_y = value;
        }
        auto gap = get_length(to_view.map(candidate) - view);
        if(gap <= best) {
          best = gap;
          if(is_x) {
            snapped.m_x = value;
          } else {
            snapped.m_y = value;
          }
        }
      }
    }
    return snapped;
  }

  double get_segment_distance(Point point, Point start, Point end) {
    auto direction = end - start;
    auto length = dot(direction, direction);
    auto t = length > 0 ?
      std::clamp(dot(point - start, direction) / length, 0.0, 1.0) : 0.0;
    return get_length(point - (start + direction * t));
  }

  void paint_track_line(QPainter& painter, Point start, Point end) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(TRACK_SHADOW, 3));
    painter.drawLine(to_qpoint(start), to_qpoint(end));
    painter.setPen(QPen(Qt::white, 1));
    painter.drawLine(to_qpoint(start), to_qpoint(end));
  }

  void paint_handle(QPainter& painter, Point point, double radius) {
    painter.setPen(QPen(HANDLE_COLOR, 1));
    painter.setBrush(Qt::white);
    painter.drawEllipse(to_qpoint(point), radius, radius);
  }

  void paint_stop(QPainter& painter, Point point, const Color& color,
      bool is_selected) {
    painter.setPen(QPen(is_selected ? HANDLE_COLOR : QColor(Qt::white),
      is_selected ? 2.5 : 2));
    painter.setBrush(QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue)));
    painter.drawEllipse(to_qpoint(point), GradientHandles::HANDLE_RADIUS,
      GradientHandles::HANDLE_RADIUS);
  }

  struct Track {
    Point m_start;
    Point m_end;
  };

  Track get_track(const GradientFill& fill, const GradientFrame& frame) {
    if(auto linear = std::get_if<LinearGradient>(&fill.m_geometry)) {
      auto to_view = get_unit_to_view(frame);
      return Track(to_view.map(linear->m_start), to_view.map(linear->m_end));
    }
    auto ellipse = get_ellipse(fill, frame.m_bounds);
    return Track(frame.m_to_view.map(ellipse.m_center),
      frame.m_to_view.map(get_point(ellipse, ellipse.m_radius_x, 0)));
  }

  bool is_start(const GradientFill& fill, int index) {
    return index == 0 && fill.m_stops[index].m_offset == 0;
  }

  bool is_end(const GradientFill& fill, int index) {
    return index + 1 == static_cast<int>(fill.m_stops.size()) &&
      fill.m_stops[index].m_offset == 1;
  }

  Point get_angular_point(const GradientFill& fill, const Matrix& to_view,
      double offset) {
    auto& angular = std::get<AngularGradient>(fill.m_geometry);
    auto radians =
      2 * std::numbers::pi * (offset + angular.m_rotation / 360);
    return to_view.map(angular.m_center +
      Point(0.5 * std::cos(radians), 0.5 * std::sin(radians)));
  }

  void turn_edge(Ellipse& ellipse, Point local, bool is_stepped) {
    auto delta = local - ellipse.m_center;
    auto distance = get_length(delta);
    auto degrees = std::atan2(delta.m_y, delta.m_x) * 180 / std::numbers::pi;
    if(is_stepped) {
      if(degrees < 0) {
        degrees += 360;
      }
      degrees = ANGLE_STEP * std::round(degrees / ANGLE_STEP);
    }
    auto ratio = ellipse.m_radius_x > 0 ?
      ellipse.m_radius_y / ellipse.m_radius_x : 1;
    ellipse.m_angle = degrees * std::numbers::pi / 180;
    ellipse.m_radius_x = std::max(MINIMUM_RADIUS, distance);
    ellipse.m_radius_y = std::max(MINIMUM_RADIUS, ratio * distance);
  }

  int normalize_stops(std::vector<GradientStop>& stops, int index) {
    if(stops.front().m_offset != 0) {
      stops.insert(stops.begin(), GradientStop(0, stops.front().m_color));
      ++index;
    }
    if(stops.back().m_offset != 1) {
      stops.push_back(GradientStop(1, stops.back().m_color));
    }
    return index;
  }

  void sort_stops(std::vector<GradientStop>& stops) {
    std::stable_sort(stops.begin(), stops.end(),
      [] (const auto& left, const auto& right) {
        return left.m_offset < right.m_offset;
      });
  }

  Point get_unit_point(const Rect& bounds, Point local) {
    if(auto inverse = invert(get_box(bounds))) {
      return inverse->map(local);
    }
    return Point(0, 0);
  }

  Point get_track_point(
      const GradientFill& fill, const Rect& bounds, double offset) {
    if(auto linear = std::get_if<LinearGradient>(&fill.m_geometry)) {
      return get_box(bounds).map(
        linear->m_start + (linear->m_end - linear->m_start) * offset);
    } else if(std::holds_alternative<RadialGradient>(fill.m_geometry)) {
      auto ellipse = get_ellipse(fill, bounds);
      return get_point(ellipse, ellipse.m_radius_x * offset, 0);
    }
    return get_angular_point(fill, get_box(bounds), offset);
  }

  double get_track_offset(
      const GradientFill& fill, const Rect& bounds, Point local) {
    auto unit = get_unit_point(bounds, local);
    if(auto linear = std::get_if<LinearGradient>(&fill.m_geometry)) {
      auto direction = linear->m_end - linear->m_start;
      auto length = dot(direction, direction);
      return length > 0 ? std::clamp(
        dot(unit - linear->m_start, direction) / length, 0.0, 1.0) : 0.0;
    } else if(std::holds_alternative<RadialGradient>(fill.m_geometry)) {
      auto ellipse = get_ellipse(fill, bounds);
      if(!(ellipse.m_radius_x > 0)) {
        return 0;
      }
      auto x = make_rotation(-ellipse.m_angle).map(local - ellipse.m_center).
        m_x;
      return std::clamp(x / ellipse.m_radius_x, 0.0, 1.0);
    }
    return get_angular_offset(std::get<AngularGradient>(fill.m_geometry), unit);
  }

  GradientEdit nudge_end(const GradientFill& fill, const Rect& bounds,
      std::vector<GradientStop> stops, int index, Point local) {
    auto edit = GradientEdit();
    edit.m_is_handled = true;
    auto changed = fill;
    auto unit = get_unit_point(bounds, local);
    if(auto linear = std::get_if<LinearGradient>(&changed.m_geometry)) {
      if(index == 0) {
        linear->m_start = unit;
      } else {
        linear->m_end = unit;
      }
    } else if(auto angular =
        std::get_if<AngularGradient>(&changed.m_geometry)) {
      auto rotation =
        360 * get_angular_offset(*angular, unit) + angular->m_rotation;
      auto shift = (std::fmod(rotation + 720, 360) -
        std::fmod(angular->m_rotation + 720, 360)) / 360;
      for(auto& stop : changed.m_stops) {
        if(stop.m_offset != 0 && stop.m_offset != 1) {
          auto offset = stop.m_offset - shift;
          stop.m_offset = offset - std::floor(offset);
        }
      }
      sort_stops(changed.m_stops);
      angular->m_rotation = rotation;
    } else {
      auto ellipse = get_ellipse(fill, bounds);
      if(index == 0) {
        ellipse.m_center = local;
      } else {
        turn_edge(ellipse, local, false);
      }
      changed.m_stops = std::move(stops);
      edit.m_fill = make_radial(std::move(changed), ellipse, bounds);
      edit.m_stop = index;
      return edit;
    }
    edit.m_fill = std::move(changed);
    return edit;
  }

  GradientEdit nudge_stop(const GradientFill& fill, const Rect& bounds,
      int stop, Point delta) {
    auto stops = fill.m_stops;
    auto index = normalize_stops(stops, stop);
    auto count = static_cast<int>(stops.size());
    auto local = get_track_point(fill, bounds, stops[index].m_offset) + delta;
    if(index == 0 || index + 1 == count) {
      return nudge_end(fill, bounds, std::move(stops), index, local);
    }
    stops[index].m_offset = get_track_offset(fill, bounds, local);
    auto order = std::vector<int>();
    for(auto i = 0; i != count; ++i) {
      order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&] (auto left, auto right) {
      return stops[left].m_offset < stops[right].m_offset;
    });
    auto edit = GradientEdit();
    edit.m_is_handled = true;
    auto changed = fill;
    changed.m_stops.clear();
    for(auto i = 0; i != count; ++i) {
      changed.m_stops.push_back(stops[order[i]]);
      if(order[i] == index) {
        edit.m_stop = i;
      }
    }
    edit.m_fill = std::move(changed);
    return edit;
  }
}

bool GradientHandles::is_editable(const GradientFill& fill) {
  if(fill.m_units != GradientUnits::OBJECT_BOUNDING_BOX ||
      fill.m_stops.empty()) {
    return false;
  } else if(auto radial = std::get_if<RadialGradient>(&fill.m_geometry)) {
    return radial->m_focus == radial->m_center &&
      static_cast<bool>(invert(fill.m_transform));
  }
  return fill.m_transform.is_identity();
}

GradientEdit GradientHandles::press_key(const GradientFill& fill,
    const Rect& bounds, double zoom, int stop, int key,
    Qt::KeyboardModifiers modifiers) {
  auto edit = GradientEdit();
  if(!is_editable(fill)) {
    return edit;
  }
  auto count = static_cast<int>(fill.m_stops.size());
  auto selected = std::clamp(stop, 0, count - 1);
  if((key == Qt::Key_Left || key == Qt::Key_Right ||
      key == Qt::Key_Up || key == Qt::Key_Down) &&
      !(modifiers & (Qt::ControlModifier | Qt::AltModifier |
        Qt::MetaModifier))) {
    auto distance = modifiers.testFlag(Qt::ShiftModifier) ? LARGE_NUDGE :
      NUDGE;
    auto delta = Point(0, 0);
    if(key == Qt::Key_Left) {
      delta.m_x = -distance;
    } else if(key == Qt::Key_Right) {
      delta.m_x = distance;
    } else if(key == Qt::Key_Up) {
      delta.m_y = -distance;
    } else {
      delta.m_y = distance;
    }
    return nudge(fill, bounds, zoom, selected, delta);
  } else if(key != Qt::Key_Delete && key != Qt::Key_Backspace) {
    return edit;
  }
  edit.m_is_handled = true;
  auto stops = fill.m_stops;
  selected = normalize_stops(stops, selected);
  if(selected == 0 || selected + 1 == static_cast<int>(stops.size())) {
    return edit;
  }
  stops.erase(stops.begin() + selected);
  auto removed = fill;
  removed.m_stops = std::move(stops);
  edit.m_fill = std::move(removed);
  edit.m_stop = selected - 1;
  return edit;
}

GradientEdit GradientHandles::nudge(const GradientFill& fill,
    const Rect& bounds, double zoom, int stop, Point delta) {
  if(!is_editable(fill)) {
    return GradientEdit();
  }
  if(zoom > 0) {
    delta = delta / zoom;
  }
  auto count = static_cast<int>(fill.m_stops.size());
  return nudge_stop(fill, bounds, std::clamp(stop, 0, count - 1), delta);
}

GradientHandles::GradientHandles()
  : m_part(Part::NONE),
    m_stop(0),
    m_rotation(0),
    m_angle(0),
    m_offset(0, 0),
    m_handle_angle(DEFAULT_ROTATION_HANDLE_ANGLE) {}

void GradientHandles::reset() {
  release();
  m_handle_angle = DEFAULT_ROTATION_HANDLE_ANGLE;
}

void GradientHandles::paint(QPainter& painter, const GradientFill& fill,
    const GradientFrame& frame, int stop) const {
  if(!is_editable(fill)) {
    return;
  }
  auto& stops = fill.m_stops;
  if(std::holds_alternative<AngularGradient>(fill.m_geometry)) {
    auto to_view = get_unit_to_view(frame);
    auto& angular = std::get<AngularGradient>(fill.m_geometry);
    auto track = QPolygonF();
    for(auto i = 0; i != TRACK_SEGMENTS; ++i) {
      track.push_back(to_qpoint(get_angular_point(fill, to_view,
        static_cast<double>(i) / TRACK_SEGMENTS - angular.m_rotation / 360)));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(TRACK_SHADOW, 3));
    painter.drawPolygon(track);
    painter.setPen(QPen(Qt::white, 1));
    painter.drawPolygon(track);
    auto center = to_view.map(angular.m_center);
    auto radians = m_handle_angle * std::numbers::pi / 180;
    auto direction = Point(std::cos(radians), std::sin(radians));
    auto unit = to_unit(frame, center + direction);
    auto edge = get_angular_point(
      fill, to_view, get_angular_offset(angular, unit));
    auto handle = center + direction *
      (get_length(edge - center) + ROTATION_HANDLE_DISTANCE);
    painter.setPen(QPen(HANDLE_COLOR, 1));
    painter.drawLine(to_qpoint(center), to_qpoint(handle));
    paint_handle(painter, handle, HANDLE_RADIUS - 2);
    paint_handle(painter, center, HANDLE_RADIUS - 1);
    for(auto i = 0; i != static_cast<int>(stops.size()); ++i) {
      paint_stop(painter, get_angular_point(fill, to_view, stops[i].m_offset),
        stops[i].m_color, i == stop);
    }
    return;
  }
  auto track = get_track(fill, frame);
  if(std::holds_alternative<RadialGradient>(fill.m_geometry)) {
    auto ellipse = get_ellipse(fill, frame.m_bounds);
    auto& view = frame.m_to_view;
    auto outline = QPolygonF();
    for(auto i = 0; i != TRACK_SEGMENTS; ++i) {
      auto radians = 2 * std::numbers::pi * i / TRACK_SEGMENTS;
      outline.push_back(to_qpoint(view.map(get_point(ellipse,
        ellipse.m_radius_x * std::cos(radians),
        ellipse.m_radius_y * std::sin(radians)))));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(GUIDE_COLOR, 1));
    painter.drawPolygon(outline);
    auto corner = view.map(
      get_point(ellipse, ellipse.m_radius_x, ellipse.m_radius_y));
    auto bottom = view.map(get_point(ellipse, 0, ellipse.m_radius_y));
    auto dashed = QPen(GUIDE_COLOR, 1, Qt::DashLine);
    painter.setPen(dashed);
    painter.drawLine(to_qpoint(bottom), to_qpoint(corner));
    painter.drawLine(to_qpoint(track.m_end), to_qpoint(corner));
    paint_track_line(painter, track.m_start, track.m_end);
    paint_handle(painter, corner, HANDLE_RADIUS - 2);
  } else {
    paint_track_line(painter, track.m_start, track.m_end);
  }
  if(!is_start(fill, 0)) {
    paint_handle(painter, track.m_start, HANDLE_RADIUS - 1);
  }
  if(!is_end(fill, static_cast<int>(stops.size()) - 1)) {
    paint_handle(painter, track.m_end, HANDLE_RADIUS - 1);
  }
  for(auto i = 0; i != static_cast<int>(stops.size()); ++i) {
    paint_stop(painter, track.m_start +
      (track.m_end - track.m_start) * stops[i].m_offset, stops[i].m_color,
      i == stop);
  }
}

GradientEdit GradientHandles::press(
    const GradientFill& fill, const GradientFrame& frame, Point position) {
  m_part = Part::NONE;
  if(!is_editable(fill)) {
    return GradientEdit();
  }
  auto& stops = fill.m_stops;
  auto edit = GradientEdit();
  edit.m_is_handled = true;
  if(std::holds_alternative<AngularGradient>(fill.m_geometry)) {
    auto to_view = get_unit_to_view(frame);
    auto& angular = std::get<AngularGradient>(fill.m_geometry);
    auto unit = to_unit(frame, position);
    for(auto i = 0; i != static_cast<int>(stops.size()); ++i) {
      if(get_length(get_angular_point(fill, to_view, stops[i].m_offset) -
          position) <= HANDLE_TOLERANCE) {
        m_part = Part::STOP;
        m_stop = i;
        edit.m_stop = i;
        return edit;
      }
    }
    auto center = to_view.map(angular.m_center);
    auto radians = m_handle_angle * std::numbers::pi / 180;
    auto direction = Point(std::cos(radians), std::sin(radians));
    auto edge = get_angular_point(fill, to_view,
      get_angular_offset(angular, to_unit(frame, center + direction)));
    auto handle = center + direction *
      (get_length(edge - center) + ROTATION_HANDLE_DISTANCE);
    if(get_length(handle - position) <= HANDLE_TOLERANCE) {
      m_part = Part::ROTATION;
      m_rotation = angular.m_rotation;
      m_angle = std::atan2(unit.m_y - angular.m_center.m_y,
        unit.m_x - angular.m_center.m_x) * 180 / std::numbers::pi;
      return edit;
    }
    if(get_length(center - position) <= HANDLE_TOLERANCE) {
      m_part = Part::CENTER;
      m_offset = angular.m_center - unit;
      return edit;
    }
    auto offset = get_angular_offset(angular, unit);
    if(get_length(get_angular_point(fill, to_view, offset) - position) >
        HANDLE_TOLERANCE) {
      return GradientEdit();
    }
    auto added = fill;
    auto index = insert_stop(added.m_stops,
      GradientStop(offset, interpolate_color(stops, offset)));
    m_part = Part::STOP;
    m_stop = index;
    edit.m_fill = std::move(added);
    edit.m_stop = index;
    return edit;
  }
  auto track = get_track(fill, frame);
  for(auto i = static_cast<int>(stops.size()) - 1; i >= 0; --i) {
    auto point =
      track.m_start + (track.m_end - track.m_start) * stops[i].m_offset;
    if(get_length(point - position) > HANDLE_TOLERANCE) {
      continue;
    }
    if(is_start(fill, i)) {
      m_part = Part::START;
    } else if(is_end(fill, i)) {
      m_part = Part::END;
    } else {
      m_part = Part::STOP;
    }
    m_stop = i;
    edit.m_stop = i;
    return edit;
  }
  if(get_length(track.m_start - position) <= HANDLE_TOLERANCE) {
    m_part = Part::START;
    return edit;
  } else if(get_length(track.m_end - position) <= HANDLE_TOLERANCE) {
    m_part = Part::END;
    return edit;
  }
  if(std::holds_alternative<RadialGradient>(fill.m_geometry)) {
    auto ellipse = get_ellipse(fill, frame.m_bounds);
    auto corner = frame.m_to_view.map(
      get_point(ellipse, ellipse.m_radius_x, ellipse.m_radius_y));
    if(get_length(corner - position) <= HANDLE_TOLERANCE) {
      m_part = Part::RESIZE;
      return edit;
    }
  }
  if(get_segment_distance(position, track.m_start, track.m_end) >
      HANDLE_TOLERANCE) {
    return GradientEdit();
  }
  auto offset = 0.0;
  if(auto linear = std::get_if<LinearGradient>(&fill.m_geometry)) {
    auto unit = to_unit(frame, position);
    auto direction = linear->m_end - linear->m_start;
    auto length = dot(direction, direction);
    offset = length > 0 ? std::clamp(
      dot(unit - linear->m_start, direction) / length, 0.0, 1.0) : 0.0;
  } else {
    offset = get_radial_offset(
      fill, frame.m_bounds, to_local(frame, position));
  }
  auto added = fill;
  auto index = insert_stop(added.m_stops,
    GradientStop(offset, interpolate_color(stops, offset)));
  m_part = Part::STOP;
  m_stop = index;
  edit.m_fill = std::move(added);
  edit.m_stop = index;
  return edit;
}

GradientEdit GradientHandles::move(const GradientFill& fill,
    const GradientFrame& frame, Point position,
    Qt::KeyboardModifiers modifiers) {
  auto edit = GradientEdit();
  if(m_part == Part::NONE || !is_editable(fill)) {
    return edit;
  }
  edit.m_is_handled = true;
  auto changed = fill;
  auto unit = to_unit(frame, position);
  if(auto angular = std::get_if<AngularGradient>(&changed.m_geometry)) {
    if(m_part == Part::CENTER) {
      angular->m_center = unit + m_offset;
    } else if(m_part == Part::ROTATION) {
      auto angle = std::atan2(unit.m_y - angular->m_center.m_y,
        unit.m_x - angular->m_center.m_x) * 180 / std::numbers::pi;
      angular->m_rotation = m_rotation + angle - m_angle;
      auto center = get_unit_to_view(frame).map(angular->m_center);
      m_handle_angle = std::atan2(position.m_y - center.m_y,
        position.m_x - center.m_x) * 180 / std::numbers::pi;
    } else if(m_part == Part::STOP &&
        m_stop < static_cast<int>(changed.m_stops.size())) {
      m_stop = move_stop(
        changed.m_stops, m_stop, get_angular_offset(*angular, unit));
      edit.m_stop = m_stop;
    } else {
      return edit;
    }
    edit.m_fill = std::move(changed);
    return edit;
  }
  if(auto linear = std::get_if<LinearGradient>(&changed.m_geometry)) {
    if(m_part == Part::START) {
      linear->m_start = snap_unit(frame, unit);
    } else if(m_part == Part::END) {
      linear->m_end = snap_unit(frame, unit);
    } else if(m_part == Part::STOP &&
        m_stop < static_cast<int>(changed.m_stops.size())) {
      auto direction = linear->m_end - linear->m_start;
      auto length = dot(direction, direction);
      auto offset = length > 0 ? std::clamp(
        dot(unit - linear->m_start, direction) / length, 0.0, 1.0) : 0.0;
      m_stop = move_stop(changed.m_stops, m_stop, offset);
      edit.m_stop = m_stop;
    } else {
      return edit;
    }
    edit.m_fill = std::move(changed);
    return edit;
  }
  auto& bounds = frame.m_bounds;
  auto ellipse = get_ellipse(fill, bounds);
  auto local = to_local(frame, position);
  if(m_part == Part::START) {
    auto snapped = snap_unit(frame, unit);
    ellipse.m_center = get_box(bounds).map(snapped);
  } else if(m_part == Part::END) {
    turn_edge(ellipse, local, modifiers.testFlag(Qt::ShiftModifier));
  } else if(m_part == Part::RESIZE) {
    auto end = get_point(ellipse, ellipse.m_radius_x, 0);
    auto bottom = get_point(ellipse, 0, ellipse.m_radius_y);
    auto direction = end - ellipse.m_center;
    auto rotation = std::atan2(direction.m_y, direction.m_x);
    auto rotated =
      make_rotation(-rotation).map(local - ellipse.m_center);
    auto x = rotated.m_x;
    auto y = rotated.m_y;
    auto sign_x = x < 0 ? -1.0 : 1.0;
    auto sign_y = y < 0 ? -1.0 : 1.0;
    if(modifiers.testFlag(Qt::ShiftModifier)) {
      auto width = get_length(direction);
      auto height = get_length(bottom - ellipse.m_center);
      if(width > 0 && height > 0) {
        auto scale = std::min(std::abs(x) / width, std::abs(y) / height);
        x = sign_x * width * scale;
        y = sign_y * height * scale;
      }
    }
    ellipse.m_angle = rotation;
    if(sign_x < 0) {
      ellipse.m_angle += std::numbers::pi;
      ellipse.m_flip = -sign_y;
    } else {
      ellipse.m_flip = sign_y;
    }
    ellipse.m_radius_x = std::max(MINIMUM_RADIUS, std::abs(x));
    ellipse.m_radius_y = std::max(MINIMUM_RADIUS, std::abs(y));
  } else if(m_part == Part::STOP &&
      m_stop < static_cast<int>(changed.m_stops.size())) {
    m_stop = move_stop(changed.m_stops, m_stop,
      get_radial_offset(fill, bounds, local));
    edit.m_stop = m_stop;
    edit.m_fill = std::move(changed);
    return edit;
  } else {
    return edit;
  }
  edit.m_fill = make_radial(std::move(changed), ellipse, bounds);
  return edit;
}

void GradientHandles::release() {
  m_part = Part::NONE;
}

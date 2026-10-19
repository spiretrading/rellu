#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Tools/SmartGuides.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 1e-6;
  constexpr auto MAXIMUM_LABELS = std::size_t(256);
  constexpr auto LABEL_RADIUS = 2.0;
  constexpr auto OBJECTS_PER_BIN = std::size_t(4);
  constexpr auto MAXIMUM_BINS = std::size_t(4096);
  constexpr auto MINIMUM_WIDE_BINS = std::size_t(4);

  struct Interval {
    double m_start;
    double m_end;
  };

  struct Neighbour {
    Interval m_along;
    Interval m_across;
  };

  Interval get_along(const Rect& rect, bool is_x) {
    if(is_x) {
      return Interval(rect.get_left(), rect.get_right());
    }
    return Interval(rect.get_top(), rect.get_bottom());
  }

  Interval get_across(const Rect& rect, bool is_x) {
    return get_along(rect, !is_x);
  }

  double get_middle(Interval interval) {
    return (interval.m_start + interval.m_end) / 2;
  }

  bool overlaps(Interval left, Interval right) {
    return left.m_start < right.m_end && right.m_start < left.m_end;
  }

  double get_middle(Interval left, Interval right) {
    auto start = std::max(left.m_start, right.m_start);
    auto end = std::min(left.m_end, right.m_end);
    if(start <= end) {
      return (start + end) / 2;
    }
    return get_middle(left);
  }

  Point make_point(bool is_x, double along, double across) {
    if(is_x) {
      return Point(along, across);
    }
    return Point(across, along);
  }

  Rect translate(const Rect& rect, Point delta) {
    return Rect(rect.m_x + delta.m_x, rect.m_y + delta.m_y, rect.m_width,
      rect.m_height);
  }

  std::vector<double> get_features(const Rect& rect, bool is_x) {
    auto along = get_along(rect, is_x);
    return {along.m_start, get_middle(along), along.m_end};
  }

  bool has_x(SnapAxes axes) {
    return axes == SnapAxes::HORIZONTAL || axes == SnapAxes::BOTH;
  }

  bool has_y(SnapAxes axes) {
    return axes == SnapAxes::VERTICAL || axes == SnapAxes::BOTH;
  }

  void keep_closest(std::optional<double>& best, double offset) {
    if(!best || std::abs(offset) < std::abs(*best)) {
      best = offset;
    }
  }

  std::optional<Interval> find_before(
      const std::vector<Neighbour>& row, double limit, double center) {
    auto found = std::optional<Interval>();
    for(auto& neighbour : row) {
      auto& along = neighbour.m_along;
      if(along.m_end <= limit && get_middle(along) < center &&
          (!found || along.m_end > found->m_end)) {
        found = along;
      }
    }
    return found;
  }

  std::optional<Interval> find_after(
      const std::vector<Neighbour>& row, double limit, double center) {
    auto found = std::optional<Interval>();
    for(auto& neighbour : row) {
      auto& along = neighbour.m_along;
      if(along.m_start >= limit && get_middle(along) > center &&
          (!found || along.m_start < found->m_start)) {
        found = along;
      }
    }
    return found;
  }

  Interval get_across(const std::vector<Neighbour>& row, Interval along) {
    for(auto& neighbour : row) {
      if(neighbour.m_along.m_start == along.m_start &&
          neighbour.m_along.m_end == along.m_end) {
        return neighbour.m_across;
      }
    }
    return Interval(0, 0);
  }

  QColor to_qcolor(Color color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }
}

double GuideDistance::get_length() const {
  return distance(m_start, m_end);
}

SmartGuides::SmartGuides(const ToolContext& context)
  : m_context(&context),
    m_is_active(false),
    m_is_collected(false),
    m_mark(0),
    m_label_scale(0) {}

const std::vector<GuideLine>& SmartGuides::get_lines() const {
  return m_lines;
}

const std::vector<GuideDistance>& SmartGuides::get_distances() const {
  return m_distances;
}

const std::vector<GuideLine>& SmartGuides::get_measured_lines() const {
  return m_measured_lines;
}

const std::vector<GuideDistance>&
    SmartGuides::get_measured_distances() const {
  return m_measured_distances;
}

void SmartGuides::begin(
    const NodeId& container, const std::vector<NodeId>& nodes) {
  m_is_active = true;
  m_is_collected = false;
  m_container = container;
  m_nodes = nodes;
  clear_guides();
}

Point SmartGuides::snap_move(
    const Rect& bounds, Point delta, Qt::KeyboardModifiers modifiers) {
  clear_guides();
  if(!m_is_active || modifiers.testFlag(Qt::ControlModifier)) {
    return delta;
  }
  if(!m_is_collected) {
    collect();
  }
  auto threshold = get_threshold();
  auto rect = translate(bounds, delta);
  auto offset = Point(0, 0);
  for(auto is_x : {true, false}) {
    auto best = find_alignment(is_x, get_features(rect, is_x), threshold);
    if(auto spacing = find_spacing(is_x, rect, threshold)) {
      keep_closest(best, *spacing);
    }
    if(!best) {
      continue;
    } else if(is_x) {
      offset.m_x = *best;
    } else {
      offset.m_y = *best;
    }
  }
  auto snapped = translate(rect, offset);
  for(auto is_x : {true, false}) {
    add_lines(is_x, snapped, get_features(snapped, is_x));
    add_spacing(is_x, snapped);
  }
  return delta + offset;
}

Point SmartGuides::snap_point(
    Point point, SnapAxes axes, Qt::KeyboardModifiers modifiers) {
  clear_guides();
  if(!m_is_active || modifiers.testFlag(Qt::ControlModifier)) {
    return point;
  }
  if(!m_is_collected) {
    collect();
  }
  auto threshold = get_threshold();
  auto snapped = point;
  if(has_x(axes)) {
    if(auto offset = find_alignment(true, {point.m_x}, threshold)) {
      snapped.m_x += *offset;
    }
  }
  if(has_y(axes)) {
    if(auto offset = find_alignment(false, {point.m_y}, threshold)) {
      snapped.m_y += *offset;
    }
  }
  auto rect = Rect(snapped.m_x, snapped.m_y, 0, 0);
  if(has_x(axes)) {
    add_lines(true, rect, {snapped.m_x});
  }
  if(has_y(axes)) {
    add_lines(false, rect, {snapped.m_y});
  }
  return snapped;
}

void SmartGuides::end() {
  m_is_active = false;
  m_is_collected = false;
  m_nodes.clear();
  m_targets.clear();
  m_objects.clear();
  m_x_entries.clear();
  m_y_entries.clear();
  m_square_grid = std::nullopt;
  m_rows = RowIndex();
  m_columns = RowIndex();
  m_marks.clear();
  clear_guides();
}

void SmartGuides::measure(
    const std::optional<Rect>& selection, const Rect& target) {
  m_measured_lines.clear();
  m_measured_distances.clear();
  if(!selection) {
    return;
  }
  for(auto is_x : {true, false}) {
    auto from = get_along(*selection, is_x);
    auto to = get_along(target, is_x);
    auto from_across = get_across(*selection, is_x);
    auto to_across = get_across(target, is_x);
    auto is_overlapping = from_across.m_start <= to_across.m_end &&
      to_across.m_start <= from_across.m_end;
    auto across = get_middle(from_across, to_across);
    auto add = [&] (double start, double end, double edge) {
      if(!(end - start > TOLERANCE)) {
        return;
      }
      m_measured_distances.push_back(GuideDistance(
        make_point(is_x, start, across), make_point(is_x, end, across)));
      if(!is_overlapping) {
        auto bound = to_across.m_start;
        if(across > to_across.m_end) {
          bound = to_across.m_end;
        }
        m_measured_lines.push_back(GuideLine(
          make_point(is_x, edge, bound), make_point(is_x, edge, across)));
      }
    };
    if(from.m_end <= to.m_start) {
      add(from.m_end, to.m_start, to.m_start);
    } else if(to.m_end <= from.m_start) {
      add(to.m_end, from.m_start, to.m_end);
    } else if(is_overlapping) {
      add(std::min(from.m_start, to.m_start),
        std::max(from.m_start, to.m_start), to.m_start);
      add(std::min(from.m_end, to.m_end), std::max(from.m_end, to.m_end),
        to.m_end);
    }
  }
}

void SmartGuides::draw(const Matrix& view, CanvasOverlay& overlay) const {
  auto scale = m_context->get_scale();
  for(auto& line : m_lines) {
    draw_line(view, line, overlay);
  }
  for(auto& line : m_measured_lines) {
    draw_line(view, line, overlay);
  }
  for(auto& distance : m_distances) {
    draw_distance(view, distance, scale, overlay);
  }
  for(auto& distance : m_measured_distances) {
    draw_distance(view, distance, scale, overlay);
  }
}

void SmartGuides::collect() {
  m_is_collected = true;
  m_targets.clear();
  m_objects.clear();
  m_x_entries.clear();
  m_y_entries.clear();
  m_square_grid = std::nullopt;
  auto& document = m_context->get_document();
  auto& measurer = m_context->get_text_measurer();
  auto excluded = std::unordered_set<NodeId>();
  for(auto& id : m_nodes) {
    auto current = id;
    while(auto node = document.find(current)) {
      if(!excluded.insert(current).second || node->m_parent.is_nil()) {
        break;
      }
      current = node->m_parent;
    }
  }
  auto container = document.find(m_container);
  auto& children =
    container ? container->m_children : document.get_canvas();
  for(auto& child : children) {
    if(excluded.contains(child)) {
      continue;
    }
    auto node = document.find(child);
    if(!node || !node->m_header.m_is_visible) {
      continue;
    }
    if(auto bounds = get_world_bounds(
        document, child, BoundsType::GEOMETRY, measurer)) {
      add_target(*bounds, true, true);
    }
  }
  if(container) {
    if(auto artboard = find_artboard(document, m_container);
        !artboard.is_nil()) {
      add_artboard(artboard);
    }
  }
  auto order = [] (const auto& left, const auto& right) {
    return left.m_value < right.m_value;
  };
  std::sort(m_x_entries.begin(), m_x_entries.end(), order);
  std::sort(m_y_entries.begin(), m_y_entries.end(), order);
  index(true);
  index(false);
  m_marks.assign(m_targets.size(), 0);
}

void SmartGuides::index(bool is_x) {
  auto& rows = is_x ? m_rows : m_columns;
  rows.m_bins.clear();
  rows.m_wide.clear();
  if(m_objects.empty()) {
    return;
  }
  auto start = get_across(m_targets[m_objects.front()].m_bounds, is_x).m_start;
  auto end = start;
  for(auto object : m_objects) {
    auto across = get_across(m_targets[object].m_bounds, is_x);
    start = std::min(start, across.m_start);
    end = std::max(end, across.m_end);
  }
  auto count = std::clamp(
    m_objects.size() / OBJECTS_PER_BIN, std::size_t(1), MAXIMUM_BINS);
  rows.m_origin = start;
  rows.m_bin_width = (end - start) / static_cast<double>(count);
  if(!(rows.m_bin_width > 0)) {
    rows.m_bin_width = 1;
    count = 1;
  }
  rows.m_bins.resize(count);
  auto wide = std::max(MINIMUM_WIDE_BINS, count / 8);
  auto get_bin = [&] (double value) {
    auto bin = std::floor((value - rows.m_origin) / rows.m_bin_width);
    return static_cast<std::size_t>(
      std::clamp(bin, 0.0, static_cast<double>(count - 1)));
  };
  for(auto object : m_objects) {
    auto across = get_across(m_targets[object].m_bounds, is_x);
    auto first = get_bin(across.m_start);
    auto last = get_bin(across.m_end);
    if(last - first + 1 > wide) {
      rows.m_wide.push_back(object);
      continue;
    }
    for(auto bin = first; bin <= last; ++bin) {
      rows.m_bins[bin].push_back(object);
    }
  }
}

std::vector<std::size_t> SmartGuides::find_row(
    bool is_x, double start, double end) const {
  auto& rows = is_x ? m_rows : m_columns;
  auto row = std::vector<std::size_t>();
  if(rows.m_bins.empty()) {
    return row;
  }
  ++m_mark;
  auto range = Interval(start, end);
  auto add = [&] (std::size_t object) {
    if(m_marks[object] == m_mark) {
      return;
    }
    m_marks[object] = m_mark;
    if(overlaps(get_across(m_targets[object].m_bounds, is_x), range)) {
      row.push_back(object);
    }
  };
  auto count = static_cast<double>(rows.m_bins.size());
  auto first = std::floor((start - rows.m_origin) / rows.m_bin_width);
  auto last = std::floor((end - rows.m_origin) / rows.m_bin_width);
  if(last >= 0 && first < count) {
    auto bin = static_cast<std::size_t>(std::max(first, 0.0));
    auto bound = static_cast<std::size_t>(std::min(last, count - 1));
    for(; bin <= bound; ++bin) {
      for(auto object : rows.m_bins[bin]) {
        add(object);
      }
    }
  }
  for(auto object : rows.m_wide) {
    add(object);
  }
  return row;
}

void SmartGuides::add_target(
    const Rect& bounds, bool is_shown, bool is_object) {
  auto index = m_targets.size();
  m_targets.push_back(Target(bounds, is_shown));
  if(is_object) {
    m_objects.push_back(index);
  }
  for(auto value : get_features(bounds, true)) {
    m_x_entries.push_back(Entry(value, index));
  }
  for(auto value : get_features(bounds, false)) {
    m_y_entries.push_back(Entry(value, index));
  }
}

void SmartGuides::add_line(bool is_x, double value) {
  auto index = m_targets.size();
  m_targets.push_back(Target(Rect(0, 0, 0, 0), false));
  if(is_x) {
    m_x_entries.push_back(Entry(value, index));
  } else {
    m_y_entries.push_back(Entry(value, index));
  }
}

void SmartGuides::add_artboard(const NodeId& id) {
  auto artboard = m_context->get_document().get(id).get_artboard();
  auto& frame = artboard->m_frame;
  add_target(frame, true, false);
  for(auto& guide : artboard->m_guides) {
    if(guide.m_orientation == Orientation::HORIZONTAL) {
      add_line(false, frame.m_y + guide.m_position);
    } else {
      add_line(true, frame.m_x + guide.m_position);
    }
  }
  auto& grid = artboard->m_layout_grid;
  if(!grid || !grid->m_is_visible) {
    return;
  }
  if(grid->m_type == GridType::SQUARE) {
    if(grid->m_square_size.m_width > 0 && grid->m_square_size.m_height > 0) {
      m_square_grid = SquareGrid(frame, grid->m_square_size);
    }
    return;
  }
  if(grid->m_column_count <= 0) {
    return;
  }
  auto& margins = grid->m_margins;
  auto count = grid->m_column_count;
  auto width = (frame.m_width - margins.m_left - margins.m_right -
    grid->m_gutter * (count - 1)) / count;
  if(!(width > 0)) {
    return;
  }
  for(auto i = 0; i != count; ++i) {
    auto left = frame.m_x + margins.m_left + i * (width + grid->m_gutter);
    add_line(true, left);
    add_line(true, left + width);
  }
}

void SmartGuides::clear_guides() {
  m_lines.clear();
  m_distances.clear();
}

double SmartGuides::get_threshold() const {
  auto zoom = m_context->get_zoom();
  if(!(zoom > 0)) {
    return 0;
  }
  return THRESHOLD / zoom;
}

std::optional<double> SmartGuides::find_alignment(bool is_x,
    const std::vector<double>& values, double threshold) const {
  auto& entries = is_x ? m_x_entries : m_y_entries;
  auto best = std::optional<double>();
  for(auto value : values) {
    auto i = std::lower_bound(entries.begin(), entries.end(),
      value - threshold, [] (const auto& entry, auto bound) {
        return entry.m_value < bound;
      });
    for(; i != entries.end() && i->m_value <= value + threshold; ++i) {
      keep_closest(best, i->m_value - value);
    }
    if(m_square_grid) {
      auto& frame = m_square_grid->m_frame;
      auto extent = get_along(frame, is_x);
      auto size = is_x ? m_square_grid->m_size.m_width :
        m_square_grid->m_size.m_height;
      auto line = extent.m_start +
        std::round((value - extent.m_start) / size) * size;
      if(line >= extent.m_start && line <= extent.m_end &&
          std::abs(line - value) <= threshold) {
        keep_closest(best, line - value);
      }
    }
  }
  return best;
}

std::optional<double> SmartGuides::find_spacing(
    bool is_x, const Rect& rect, double threshold) const {
  auto along = get_along(rect, is_x);
  auto across = get_across(rect, is_x);
  auto length = along.m_end - along.m_start;
  auto center = get_middle(along);
  auto row = std::vector<Neighbour>();
  for(auto index : find_row(is_x, across.m_start, across.m_end)) {
    auto& bounds = m_targets[index].m_bounds;
    row.push_back(
      Neighbour(get_along(bounds, is_x), get_across(bounds, is_x)));
  }
  if(row.empty()) {
    return std::nullopt;
  }
  auto best = std::optional<double>();
  auto consider = [&] (double start) {
    auto offset = start - along.m_start;
    if(std::abs(offset) <= threshold) {
      keep_closest(best, offset);
    }
  };
  auto before = find_before(row, along.m_start + threshold, center);
  auto after = find_after(row, along.m_end - threshold, center);
  if(before && after) {
    auto room = after->m_start - before->m_end - length;
    if(room >= 0) {
      consider(before->m_end + room / 2);
    }
  }
  if(before) {
    if(auto outer = find_before(row, before->m_start + TOLERANCE,
        get_middle(*before))) {
      auto gap = before->m_start - outer->m_end;
      if(gap >= 0) {
        consider(before->m_end + gap);
      }
    }
  }
  if(after) {
    if(auto outer = find_after(row, after->m_end - TOLERANCE,
        get_middle(*after))) {
      auto gap = outer->m_start - after->m_end;
      if(gap >= 0) {
        consider(after->m_start - gap - length);
      }
    }
  }
  return best;
}

void SmartGuides::add_lines(
    bool is_x, const Rect& rect, const std::vector<double>& values) {
  auto& entries = is_x ? m_x_entries : m_y_entries;
  auto across = get_across(rect, is_x);
  auto drawn = std::vector<double>();
  for(auto value : values) {
    if(std::any_of(drawn.begin(), drawn.end(), [&] (auto other) {
        return std::abs(other - value) <= TOLERANCE;
      })) {
      continue;
    }
    drawn.push_back(value);
    auto start = across.m_start;
    auto end = across.m_end;
    auto is_shown = false;
    auto i = std::lower_bound(entries.begin(), entries.end(),
      value - TOLERANCE, [] (const auto& entry, auto bound) {
        return entry.m_value < bound;
      });
    for(; i != entries.end() && i->m_value <= value + TOLERANCE; ++i) {
      auto& target = m_targets[i->m_target];
      if(!target.m_is_shown) {
        continue;
      }
      auto extent = get_across(target.m_bounds, is_x);
      start = std::min(start, extent.m_start);
      end = std::max(end, extent.m_end);
      is_shown = true;
    }
    if(is_shown) {
      m_lines.push_back(GuideLine(
        make_point(is_x, value, start), make_point(is_x, value, end)));
    }
  }
}

void SmartGuides::add_spacing(bool is_x, const Rect& rect) {
  auto along = get_along(rect, is_x);
  auto across = get_across(rect, is_x);
  auto center = get_middle(along);
  auto row = std::vector<Neighbour>();
  for(auto index : find_row(is_x, across.m_start, across.m_end)) {
    auto& bounds = m_targets[index].m_bounds;
    row.push_back(
      Neighbour(get_along(bounds, is_x), get_across(bounds, is_x)));
  }
  auto add = [&] (Interval first, Interval first_across, Interval second,
      Interval second_across) {
    auto gap = second.m_start - first.m_end;
    if(!(gap > TOLERANCE)) {
      return;
    }
    auto middle = get_middle(first_across, second_across);
    auto distance = GuideDistance(make_point(is_x, first.m_end, middle),
      make_point(is_x, second.m_start, middle));
    if(std::find(m_distances.begin(), m_distances.end(), distance) ==
        m_distances.end()) {
      m_distances.push_back(distance);
    }
  };
  auto is_equal = [] (double left, double right) {
    return std::abs(left - right) <= TOLERANCE;
  };
  auto before = find_before(row, along.m_start + TOLERANCE, center);
  auto after = find_after(row, along.m_end - TOLERANCE, center);
  if(before && after && is_equal(along.m_start - before->m_end,
      after->m_start - along.m_end)) {
    add(*before, get_across(row, *before), along, across);
    add(along, across, *after, get_across(row, *after));
  }
  if(before) {
    if(auto outer = find_before(row, before->m_start + TOLERANCE,
        get_middle(*before)); outer && is_equal(
          along.m_start - before->m_end, before->m_start - outer->m_end)) {
      add(*outer, get_across(row, *outer), *before, get_across(row, *before));
      add(*before, get_across(row, *before), along, across);
    }
  }
  if(after) {
    if(auto outer = find_after(row, after->m_end - TOLERANCE,
        get_middle(*after)); outer && is_equal(
          after->m_start - along.m_end, outer->m_start - after->m_end)) {
      add(along, across, *after, get_across(row, *after));
      add(*after, get_across(row, *after), *outer, get_across(row, *outer));
    }
  }
}

std::shared_ptr<const QImage> SmartGuides::get_label(
    const std::string& text, double scale) const {
  if(scale != m_label_scale) {
    m_labels.clear();
    m_label_scale = scale;
  }
  if(auto label = m_labels.find(text); label != m_labels.end()) {
    return label->second;
  }
  if(m_labels.size() >= MAXIMUM_LABELS) {
    m_labels.clear();
  }
  auto font = QFont();
  font.setPixelSize(
    std::max(1, static_cast<int>(std::lround(FONT_SIZE * scale))));
  auto metrics = QFontMetricsF(font);
  auto content = QString::fromStdString(text);
  auto width = static_cast<int>(std::ceil(
    metrics.horizontalAdvance(content) + 2 * LABEL_PADDING * scale));
  auto height = static_cast<int>(std::ceil(LABEL_HEIGHT * scale));
  auto image = std::make_shared<QImage>(std::max(width, 1),
    std::max(height, 1), QImage::Format_RGBA8888_Premultiplied);
  image->fill(Qt::transparent);
  auto painter = QPainter(image.get());
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(to_qcolor(COLOR));
  painter.drawRoundedRect(QRectF(0, 0, image->width(), image->height()),
    LABEL_RADIUS * scale, LABEL_RADIUS * scale);
  painter.setFont(font);
  painter.setPen(QColor(255, 255, 255));
  painter.drawText(QRectF(0, 0, image->width(), image->height()),
    Qt::AlignCenter | Qt::TextSingleLine, content);
  painter.end();
  auto label = std::shared_ptr<const QImage>(std::move(image));
  m_labels.emplace(text, label);
  return label;
}

void SmartGuides::draw_line(const Matrix& view, const GuideLine& line,
    CanvasOverlay& overlay) const {
  auto start = view.map(line.m_start);
  auto end = view.map(line.m_end);
  auto length = distance(start, end);
  if(length == 0) {
    return;
  }
  auto extension = (end - start) * (LINE_WIDTH / 2 / length);
  overlay.m_shapes.push_back(make_overlay_line(
    start - extension, end + extension, LINE_WIDTH, COLOR));
}

void SmartGuides::draw_distance(const Matrix& view,
    const GuideDistance& distance, double scale,
    CanvasOverlay& overlay) const {
  auto start = view.map(distance.m_start);
  auto end = view.map(distance.m_end);
  auto length = Rellu::distance(start, end);
  if(length == 0) {
    return;
  }
  auto direction = (end - start) / length;
  auto normal = Point(-direction.m_y, direction.m_x);
  auto cap = normal * (CAP_LENGTH / 2);
  overlay.m_shapes.push_back(
    make_overlay_line(start - cap, start + cap, LINE_WIDTH, COLOR));
  overlay.m_shapes.push_back(
    make_overlay_line(end - cap, end + cap, LINE_WIDTH, COLOR));
  auto middle = interpolate(start, end, 0.5);
  if(!(scale > 0)) {
    overlay.m_shapes.push_back(
      make_overlay_line(start, end, LINE_WIDTH, COLOR));
    return;
  }
  auto label = get_label(format_distance(distance.get_length()), scale);
  auto size = Point(label->width(), label->height()) / scale;
  auto get_extent = [&] (Point axis) {
    return (std::abs(axis.m_x) * size.m_x + std::abs(axis.m_y) * size.m_y) /
      2;
  };
  auto half = get_extent(direction);
  auto center = middle;
  if(length > 2 * half) {
    overlay.m_shapes.push_back(make_overlay_line(
      start, middle - direction * half, LINE_WIDTH, COLOR));
    overlay.m_shapes.push_back(make_overlay_line(
      middle + direction * half, end, LINE_WIDTH, COLOR));
  } else {
    overlay.m_shapes.push_back(
      make_overlay_line(start, end, LINE_WIDTH, COLOR));
    center = middle + normal * (get_extent(normal) + CAP_LENGTH / 2);
  }
  overlay.m_images.push_back(OverlayImage(label, center - size / 2));
}

std::string Rellu::format_distance(double distance) {
  auto text = std::format("{:.2f}", std::abs(distance));
  while(text.back() == '0') {
    text.pop_back();
  }
  if(text.back() == '.') {
    text.pop_back();
  }
  return text;
}

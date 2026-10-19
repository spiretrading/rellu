#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <vector>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QUuid>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/GuideCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Tools/ArtboardGuides.hpp"
#include "Rellu/Tools/SmartGuides.hpp"

using namespace Rellu;

namespace {
  constexpr auto DISTANCE_COLOR = make_color(255, 74, 255);
  constexpr auto CREATION_COLOR = Color(1, 1, 1, 0.3);
  constexpr auto LABEL_FONT_SIZE = 11.0;
  constexpr auto AXIS_FONT_SIZE = 9.0;
  constexpr auto LABEL_OFFSET = -6.0;
  constexpr auto TOP_MARGIN = 20.0;
  constexpr auto BOTTOM_MARGIN = 12.0;
  constexpr auto SIDE_MARGIN = 20.0;
  constexpr auto LABEL_GAP = 20.0;
  constexpr auto HORIZONTAL_EDGE = 30.0;
  constexpr auto VERTICAL_EDGE = 20.0;
  constexpr auto POSITION_WIDTH = 26.0;
  constexpr auto POSITION_DIGIT_WIDTH = 6.0;
  constexpr auto POSITION_HEIGHT = 19.0;
  constexpr auto POSITION_RADIUS = 4.0;
  constexpr auto POSITION_OPACITY = 0.8;
  constexpr auto POSITION_TEXT_LEFT = 8.0;
  constexpr auto POSITION_TEXT_TOP = 13.0;
  const auto VERTICAL_POSITION_OFFSET = Point(10, -30);
  const auto HORIZONTAL_POSITION_OFFSET = Point(15, -30);

  enum class Alignment : unsigned char {
    LEFT,
    CENTER,
    RIGHT
  };

  const Artboard* find_frame(const Document& document, const NodeId& id) {
    if(auto node = document.find(id)) {
      return node->get_artboard();
    }
    return nullptr;
  }

  double get_extent(const Artboard& artboard, Orientation orientation) {
    if(orientation == Orientation::VERTICAL) {
      return artboard.m_frame.m_width;
    }
    return artboard.m_frame.m_height;
  }

  bool is_inside(const Artboard& artboard, Orientation orientation,
      double position) {
    return position > 0 && position < get_extent(artboard, orientation);
  }

  QColor to_qcolor(Color color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  QFont make_font(double size) {
    auto font = QFont();
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(size))));
    return font;
  }

  std::string to_text(double value) {
    return std::to_string(static_cast<long long>(std::llround(value)));
  }

  void add_text(CanvasOverlay& overlay, const std::string& text, Point anchor,
      Alignment alignment, double scale) {
    if(!(scale > 0)) {
      return;
    }
    auto font = make_font(LABEL_FONT_SIZE * scale);
    auto metrics = QFontMetricsF(font);
    auto content = QString::fromStdString(text);
    auto width = metrics.horizontalAdvance(content);
    auto image = std::make_shared<QImage>(
      std::max(1, static_cast<int>(std::ceil(width))),
      std::max(1, static_cast<int>(std::ceil(metrics.height()))),
      QImage::Format_RGBA8888_Premultiplied);
    image->fill(Qt::transparent);
    auto painter = QPainter(image.get());
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    painter.setPen(to_qcolor(DISTANCE_COLOR));
    painter.drawText(QPointF(0, metrics.ascent()), content);
    painter.end();
    auto left = anchor.m_x;
    if(alignment == Alignment::CENTER) {
      left -= width / scale / 2;
    } else if(alignment == Alignment::RIGHT) {
      left -= width / scale;
    }
    overlay.m_images.push_back(OverlayImage(std::move(image),
      Point(left, anchor.m_y - metrics.ascent() / scale)));
  }

  void add_position(CanvasOverlay& overlay, const std::string& axis,
      double position, Point origin, double scale) {
    if(!(scale > 0)) {
      return;
    }
    auto number = to_text(position);
    auto width = POSITION_WIDTH +
      POSITION_DIGIT_WIDTH * static_cast<double>(number.size());
    auto image = std::make_shared<QImage>(
      std::max(1, static_cast<int>(std::ceil(width * scale))),
      std::max(1, static_cast<int>(std::ceil(POSITION_HEIGHT * scale))),
      QImage::Format_RGBA8888_Premultiplied);
    image->fill(Qt::transparent);
    auto painter = QPainter(image.get());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(scale, scale);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0,
      static_cast<int>(std::lround(255 * POSITION_OPACITY))));
    painter.drawRoundedRect(QRectF(0, 0, width, POSITION_HEIGHT),
      POSITION_RADIUS, POSITION_RADIUS);
    auto axis_font = make_font(AXIS_FONT_SIZE);
    painter.setFont(axis_font);
    painter.setPen(QColor(0xB3, 0xB3, 0xB3));
    auto label = QString::fromStdString(axis + " ");
    painter.drawText(QPointF(POSITION_TEXT_LEFT, POSITION_TEXT_TOP), label);
    auto advance = QFontMetricsF(axis_font).horizontalAdvance(label);
    painter.setFont(make_font(LABEL_FONT_SIZE));
    painter.setPen(Qt::white);
    painter.drawText(QPointF(POSITION_TEXT_LEFT + advance, POSITION_TEXT_TOP),
      QString::fromStdString(number));
    painter.end();
    overlay.m_images.push_back(OverlayImage(std::move(image),
      Point(std::round(origin.m_x), std::round(origin.m_y))));
  }

  std::string make_guide_id() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  }
}

void Rellu::draw_artboard_guides(
    const ToolContext& context, CanvasOverlay& overlay) {
  if(!context.get_guide_settings().m_is_visible) {
    return;
  }
  auto& document = context.get_document();
  auto& view = context.get_view();
  auto& dragged = context.get_dragged_guide();
  for(auto& id : document.get_canvas()) {
    auto& node = document.get(id);
    auto artboard = node.get_artboard();
    if(!artboard || !node.m_header.m_is_visible ||
        artboard->m_guides.empty()) {
      continue;
    }
    auto& frame = artboard->m_frame;
    for(auto i = std::size_t(0); i != artboard->m_guides.size(); ++i) {
      auto& guide = artboard->m_guides[i];
      if((dragged && dragged->m_artboard == id && dragged->m_index == i) ||
          !is_inside(*artboard, guide.m_orientation, guide.m_position)) {
        continue;
      }
      auto start = Point(frame.m_x + guide.m_position, frame.m_y);
      auto end = Point(start.m_x, frame.get_bottom());
      if(guide.m_orientation == Orientation::HORIZONTAL) {
        start = Point(frame.m_x, frame.m_y + guide.m_position);
        end = Point(frame.get_right(), start.m_y);
      }
      overlay.m_shapes.push_back(make_overlay_line(
        view.map(start), view.map(end), 1, GUIDE_COLOR));
    }
  }
}

ArtboardGuides::ArtboardGuides()
  : m_position(0),
    m_is_shown_once(false),
    m_pointer(0, 0) {}

bool ArtboardGuides::press(ToolContext& context, const PointerEvent& event) {
  auto grip = find(context, event.m_position);
  if(!grip) {
    return false;
  }
  auto& artboard = *find_frame(context.get_document(), grip->m_artboard);
  m_grip = grip;
  m_hover = std::nullopt;
  m_pointer = event.m_position;
  m_is_shown_once = false;
  if(grip->m_index) {
    m_position = artboard.m_guides[*grip->m_index].m_position;
    context.set_dragged_guide(GuideReference(grip->m_artboard, *grip->m_index));
    m_is_shown_once = true;
  } else {
    m_position = 0;
  }
  context.get_editor().get_history().seal();
  return true;
}

void ArtboardGuides::move(ToolContext& context, const PointerEvent& event) {
  if(!m_grip) {
    return;
  }
  auto& document = context.get_document();
  auto artboard = find_frame(document, m_grip->m_artboard);
  if(!artboard) {
    return;
  }
  m_pointer = event.m_position;
  auto& frame = artboard->m_frame;
  auto is_vertical = m_grip->m_orientation == Orientation::VERTICAL;
  auto point = context.to_document(event.m_position);
  auto position = is_vertical ? point.m_x - frame.m_x : point.m_y - frame.m_y;
  auto extent = get_extent(*artboard, m_grip->m_orientation);
  auto exceeds = [&] (double value) {
    return value >= extent;
  };
  if(event.m_modifiers.testFlag(Qt::ShiftModifier)) {
    auto rounded = std::round(position / SNAP_STEP) * SNAP_STEP;
    if(position > 0 && rounded < SNAP_STEP) {
      position = SNAP_STEP;
    } else if(!exceeds(position) && exceeds(rounded)) {
      position = rounded - SNAP_STEP;
    } else {
      position = rounded;
    }
  } else if(!event.m_modifiers.testFlag(Qt::ControlModifier)) {
    auto zoom = context.get_zoom();
    auto threshold = zoom > 0 ? SmartGuides::THRESHOLD / zoom : 0;
    auto best = std::optional<double>();
    auto& measurer = context.get_text_measurer();
    auto candidates = document.get(m_grip->m_artboard).m_children;
    auto& container = context.get_editor().get_context().get_container();
    if(!container.is_nil() && document.find(container) &&
        find_artboard(document, container) == m_grip->m_artboard) {
      auto& children = document.get(container).m_children;
      candidates.insert(candidates.end(), children.begin(), children.end());
    }
    for(auto& child : candidates) {
      auto bounds =
        get_world_bounds(document, child, BoundsType::GEOMETRY, measurer);
      if(!bounds || !document.get(child).m_header.m_is_visible) {
        continue;
      }
      auto start = is_vertical ? bounds->m_x - frame.m_x :
        bounds->m_y - frame.m_y;
      auto length = is_vertical ? bounds->m_width : bounds->m_height;
      for(auto value : {start, start + length / 2, start + length}) {
        auto delta = value - position;
        if(std::abs(delta) < threshold &&
            (!best || std::abs(delta) < std::abs(*best))) {
          best = delta;
        }
      }
    }
    if(best) {
      auto snapped = position + *best;
      if(snapped > 0 && !exceeds(snapped)) {
        position = snapped;
      }
    }
  }
  m_position = std::round(position);
  if(is_inside(*artboard, m_grip->m_orientation, m_position)) {
    m_is_shown_once = true;
  }
}

void ArtboardGuides::release(ToolContext& context, const PointerEvent& event) {
  if(!m_grip) {
    return;
  }
  move(context, event);
  auto grip = *m_grip;
  auto& document = context.get_document();
  auto artboard = find_frame(document, grip.m_artboard);
  auto is_kept = is_visible(context) && !is_taken(context);
  auto position = m_position;
  cancel(context);
  if(!artboard) {
    return;
  }
  auto guides = artboard->m_guides;
  auto& editor = context.get_editor();
  if(!grip.m_index) {
    if(is_kept) {
      insert_guide(guides,
        Guide(grip.m_orientation, position, make_guide_id()));
      editor.execute(std::make_unique<SetGuidesCommand>(
        "Add Guide", grip.m_artboard, std::move(guides)));
    }
    return;
  }
  if(*grip.m_index >= guides.size()) {
    return;
  }
  auto guide = guides[*grip.m_index];
  if(is_kept && guide.m_position == position) {
    return;
  }
  guides.erase(guides.begin() + static_cast<std::ptrdiff_t>(*grip.m_index));
  if(is_kept) {
    guide.m_position = position;
    insert_guide(guides, std::move(guide));
    editor.execute(std::make_unique<SetGuidesCommand>(
      "Move Guide", grip.m_artboard, std::move(guides)));
  } else {
    editor.execute(std::make_unique<SetGuidesCommand>(
      "Delete Guide", grip.m_artboard, std::move(guides)));
  }
}

void ArtboardGuides::cancel(ToolContext& context) {
  m_grip = std::nullopt;
  m_hover = std::nullopt;
  m_is_shown_once = false;
  context.set_dragged_guide(std::nullopt);
}

bool ArtboardGuides::is_busy() const {
  return static_cast<bool>(m_grip);
}

std::optional<ToolCursor> ArtboardGuides::hover(
    const ToolContext& context, Point position) {
  auto grip = m_grip;
  if(!grip) {
    m_hover = find(context, position);
    grip = m_hover;
    if(!grip) {
      return std::nullopt;
    }
  } else if(m_is_shown_once && !is_visible(context)) {
    return ToolCursor();
  }
  if(grip->m_orientation == Orientation::VERTICAL) {
    return ToolCursor(CursorKind::RESIZE, 0);
  }
  return ToolCursor(CursorKind::RESIZE, std::numbers::pi / 2);
}

void ArtboardGuides::draw(
    const ToolContext& context, CanvasOverlay& overlay) const {
  auto& document = context.get_document();
  auto& view = context.get_view();
  if(!m_grip) {
    if(!m_hover || m_hover->m_index ||
        !context.get_guide_settings().m_is_visible) {
      return;
    }
    auto artboard = find_frame(document, m_hover->m_artboard);
    if(!artboard) {
      return;
    }
    auto screen = view.map(artboard->m_frame);
    auto band = OverlayShape();
    band.m_fill = CREATION_COLOR;
    if(m_hover->m_orientation == Orientation::VERTICAL) {
      band.m_center = Point(screen.m_x, screen.get_center().m_y);
      band.m_size = Size(CREATION_WIDTH, screen.m_height);
    } else {
      band.m_center = Point(screen.get_center().m_x, screen.m_y);
      band.m_size = Size(screen.m_width, CREATION_WIDTH);
    }
    overlay.m_shapes.push_back(band);
    return;
  }
  auto artboard = find_frame(document, m_grip->m_artboard);
  if(!artboard || !is_visible(context)) {
    return;
  }
  auto& frame = artboard->m_frame;
  auto screen = view.map(frame);
  auto is_vertical = m_grip->m_orientation == Orientation::VERTICAL;
  auto origin = view.map(frame.get_top_left());
  auto zoom = context.get_zoom();
  auto guide = view.map(Point(frame.m_x + m_position, frame.m_y + m_position));
  if(is_vertical) {
    overlay.m_shapes.push_back(make_overlay_line(Point(guide.m_x, screen.m_y),
      Point(guide.m_x, screen.get_bottom()), 1, ACTIVE_GUIDE_COLOR));
  } else {
    overlay.m_shapes.push_back(make_overlay_line(Point(screen.m_x, guide.m_y),
      Point(screen.get_right(), guide.m_y), 1, ACTIVE_GUIDE_COLOR));
  }
  auto lower = 0.0;
  auto upper = get_extent(*artboard, m_grip->m_orientation);
  for(auto i = std::size_t(0); i != artboard->m_guides.size(); ++i) {
    auto& other = artboard->m_guides[i];
    if(other.m_orientation != m_grip->m_orientation ||
        (m_grip->m_index && *m_grip->m_index == i)) {
      continue;
    }
    if(other.m_position < m_position) {
      lower = std::max(lower, other.m_position);
    } else if(other.m_position >= m_position + 1) {
      upper = std::min(upper, other.m_position);
    }
  }
  auto size = context.get_size();
  auto scale = context.get_scale();
  auto before = std::llround(m_position - lower);
  auto after = std::llround(upper - m_position);
  if(is_vertical) {
    auto start = origin.m_x + lower * zoom;
    auto end = origin.m_x + upper * zoom;
    auto at = origin.m_x + m_position * zoom;
    auto y = origin.m_y;
    if(y < VERTICAL_EDGE) {
      auto visible = intersect(screen, Rect(0, 0, size.m_width,
        size.m_height));
      y = visible.m_y + visible.m_height / 2;
    }
    auto line = std::round(y - 1 + 0.5) - 0.5;
    overlay.m_shapes.push_back(make_overlay_line(Point(std::round(start), line),
      Point(std::round(end), line), 1, DISTANCE_COLOR));
    auto is_narrow = end - start < HORIZONTAL_EDGE;
    auto left = std::optional<double>();
    auto right = std::optional<double>();
    auto left_alignment = Alignment::CENTER;
    auto right_alignment = Alignment::CENTER;
    if(before > 0) {
      if(is_narrow) {
        left_alignment = Alignment::RIGHT;
        left = at - 2;
      } else {
        left = start + (at - start) / 2;
      }
    }
    if(after > 0) {
      if(is_narrow) {
        right_alignment = Alignment::LEFT;
        right = at + 2;
      } else {
        right = at + (end - at) / 2;
      }
    }
    if(left && *left < SIDE_MARGIN) {
      left = SIDE_MARGIN;
      if(right) {
        right = std::max(*left + LABEL_GAP, *right);
      }
    }
    if(right && *right > size.m_width - SIDE_MARGIN) {
      right = size.m_width - SIDE_MARGIN;
      if(left) {
        left = std::min(*right - LABEL_GAP, *left);
      }
    }
    if(left) {
      add_text(overlay, to_text(static_cast<double>(before)),
        Point(*left, y + LABEL_OFFSET), left_alignment, scale);
    }
    if(right) {
      add_text(overlay, to_text(static_cast<double>(after)),
        Point(*right, y + LABEL_OFFSET), right_alignment, scale);
    }
    add_position(overlay, "X", m_position,
      Point(at + VERTICAL_POSITION_OFFSET.m_x,
        m_pointer.m_y + VERTICAL_POSITION_OFFSET.m_y), scale);
    return;
  }
  auto start = origin.m_y + lower * zoom;
  auto end = origin.m_y + upper * zoom;
  auto at = origin.m_y + m_position * zoom;
  auto x = origin.m_x;
  if(x < HORIZONTAL_EDGE) {
    auto visible = intersect(screen, Rect(0, 0, size.m_width, size.m_height));
    x = visible.m_x + visible.m_width / 2;
  }
  auto line = std::round(x - 1 + 0.5) - 0.5;
  overlay.m_shapes.push_back(make_overlay_line(Point(line, std::round(start)),
    Point(line, std::round(end)), 1, DISTANCE_COLOR));
  auto is_narrow = end - start < VERTICAL_EDGE;
  auto third = LABEL_FONT_SIZE / 3;
  auto top = std::optional<double>();
  auto bottom = std::optional<double>();
  if(before > 0) {
    top = is_narrow ? at - 2 : start + (at - start) / 2 + third;
  }
  if(after > 0) {
    bottom = is_narrow ? at + LABEL_FONT_SIZE : at + (end - at) / 2 + third;
  }
  if(top && *top < TOP_MARGIN) {
    top = TOP_MARGIN;
    if(bottom) {
      bottom = std::max(*top + LABEL_GAP, *bottom);
    }
  }
  if(bottom && *bottom > size.m_height - BOTTOM_MARGIN) {
    bottom = size.m_height - BOTTOM_MARGIN;
    if(top) {
      top = std::min(*bottom - LABEL_GAP, *top);
    }
  }
  if(top) {
    add_text(overlay, to_text(static_cast<double>(before)),
      Point(x + LABEL_OFFSET, *top), Alignment::RIGHT, scale);
  }
  if(bottom) {
    add_text(overlay, to_text(static_cast<double>(after)),
      Point(x + LABEL_OFFSET, *bottom), Alignment::RIGHT, scale);
  }
  add_position(overlay, "Y", m_position,
    Point(m_pointer.m_x + HORIZONTAL_POSITION_OFFSET.m_x,
      at + HORIZONTAL_POSITION_OFFSET.m_y), scale);
}

std::optional<ArtboardGuides::Grip> ArtboardGuides::find(
    const ToolContext& context, Point position) const {
  auto& settings = context.get_guide_settings();
  if(!settings.m_is_visible ||
      !context.get_titles().find(position).is_nil()) {
    return std::nullopt;
  }
  auto& document = context.get_document();
  auto& view = context.get_view();
  auto& canvas = document.get_canvas();
  for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
    auto& node = document.get(*i);
    auto artboard = node.get_artboard();
    if(!artboard || !node.m_header.m_is_visible || node.m_header.m_is_locked) {
      continue;
    }
    auto& frame = artboard->m_frame;
    auto screen = view.map(frame);
    auto is_across_x = position.m_x >= screen.m_x &&
      position.m_x <= screen.get_right();
    auto is_across_y = position.m_y >= screen.m_y &&
      position.m_y <= screen.get_bottom();
    if(!settings.m_is_locked) {
      auto best = std::optional<std::size_t>();
      auto distance = SLOP;
      for(auto j = std::size_t(0); j != artboard->m_guides.size(); ++j) {
        auto& guide = artboard->m_guides[j];
        if(!is_inside(*artboard, guide.m_orientation, guide.m_position)) {
          continue;
        }
        auto point = view.map(Point(frame.m_x + guide.m_position,
          frame.m_y + guide.m_position));
        auto offset = std::numeric_limits<double>::infinity();
        if(guide.m_orientation == Orientation::VERTICAL && is_across_y) {
          offset = std::abs(position.m_x - point.m_x);
        } else if(guide.m_orientation == Orientation::HORIZONTAL &&
            is_across_x) {
          offset = std::abs(position.m_y - point.m_y);
        }
        if(offset <= distance) {
          distance = offset;
          best = j;
        }
      }
      if(best) {
        return Grip(*i, artboard->m_guides[*best].m_orientation, best);
      }
    }
    if(is_across_y &&
        std::abs(position.m_x - screen.m_x) <= CREATION_WIDTH / 2) {
      return Grip(*i, Orientation::VERTICAL, std::nullopt);
    }
    if(is_across_x &&
        std::abs(position.m_y - screen.m_y) <= CREATION_WIDTH / 2) {
      return Grip(*i, Orientation::HORIZONTAL, std::nullopt);
    }
  }
  return std::nullopt;
}

bool ArtboardGuides::is_visible(const ToolContext& context) const {
  if(!m_grip) {
    return false;
  }
  auto artboard = find_frame(context.get_document(), m_grip->m_artboard);
  return artboard &&
    is_inside(*artboard, m_grip->m_orientation, m_position);
}

bool ArtboardGuides::is_taken(const ToolContext& context) const {
  if(!m_grip) {
    return false;
  }
  auto artboard = find_frame(context.get_document(), m_grip->m_artboard);
  if(!artboard) {
    return false;
  }
  for(auto i = std::size_t(0); i != artboard->m_guides.size(); ++i) {
    auto& guide = artboard->m_guides[i];
    if(guide.m_orientation == m_grip->m_orientation &&
        guide.m_position == m_position &&
        (!m_grip->m_index || *m_grip->m_index != i)) {
      return true;
    }
  }
  return false;
}

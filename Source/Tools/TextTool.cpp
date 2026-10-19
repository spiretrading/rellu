#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QTransform>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Text/TextEditing.hpp"
#include "Rellu/Tools/DrawTool.hpp"
#include "Rellu/Tools/TextTool.hpp"

using namespace Rellu;

namespace {
  constexpr auto FRAME_COLOR = make_color(0x26, 0x80, 0xEB);
  constexpr auto CARET_COLOR = make_color(0, 0, 0);
  constexpr auto LINE_WIDTH = 1.0;
  constexpr auto MINIMUM_FRAME = 1.0;

  bool is_shift(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ShiftModifier);
  }

  bool is_control(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::ControlModifier);
  }

  bool is_alt(Qt::KeyboardModifiers modifiers) {
    return modifiers.testFlag(Qt::AltModifier);
  }

  bool is_navigation(int key) {
    return key == Qt::Key_Left || key == Qt::Key_Right ||
      key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Home ||
      key == Qt::Key_End;
  }

  bool is_printable(const KeyEvent& event) {
    if(event.m_text.empty() ||
        (is_control(event.m_modifiers) && !is_alt(event.m_modifiers))) {
      return false;
    }
    return std::none_of(event.m_text.begin(), event.m_text.end(),
      [] (auto character) {
        return character < u' ' || character == u'\x7F';
      });
  }

  bool is_shortcut(const KeyEvent& event, int key) {
    return event.m_key == key && is_control(event.m_modifiers) &&
      !is_alt(event.m_modifiers) && !is_shift(event.m_modifiers);
  }

  std::u16string normalize_breaks(const QString& text) {
    auto normalized = text;
    normalized.replace(QString("\r\n"), QString("\n"));
    normalized.replace(QChar('\r'), QChar('\n'));
    return normalized.toStdU16String();
  }

  Rect make_rect(Point first, Point second) {
    auto left = std::min(first.m_x, second.m_x);
    auto top = std::min(first.m_y, second.m_y);
    return Rect(left, top, std::max(first.m_x, second.m_x) - left,
      std::max(first.m_y, second.m_y) - top);
  }

  void add_outline(CanvasOverlay& overlay, const Matrix& transform,
      const Rect& rect, Color color) {
    auto corners = std::array<Point, 4>({
      transform.map(rect.get_top_left()),
      transform.map(Point(rect.get_right(), rect.get_top())),
      transform.map(rect.get_bottom_right()),
      transform.map(Point(rect.get_left(), rect.get_bottom()))});
    for(auto i = std::size_t(0); i != corners.size(); ++i) {
      auto start = corners[i];
      auto end = corners[(i + 1) % corners.size()];
      auto length = distance(start, end);
      if(length == 0) {
        continue;
      }
      auto extension = (end - start) * (LINE_WIDTH / 2 / length);
      overlay.m_shapes.push_back(make_overlay_line(
        start - extension, end + extension, LINE_WIDTH, color));
    }
  }

  QColor to_qcolor(Color color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  QTransform to_qtransform(const Matrix& matrix) {
    return QTransform(matrix.m_a, matrix.m_b, matrix.m_c, matrix.m_d,
      matrix.m_tx, matrix.m_ty);
  }

  QPainterPath to_painter_path(const Path& path) {
    auto result = QPainterPath();
    result.setFillRule(Qt::WindingFill);
    for(auto& segment : path.get_segments()) {
      auto& points = segment.m_points;
      auto point = [&] (std::size_t index) {
        return QPointF(points[index].m_x, points[index].m_y);
      };
      if(segment.m_type == SegmentType::MOVE) {
        result.moveTo(point(0));
      } else if(segment.m_type == SegmentType::LINE) {
        result.lineTo(point(0));
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        result.quadTo(point(0), point(1));
      } else if(segment.m_type == SegmentType::CUBIC) {
        result.cubicTo(point(0), point(1), point(2));
      } else {
        result.closeSubpath();
      }
    }
    return result;
  }

  const Text* find_edited(const ToolContext& context) {
    auto& selection = context.get_editor().get_text_selection();
    if(!selection.is_editing()) {
      return nullptr;
    }
    auto node = context.get_document().find(selection.get_node());
    if(!node) {
      return nullptr;
    }
    return node->get_text();
  }

  NodeId find_parent(const ToolContext& context, Point point) {
    return find_drawing_parent(context, point);
  }
}

std::size_t Rellu::find_text_caret(const ToolContext& context,
    const NodeId& id, Point position, const FontLibrary& library) {
  auto& document = context.get_document();
  auto node = document.find(id);
  if(!node || !node->get_text()) {
    return 0;
  }
  auto inverse = invert(Rellu::get_world_transform(document, id));
  if(!inverse) {
    return 0;
  }
  auto& text = *node->get_text();
  return hit_test(measure_lines(text, library), *text.m_content,
    inverse->map(context.to_document(position)));
}

TextTool::TextTool(const FontLibrary& library) noexcept
  : m_library(&library),
    m_gesture(Gesture::NONE),
    m_press(0, 0),
    m_press_document(0, 0),
    m_current_document(0, 0),
    m_last_edit(EditKind::NONE),
    m_preedit_cursor(0),
    m_is_caret_visible(true),
    m_double_click_position(0, 0) {}

bool TextTool::is_editing(const ToolContext& context) const {
  return find_text(context) != nullptr;
}

void TextTool::edit(ToolContext& context, const NodeId& id,
    std::size_t anchor, std::size_t caret) {
  auto& editor = context.get_editor();
  auto& selection = editor.get_text_selection();
  if(selection.is_editing() && selection.get_node() != id) {
    commit(context);
  }
  auto node = context.get_document().find(id);
  if(!node || !node->get_text()) {
    return;
  }
  if(editor.get_selection().get_nodes() != std::vector{id}) {
    editor.get_selection().select(id);
  }
  editor.get_history().seal();
  m_last_edit = EditKind::NONE;
  m_goal = std::nullopt;
  m_preedit = std::nullopt;
  m_is_caret_visible = true;
  selection.set(id, anchor, caret);
}

void TextTool::commit(ToolContext& context) {
  m_gesture = Gesture::NONE;
  finish(context);
}

void TextTool::insert(ToolContext& context, std::u16string_view text) {
  sync(context);
  if(!is_editing(context)) {
    return;
  }
  replace(context, get_range(context), text, EditKind::INSERT);
}

void TextTool::input_method(
    ToolContext& context, const InputMethodEvent& event) {
  sync(context);
  auto text = find_text(context);
  if(!text) {
    return;
  }
  auto& selection = context.get_editor().get_text_selection();
  auto size = text->m_content->size();
  auto range = get_range(context);
  if(m_preedit) {
    range = TextRange(std::min(m_preedit->m_start, size),
      std::min(m_preedit->m_end, size));
  } else if(event.m_replacement_length > 0) {
    auto start = static_cast<std::ptrdiff_t>(selection.get_caret()) +
      event.m_replacement_start;
    start = std::clamp(start, std::ptrdiff_t(0),
      static_cast<std::ptrdiff_t>(size));
    auto end = std::min(static_cast<std::size_t>(start) +
      static_cast<std::size_t>(event.m_replacement_length), size);
    range = TextRange(static_cast<std::size_t>(start), end);
  }
  auto inserted = event.m_commit + event.m_preedit;
  m_preedit = std::nullopt;
  if(inserted.empty() && range.m_start == range.m_end) {
    return;
  }
  auto caret = range.m_start + event.m_commit.size();
  apply(context, type(context, *text, range, inserted), caret,
    EditKind::INSERT);
  if(!event.m_preedit.empty()) {
    m_preedit = TextRange(caret, caret + event.m_preedit.size());
    m_preedit_cursor = event.m_cursor;
    auto offset = std::clamp(event.m_cursor, 0,
      static_cast<int>(event.m_preedit.size()));
    selection.set(selection.get_node(), caret + offset, caret + offset);
  }
}

std::optional<TextRange> TextTool::get_preedit() const {
  return m_preedit;
}

bool TextTool::is_text_key(
    const ToolContext& context, const KeyEvent& event) const {
  if(!is_editing(context)) {
    return false;
  }
  auto key = event.m_key;
  if(key == Qt::Key_Escape || key == Qt::Key_Backspace ||
      key == Qt::Key_Delete || is_navigation(key)) {
    return true;
  } else if(key == Qt::Key_Return || key == Qt::Key_Enter ||
      key == Qt::Key_Tab) {
    return !is_control(event.m_modifiers);
  } else if(is_shortcut(event, Qt::Key_A) || is_shortcut(event, Qt::Key_C) ||
      is_shortcut(event, Qt::Key_X) || is_shortcut(event, Qt::Key_V)) {
    return true;
  }
  return is_printable(event);
}

std::optional<Rect> TextTool::get_caret_rect(
    const ToolContext& context) const {
  auto text = find_text(context);
  if(!text) {
    return std::nullopt;
  }
  auto& geometry = get_geometry(context);
  auto caret = std::min(context.get_editor().get_text_selection().get_caret(),
    text->m_content->size());
  auto rect = Rellu::get_caret_rect(geometry.m_lines, caret);
  auto transform = context.get_view() * get_node_transform(context);
  return make_rect(transform.map(rect.get_top_left()),
    transform.map(rect.get_bottom_right()));
}

bool TextTool::is_caret_visible() const {
  return m_is_caret_visible;
}

void TextTool::blink() {
  m_is_caret_visible = !m_is_caret_visible;
}

void TextTool::press(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  auto& editor = context.get_editor();
  m_is_caret_visible = true;
  sync(context);
  if(auto text = find_text(context)) {
    if(is_inside(context, event.m_position)) {
      auto index = hit(context, event.m_position);
      auto now = std::chrono::steady_clock::now();
      if(m_double_click && now - *m_double_click <= TRIPLE_CLICK_INTERVAL &&
          distance(event.m_position, m_double_click_position) <=
            DRAG_THRESHOLD) {
        m_double_click = std::nullopt;
        auto paragraph = find_paragraph(*text->m_content, index);
        select(context, paragraph.first, paragraph.second, true);
        return;
      }
      m_double_click = std::nullopt;
      auto anchor = index;
      if(is_shift(event.m_modifiers)) {
        anchor = editor.get_text_selection().get_anchor();
      }
      select(context, anchor, index, true);
      m_gesture = Gesture::SELECT;
      return;
    }
    commit(context);
    context.request_tool("select");
    return;
  }
  auto position = context.to_document(event.m_position);
  auto& document = context.get_document();
  auto id = context.get_hit_tester().hit(
    position, HIT_TOLERANCE / context.get_zoom());
  if(auto node = document.find(id); node && node->get_text()) {
    edit(context, id, 0, 0);
    auto index = hit(context, event.m_position);
    select(context, index, index, true);
    m_gesture = Gesture::SELECT;
    return;
  }
  m_gesture = Gesture::PENDING;
  m_press = event.m_position;
  m_press_document = position;
  m_current_document = position;
}

void TextTool::move(ToolContext& context, const PointerEvent& event) {
  if(m_gesture == Gesture::NONE) {
    return;
  }
  if(!event.m_buttons.testFlag(Qt::LeftButton)) {
    if(m_gesture == Gesture::SELECT) {
      m_gesture = Gesture::NONE;
    }
    return;
  }
  if(m_gesture == Gesture::PENDING) {
    if(distance(event.m_position, m_press) < DRAG_THRESHOLD) {
      return;
    }
    m_gesture = Gesture::FRAME;
  }
  if(m_gesture == Gesture::FRAME) {
    m_current_document = context.to_document(event.m_position);
  } else if(m_gesture == Gesture::SELECT && is_editing(context)) {
    auto& selection = context.get_editor().get_text_selection();
    select(context, selection.get_anchor(), hit(context, event.m_position),
      false);
  }
}

void TextTool::release(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  auto gesture = m_gesture;
  m_gesture = Gesture::NONE;
  if(gesture == Gesture::PENDING) {
    create(context, Rect(m_press_document.m_x, m_press_document.m_y, 0, 0),
      false);
  } else if(gesture == Gesture::FRAME) {
    m_current_document = context.to_document(event.m_position);
    auto frame = make_rect(m_press_document, m_current_document);
    create(context, frame, frame.m_width >= MINIMUM_FRAME &&
      frame.m_height >= MINIMUM_FRAME);
  }
}

void TextTool::double_click(ToolContext& context, const PointerEvent& event) {
  if(event.m_button != Qt::LeftButton) {
    return;
  }
  sync(context);
  auto text = find_text(context);
  if(!text || !is_inside(context, event.m_position)) {
    press(context, event);
    return;
  }
  m_gesture = Gesture::NONE;
  auto word =
    find_word(*text->m_content, hit(context, event.m_position));
  select(context, word.first, word.second, true);
  m_double_click = std::chrono::steady_clock::now();
  m_double_click_position = event.m_position;
}

bool TextTool::key_press(ToolContext& context, const KeyEvent& event) {
  sync(context);
  auto text = find_text(context);
  if(!text) {
    if(event.m_key != Qt::Key_Escape) {
      return false;
    }
    if(m_gesture != Gesture::NONE) {
      m_gesture = Gesture::NONE;
    } else {
      context.request_tool("select");
    }
    return true;
  }
  m_is_caret_visible = true;
  auto& content = *text->m_content;
  auto range = get_range(context);
  auto key = event.m_key;
  if(key == Qt::Key_Escape) {
    commit(context);
    context.request_tool("select");
    return true;
  } else if(key == Qt::Key_Backspace || key == Qt::Key_Delete) {
    if(range.m_start == range.m_end) {
      if(key == Qt::Key_Backspace) {
        if(range.m_start == 0) {
          return true;
        }
        if(is_control(event.m_modifiers)) {
          range.m_start = get_previous_word(content, range.m_start);
        } else {
          range.m_start = get_previous_grapheme(content, range.m_start);
        }
      } else {
        if(range.m_end == content.size()) {
          return true;
        }
        if(is_control(event.m_modifiers)) {
          range.m_end = get_next_word(content, range.m_end);
        } else {
          range.m_end = get_next_grapheme(content, range.m_end);
        }
      }
    }
    replace(context, range, std::u16string_view(), EditKind::DELETE);
    return true;
  } else if(key == Qt::Key_Return || key == Qt::Key_Enter) {
    if(is_control(event.m_modifiers)) {
      return false;
    }
    if(is_shift(event.m_modifiers)) {
      insert(context, u"\u2028");
    } else {
      insert(context, u"\n");
    }
    return true;
  } else if(key == Qt::Key_Tab) {
    if(is_control(event.m_modifiers)) {
      return false;
    }
    insert(context, u"\t");
    return true;
  } else if(is_shortcut(event, Qt::Key_A)) {
    select(context, 0, content.size(), true);
    return true;
  } else if(is_shortcut(event, Qt::Key_C)) {
    copy(context);
    return true;
  } else if(is_shortcut(event, Qt::Key_X)) {
    copy(context);
    if(range.m_start != range.m_end) {
      replace(context, range, std::u16string_view(), EditKind::NONE);
      context.get_editor().get_history().seal();
    }
    return true;
  } else if(is_shortcut(event, Qt::Key_V)) {
    if(auto clipboard = QGuiApplication::clipboard()) {
      auto pasted = normalize_breaks(clipboard->text());
      if(!pasted.empty() || range.m_start != range.m_end) {
        replace(context, range, pasted, EditKind::NONE);
        context.get_editor().get_history().seal();
      }
    }
    return true;
  } else if(is_navigation(key)) {
    return navigate(context, event);
  } else if(is_printable(event)) {
    insert(context, event.m_text);
    return true;
  }
  return false;
}

void TextTool::cancel(ToolContext& context) {
  commit(context);
}

bool TextTool::is_busy() const {
  return m_gesture != Gesture::NONE;
}

ToolCursor TextTool::get_cursor() const {
  return ToolCursor(CursorKind::TEXT, 0);
}

void TextTool::draw(const ToolContext& context, CanvasOverlay& overlay) const {
  auto& view = context.get_view();
  if(m_gesture == Gesture::FRAME) {
    add_outline(overlay, view, make_rect(m_press_document, m_current_document),
      FRAME_COLOR);
  }
  auto text = find_text(context);
  if(!text) {
    return;
  }
  auto& geometry = get_geometry(context);
  auto transform = view * get_node_transform(context);
  add_outline(overlay, transform, geometry.m_bounds, FRAME_COLOR);
  auto range = get_range(context);
  if(range.m_start != range.m_end) {
    if(auto highlight = get_highlight(context, range)) {
      overlay.m_images.push_back(std::move(*highlight));
    }
  }
  auto pixel = 1 / std::max(context.get_scale(), 1e-9);
  if(m_preedit) {
    auto size = text->m_content->size();
    auto start = std::min(m_preedit->m_start, size);
    auto end = std::min(m_preedit->m_end, size);
    for(auto& line : geometry.m_lines) {
      auto from = std::max(start, line.m_start);
      auto to = std::min(end, line.m_end);
      if(from >= to) {
        continue;
      }
      auto y = line.m_baseline + std::max(line.m_descent / 3, 1.0);
      overlay.m_shapes.push_back(make_overlay_line(
        transform.map(Point(get_caret_x(line, from), y)),
        transform.map(Point(get_caret_x(line, to), y)), pixel, CARET_COLOR));
    }
  }
  if(range.m_start != range.m_end || !m_is_caret_visible ||
      (m_preedit && m_preedit_cursor < 0)) {
    return;
  }
  auto caret = Rellu::get_caret_rect(geometry.m_lines, range.m_start);
  overlay.m_shapes.push_back(make_overlay_line(
    transform.map(caret.get_top_left()),
    transform.map(caret.get_bottom_right()), pixel, CARET_COLOR));
}

const Text* TextTool::find_text(const ToolContext& context) const {
  auto& editor = context.get_editor();
  if(!editor.get_selection().contains(
      editor.get_text_selection().get_node())) {
    return nullptr;
  }
  return find_edited(context);
}

void TextTool::sync(ToolContext& context) {
  if(context.get_editor().get_text_selection().is_editing() &&
      !find_text(context)) {
    finish(context);
  }
}

const TextTool::Geometry& TextTool::get_geometry(
    const ToolContext& context) const {
  auto& id = context.get_editor().get_text_selection().get_node();
  if(m_geometry.m_id == id && m_geometry.m_revision == context.get_revision()) {
    return m_geometry;
  }
  m_geometry.m_id = id;
  m_geometry.m_revision = context.get_revision();
  m_geometry.m_fills = std::nullopt;
  m_geometry.m_clip = std::nullopt;
  if(auto text = find_text(context)) {
    m_geometry.m_lines = measure_lines(*text, *m_library);
    m_geometry.m_bounds = measure_text(*text, *m_library);
    for(auto& line : m_geometry.m_lines) {
      m_geometry.m_bounds = unite(m_geometry.m_bounds,
        Rellu::get_caret_rect(m_geometry.m_lines, line.m_start));
    }
  } else {
    m_geometry.m_lines.clear();
    m_geometry.m_bounds = Rect(0, 0, 0, 0);
  }
  return m_geometry;
}

Matrix TextTool::get_node_transform(const ToolContext& context) const {
  return Rellu::get_world_transform(context.get_document(),
    context.get_editor().get_text_selection().get_node());
}

std::optional<Point> TextTool::to_local(
    const ToolContext& context, Point position) const {
  auto inverse = invert(get_node_transform(context));
  if(!inverse) {
    return std::nullopt;
  }
  return inverse->map(context.to_document(position));
}

bool TextTool::is_inside(const ToolContext& context, Point position) const {
  auto local = to_local(context, position);
  if(!local) {
    return false;
  }
  auto tolerance = HIT_TOLERANCE / std::max(context.get_zoom(), 1e-9);
  return inflate(get_geometry(context).m_bounds, tolerance).contains(*local);
}

std::size_t TextTool::hit(const ToolContext& context, Point position) const {
  auto text = find_text(context);
  auto local = to_local(context, position);
  if(!text || !local) {
    return 0;
  }
  return hit_test(get_geometry(context).m_lines, *text->m_content, *local);
}

TextRange TextTool::get_range(const ToolContext& context) const {
  auto text = find_text(context);
  if(!text) {
    return TextRange();
  }
  auto size = text->m_content->size();
  auto range = context.get_editor().get_text_selection().get_range();
  return TextRange(std::min(range.m_start, size), std::min(range.m_end, size));
}

void TextTool::select(ToolContext& context, std::size_t anchor,
    std::size_t caret, bool is_moving) {
  auto& editor = context.get_editor();
  auto& selection = editor.get_text_selection();
  if(is_moving) {
    editor.get_history().seal();
    m_last_edit = EditKind::NONE;
    m_goal = std::nullopt;
  }
  m_is_caret_visible = true;
  selection.set(selection.get_node(), anchor, caret);
}

void TextTool::move_caret(
    ToolContext& context, std::size_t caret, bool is_extending) {
  auto& selection = context.get_editor().get_text_selection();
  auto anchor = is_extending ? selection.get_anchor() : caret;
  auto goal = m_goal;
  select(context, anchor, caret, true);
  m_goal = goal;
}

std::optional<OverlayImage> TextTool::get_highlight(
    const ToolContext& context, TextRange range) const {
  auto& geometry = get_geometry(context);
  auto scale = std::max(context.get_scale(), 1e-9);
  auto transform = make_scale(scale, scale) * context.get_view() *
    get_node_transform(context);
  auto size = context.get_size();
  if(m_highlight.m_image && m_highlight.m_id == geometry.m_id &&
      m_highlight.m_revision == geometry.m_revision &&
      m_highlight.m_range == range && m_highlight.m_transform == transform &&
      m_highlight.m_size == size) {
    return OverlayImage(m_highlight.m_image, m_highlight.m_position);
  }
  auto text = find_text(context);
  if(!text) {
    return std::nullopt;
  }
  if(!geometry.m_fills) {
    auto layout = lay_out(*text, *m_library);
    m_geometry.m_fills = outline(layout);
    m_geometry.m_clip = layout.m_clip;
  }
  auto area = QPainterPath();
  area.setFillRule(Qt::WindingFill);
  for(auto& rect :
      get_selection_rects(geometry.m_lines, range.m_start, range.m_end)) {
    area.addRect(QRectF(rect.m_x, rect.m_y, rect.m_width, rect.m_height));
  }
  if(auto& clip = geometry.m_clip) {
    auto frame = QPainterPath();
    frame.addRect(
      QRectF(clip->m_x, clip->m_y, clip->m_width, clip->m_height));
    area = area.intersected(frame);
  }
  auto mapping = to_qtransform(transform);
  auto bounds = mapping.map(area).boundingRect().intersected(
    QRectF(0, 0, size.m_width * scale, size.m_height * scale));
  if(bounds.isEmpty()) {
    return std::nullopt;
  }
  auto left = std::floor(bounds.left());
  auto top = std::floor(bounds.top());
  auto width = static_cast<int>(std::ceil(bounds.right()) - left);
  auto height = static_cast<int>(std::ceil(bounds.bottom()) - top);
  auto image = std::make_shared<QImage>(
    width, height, QImage::Format_RGBA8888_Premultiplied);
  image->fill(Qt::transparent);
  auto painter = QPainter(image.get());
  painter.setRenderHint(QPainter::Antialiasing);
  painter.translate(-left, -top);
  painter.setWorldTransform(mapping, true);
  painter.fillPath(area, to_qcolor(HIGHLIGHT_COLOR));
  painter.setClipPath(area);
  for(auto& fill : *geometry.m_fills) {
    painter.fillPath(
      to_painter_path(fill.m_path), to_qcolor(HIGHLIGHTED_TEXT_COLOR));
  }
  painter.end();
  m_highlight.m_id = geometry.m_id;
  m_highlight.m_revision = geometry.m_revision;
  m_highlight.m_range = range;
  m_highlight.m_transform = transform;
  m_highlight.m_size = size;
  m_highlight.m_image = std::move(image);
  m_highlight.m_position = Point(left / scale, top / scale);
  return OverlayImage(m_highlight.m_image, m_highlight.m_position);
}

Text TextTool::type(const ToolContext& context, const Text& text,
    TextRange range, std::u16string_view inserted) const {
  auto result = replace_text(text, range, inserted);
  auto& pending =
    context.get_editor().get_text_selection().get_pending_style();
  if(!pending || inserted.empty()) {
    return result;
  }
  return edit_text_style(result,
    TextRange(range.m_start, range.m_start + inserted.size()),
    make_style_edit(*pending));
}

void TextTool::replace(ToolContext& context, TextRange range,
    std::u16string_view text, EditKind kind) {
  auto current = find_text(context);
  if(!current) {
    return;
  }
  if(m_preedit) {
    m_preedit = std::nullopt;
  }
  apply(context, type(context, *current, range, text),
    range.m_start + text.size(), kind);
}

void TextTool::apply(
    ToolContext& context, Text text, std::size_t caret, EditKind kind) {
  auto& editor = context.get_editor();
  auto& selection = editor.get_text_selection();
  auto id = selection.get_node();
  auto& node = context.get_document().get(id);
  if(kind == EditKind::NONE || kind != m_last_edit) {
    editor.get_history().seal();
  }
  m_last_edit = kind;
  auto& previous = *node.get_text();
  auto name = std::optional<std::string>();
  if(node.m_header.m_name.empty() ||
      node.m_header.m_name == make_text_name(*previous.m_content)) {
    name = make_text_name(*text.m_content);
  }
  text.m_layout = Shared(store_layout(text, *m_library));
  editor.merge(
    std::make_unique<EditTextCommand>(id, std::move(text), std::move(name)));
  m_goal = std::nullopt;
  m_is_caret_visible = true;
  selection.set(id, caret, caret);
}

void TextTool::create(ToolContext& context, Rect frame, bool is_area) {
  auto& editor = context.get_editor();
  auto& document = context.get_document();
  commit(context);
  auto parent = find_parent(context, frame.get_top_left());
  auto inverse = invert(Rellu::get_world_transform(document, parent));
  if(!inverse) {
    return;
  }
  auto top_left = inverse->map(frame.get_top_left());
  auto bottom_right = inverse->map(frame.get_bottom_right());
  top_left = Point(std::round(top_left.m_x), std::round(top_left.m_y));
  bottom_right =
    Point(std::round(bottom_right.m_x), std::round(bottom_right.m_y));
  auto style = get_default_text_style();
  auto text = Text();
  text.m_content = Shared(std::u16string());
  text.m_base_style = style;
  text.m_style.m_fill = SolidFill(style.m_color);
  if(is_area) {
    text.m_frame = AreaFrame(Size(std::max(bottom_right.m_x - top_left.m_x,
      MINIMUM_FRAME), std::max(bottom_right.m_y - top_left.m_y,
        MINIMUM_FRAME)));
  } else {
    text.m_frame = PositionedFrame();
  }
  auto header = NodeHeader();
  header.m_id = generate_node_id();
  header.m_transform = make_translation(top_left.m_x, top_left.m_y);
  auto id = header.m_id;
  editor.get_history().seal();
  editor.execute(std::make_unique<EditTextCommand>(
    TextInsertion(parent, std::move(header)), std::move(text)));
  if(!document.find(id)) {
    return;
  }
  m_last_edit = EditKind::INSERT;
  m_goal = std::nullopt;
  m_preedit = std::nullopt;
  m_is_caret_visible = true;
  editor.get_text_selection().set(id, 0, 0);
}

void TextTool::finish(ToolContext& context) {
  auto& editor = context.get_editor();
  auto& selection = editor.get_text_selection();
  if(!selection.is_editing()) {
    return;
  }
  if(m_preedit) {
    auto preedit = *m_preedit;
    m_preedit = std::nullopt;
    if(auto text = find_edited(context)) {
      apply(context, replace_text(*text, preedit, std::u16string_view()),
        preedit.m_start, EditKind::INSERT);
    }
  }
  if(auto text = find_edited(context); text && text->m_content->empty()) {
    editor.merge(std::make_unique<EditTextCommand>(selection.get_node()));
  }
  editor.get_history().seal();
  m_last_edit = EditKind::NONE;
  m_goal = std::nullopt;
  m_double_click = std::nullopt;
  selection.clear();
}

void TextTool::copy(const ToolContext& context) const {
  auto text = find_text(context);
  auto range = get_range(context);
  if(!text || range.m_start == range.m_end) {
    return;
  }
  if(auto clipboard = QGuiApplication::clipboard()) {
    auto selected = std::u16string_view(*text->m_content).substr(
      range.m_start, range.m_end - range.m_start);
    clipboard->setText(QString::fromUtf16(selected.data(),
      static_cast<qsizetype>(selected.size())));
  }
}

bool TextTool::navigate(ToolContext& context, const KeyEvent& event) {
  auto text = find_text(context);
  if(!text) {
    return false;
  }
  auto& content = *text->m_content;
  auto& selection = context.get_editor().get_text_selection();
  auto range = get_range(context);
  auto caret = std::min(selection.get_caret(), content.size());
  auto is_extending = is_shift(event.m_modifiers);
  auto is_word = is_control(event.m_modifiers);
  auto is_collapsing = !is_extending && range.m_start != range.m_end;
  auto& lines = get_geometry(context).m_lines;
  auto goal = m_goal;
  auto target = caret;
  m_goal = std::nullopt;
  if(event.m_key == Qt::Key_Left) {
    if(is_collapsing) {
      target = range.m_start;
    } else if(is_word) {
      target = get_previous_word(content, caret);
    } else {
      target = get_previous_grapheme(content, caret);
    }
  } else if(event.m_key == Qt::Key_Right) {
    if(is_collapsing) {
      target = range.m_end;
    } else if(is_word) {
      target = get_next_word(content, caret);
    } else {
      target = get_next_grapheme(content, caret);
    }
  } else if(event.m_key == Qt::Key_Up || event.m_key == Qt::Key_Down) {
    if(!goal && !lines.empty()) {
      goal = get_caret_x(lines[find_line(lines, caret)], caret);
    }
    auto delta = event.m_key == Qt::Key_Up ? -1 : 1;
    target = move_by_lines(lines, content, caret, goal.value_or(0), delta);
    m_goal = goal;
  } else if(event.m_key == Qt::Key_Home) {
    target = is_word ? 0 : get_line_start(lines, caret);
  } else if(event.m_key == Qt::Key_End) {
    target = is_word ? content.size() : get_line_end(lines, content, caret);
  }
  move_caret(context, target, is_extending);
  return true;
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <QChar>
#include <QString>
#include <QTextBoundaryFinder>
#include "Rellu/Text/TextEditing.hpp"

using namespace Rellu;

namespace {
  enum class CharacterKind : unsigned char {
    SPACE,
    SEPARATOR,
    WORD,
    OTHER
  };

  bool is_separator(char16_t character) {
    return character == u'\n' || character == u'\r' || character == u'\v' ||
      character == u' ' || character == u' ';
  }

  bool is_mark(char16_t character) {
    auto category = QChar::category(static_cast<char32_t>(character));
    return category == QChar::Mark_NonSpacing ||
      category == QChar::Mark_SpacingCombining ||
      category == QChar::Mark_Enclosing;
  }

  CharacterKind get_kind(char16_t character) {
    if(is_separator(character)) {
      return CharacterKind::SEPARATOR;
    } else if(QChar::isSpace(static_cast<char32_t>(character))) {
      return CharacterKind::SPACE;
    } else if(QChar::isSurrogate(character) || is_mark(character) ||
        character == u'_' ||
        QChar::isLetterOrNumber(static_cast<char32_t>(character))) {
      return CharacterKind::WORD;
    }
    return CharacterKind::OTHER;
  }

  bool is_boundary(std::u16string_view content, std::size_t index) {
    if(index == 0 || index >= content.size()) {
      return true;
    }
    if(QChar::isLowSurrogate(content[index]) &&
        QChar::isHighSurrogate(content[index - 1])) {
      return false;
    }
    return !is_mark(content[index]) || is_separator(content[index - 1]);
  }

  bool ends_with_wrapping_space(const std::vector<LineGeometry>& lines,
      std::u16string_view content, std::size_t line) {
    auto& geometry = lines[line];
    return !geometry.m_is_paragraph_end && line + 1 < lines.size() &&
      geometry.m_end > geometry.m_start && geometry.m_end <= content.size() &&
      get_kind(content[geometry.m_end - 1]) == CharacterKind::SPACE;
  }

  QTextBoundaryFinder make_grapheme_finder(std::u16string_view content) {
    return QTextBoundaryFinder(QTextBoundaryFinder::Grapheme,
      reinterpret_cast<const QChar*>(content.data()),
      static_cast<qsizetype>(content.size()));
  }

  std::size_t skip_separator_backward(
      std::u16string_view content, std::size_t index) {
    if(index >= 2 && content[index - 1] == u'\n' &&
        content[index - 2] == u'\r') {
      return index - 2;
    }
    return index - 1;
  }
}

std::size_t Rellu::find_line(
    const std::vector<LineGeometry>& lines, std::size_t index) {
  for(auto i = std::size_t(0); i != lines.size(); ++i) {
    auto& line = lines[i];
    if(index < line.m_start) {
      return i == 0 ? 0 : i - 1;
    } else if(index < line.m_end) {
      return i;
    } else if(index == line.m_end && (line.m_is_paragraph_end ||
        i + 1 == lines.size() || lines[i + 1].m_start != index)) {
      return i;
    }
  }
  return lines.empty() ? 0 : lines.size() - 1;
}

double Rellu::get_caret_x(const LineGeometry& line, std::size_t index) {
  if(line.m_carets.empty()) {
    return 0;
  }
  auto offset = std::clamp(index, line.m_start, line.m_end) - line.m_start;
  return line.m_carets[std::min(offset, line.m_carets.size() - 1)];
}

Rect Rellu::get_caret_rect(
    const std::vector<LineGeometry>& lines, std::size_t index) {
  if(lines.empty()) {
    return Rect(0, 0, 0, 0);
  }
  auto& line = lines[find_line(lines, index)];
  return Rect(get_caret_x(line, index), line.m_baseline - line.m_ascent, 0,
    line.m_ascent + line.m_descent);
}

std::vector<Rect> Rellu::get_selection_rects(
    const std::vector<LineGeometry>& lines, std::size_t start,
    std::size_t end) {
  auto rects = std::vector<Rect>();
  if(start >= end) {
    return rects;
  }
  for(auto i = std::size_t(0); i != lines.size(); ++i) {
    auto& line = lines[i];
    auto from = std::max(start, line.m_start);
    auto to = std::min(end, line.m_end);
    auto is_break_selected = line.m_is_paragraph_end &&
      i + 1 != lines.size() && start <= line.m_end && end > line.m_end;
    if(from > to || (from == to && !is_break_selected)) {
      continue;
    }
    auto left = get_caret_x(line, from);
    auto right = get_caret_x(line, to);
    auto height = line.m_ascent + line.m_descent;
    if(is_break_selected) {
      right += height / 4;
    }
    rects.push_back(Rect(std::min(left, right), line.m_baseline - line.m_ascent,
      std::abs(right - left), height));
  }
  return rects;
}

std::size_t Rellu::hit_test_line(const std::vector<LineGeometry>& lines,
    std::u16string_view content, std::size_t line, double x) {
  if(lines.empty()) {
    return 0;
  }
  line = std::min(line, lines.size() - 1);
  auto& geometry = lines[line];
  auto last = geometry.m_end;
  if(ends_with_wrapping_space(lines, content, line)) {
    --last;
  }
  auto best = geometry.m_start;
  auto best_distance = std::numeric_limits<double>::infinity();
  for(auto index = geometry.m_start; index <= last; ++index) {
    if(!is_boundary(content, index)) {
      continue;
    }
    auto distance = std::abs(get_caret_x(geometry, index) - x);
    if(distance < best_distance) {
      best = index;
      best_distance = distance;
    }
  }
  return best;
}

std::size_t Rellu::hit_test(const std::vector<LineGeometry>& lines,
    std::u16string_view content, Point point) {
  for(auto i = std::size_t(0); i != lines.size(); ++i) {
    if(i + 1 == lines.size()) {
      return hit_test_line(lines, content, i, point.m_x);
    }
    auto bottom = lines[i].m_baseline + lines[i].m_descent;
    auto top = lines[i + 1].m_baseline - lines[i + 1].m_ascent;
    if(point.m_y < (bottom + top) / 2) {
      return hit_test_line(lines, content, i, point.m_x);
    }
  }
  return 0;
}

std::size_t Rellu::move_by_lines(const std::vector<LineGeometry>& lines,
    std::u16string_view content, std::size_t index, double x, int delta) {
  if(lines.empty()) {
    return index;
  }
  auto target = static_cast<std::ptrdiff_t>(find_line(lines, index)) + delta;
  if(target < 0) {
    return 0;
  } else if(target >= static_cast<std::ptrdiff_t>(lines.size())) {
    return content.size();
  }
  return hit_test_line(lines, content, static_cast<std::size_t>(target), x);
}

std::size_t Rellu::get_line_start(
    const std::vector<LineGeometry>& lines, std::size_t index) {
  if(lines.empty()) {
    return 0;
  }
  return lines[find_line(lines, index)].m_start;
}

std::size_t Rellu::get_line_end(const std::vector<LineGeometry>& lines,
    std::u16string_view content, std::size_t index) {
  if(lines.empty()) {
    return content.size();
  }
  auto line = find_line(lines, index);
  if(ends_with_wrapping_space(lines, content, line)) {
    return lines[line].m_end - 1;
  }
  return lines[line].m_end;
}

std::size_t Rellu::get_next_grapheme(
    std::u16string_view content, std::size_t index) {
  if(index >= content.size()) {
    return content.size();
  }
  if(content[index] == u'\r' && index + 1 < content.size() &&
      content[index + 1] == u'\n') {
    return index + 2;
  }
  auto finder = make_grapheme_finder(content);
  finder.setPosition(static_cast<qsizetype>(index));
  auto next = finder.toNextBoundary();
  if(next < 0) {
    return content.size();
  }
  return static_cast<std::size_t>(next);
}

std::size_t Rellu::get_previous_grapheme(
    std::u16string_view content, std::size_t index) {
  if(index == 0) {
    return 0;
  }
  index = std::min(index, content.size());
  if(index >= 2 && content[index - 1] == u'\n' &&
      content[index - 2] == u'\r') {
    return index - 2;
  }
  auto finder = make_grapheme_finder(content);
  finder.setPosition(static_cast<qsizetype>(index));
  auto previous = finder.toPreviousBoundary();
  if(previous < 0) {
    return 0;
  }
  return static_cast<std::size_t>(previous);
}

std::size_t Rellu::get_next_word(
    std::u16string_view content, std::size_t index) {
  if(index >= content.size()) {
    return content.size();
  }
  auto kind = get_kind(content[index]);
  if(kind == CharacterKind::SEPARATOR) {
    return get_next_grapheme(content, index);
  }
  if(kind != CharacterKind::SPACE) {
    while(index < content.size() && get_kind(content[index]) == kind) {
      ++index;
    }
  }
  while(index < content.size() &&
      get_kind(content[index]) == CharacterKind::SPACE) {
    ++index;
  }
  return index;
}

std::size_t Rellu::get_previous_word(
    std::u16string_view content, std::size_t index) {
  index = std::min(index, content.size());
  auto position = index;
  while(position > 0 &&
      get_kind(content[position - 1]) == CharacterKind::SPACE) {
    --position;
  }
  if(position == 0) {
    return 0;
  }
  auto kind = get_kind(content[position - 1]);
  if(kind == CharacterKind::SEPARATOR) {
    if(position == index) {
      return skip_separator_backward(content, position);
    }
    return position;
  }
  while(position > 0 && get_kind(content[position - 1]) == kind) {
    --position;
  }
  return position;
}

std::pair<std::size_t, std::size_t> Rellu::find_word(
    std::u16string_view content, std::size_t index) {
  index = std::min(index, content.size());
  auto position = index;
  auto is_at_word = position < content.size() &&
    get_kind(content[position]) == CharacterKind::WORD;
  if(position > 0 && !is_at_word &&
      get_kind(content[position - 1]) != CharacterKind::SEPARATOR &&
      (position == content.size() ||
        get_kind(content[position]) == CharacterKind::SEPARATOR ||
        get_kind(content[position - 1]) == CharacterKind::WORD)) {
    --position;
  }
  if(position >= content.size() ||
      get_kind(content[position]) == CharacterKind::SEPARATOR) {
    return std::pair(index, index);
  }
  auto kind = get_kind(content[position]);
  auto start = position;
  while(start > 0 && get_kind(content[start - 1]) == kind) {
    --start;
  }
  auto end = position + 1;
  while(end < content.size() && get_kind(content[end]) == kind) {
    ++end;
  }
  return std::pair(start, end);
}

std::pair<std::size_t, std::size_t> Rellu::find_paragraph(
    std::u16string_view content, std::size_t index) {
  index = std::min(index, content.size());
  auto start = index;
  while(start > 0 && !is_separator(content[start - 1])) {
    --start;
  }
  auto end = index;
  while(end < content.size() && !is_separator(content[end])) {
    ++end;
  }
  return std::pair(start, end);
}

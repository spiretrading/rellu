#include <algorithm>
#include <cmath>
#include <utility>
#include <QChar>
#include <QString>
#include <QTextBoundaryFinder>
#include "Rellu/Text/TextLayout.hpp"

using namespace Rellu;

namespace {
  constexpr auto TAB_INTERVAL = 4.0;

  struct Span {
    std::size_t m_start;
    std::size_t m_end;
    const TextStyle* m_style;
  };

  struct Item {
    std::shared_ptr<const Typeface> m_typeface;
    std::size_t m_start;
    std::size_t m_length;
  };

  struct Segment {
    std::size_t m_start;
    std::size_t m_end;
    const Font* m_font;
    const Color* m_color;
  };

  struct Line {
    std::vector<Segment> m_segments;
    bool m_is_paragraph_end = false;
  };

  struct LineMetrics {
    double m_ascent;
    double m_descent;
  };

  bool is_line_separator(char16_t character) {
    return character == u'\n' || character == u'\r' ||
      character == u'\u2028' || character == u'\u2029';
  }

  bool is_invisible(char32_t character) {
    auto category = QChar::category(character);
    return QChar::isSpace(character) || category == QChar::Other_Format ||
      category == QChar::Other_Control;
  }

  bool is_mark(char32_t character) {
    auto category = QChar::category(character);
    return category == QChar::Mark_NonSpacing ||
      category == QChar::Mark_SpacingCombining ||
      category == QChar::Mark_Enclosing;
  }

  QString to_qstring(std::u16string_view text) {
    return QString(reinterpret_cast<const QChar*>(text.data()),
      static_cast<qsizetype>(text.size()));
  }

  std::vector<Span> make_spans(const Text& text) {
    auto spans = std::vector<Span>();
    auto size = text.m_content->size();
    if(text.m_runs.empty()) {
      spans.push_back(Span(0, size, &text.m_base_style));
      return spans;
    }
    auto start = std::size_t(0);
    for(auto& run : text.m_runs) {
      auto end = std::min(size, start + run.m_length);
      if(end > start) {
        spans.push_back(Span(start, end, &run.m_style));
      }
      start = end;
    }
    if(start < size) {
      if(spans.empty()) {
        spans.push_back(Span(start, size, &text.m_runs.back().m_style));
      } else {
        spans.back().m_end = size;
      }
    }
    return spans;
  }

  const TextStyle& find_style(
      const std::vector<Span>& spans, const Text& text, std::size_t index) {
    for(auto& span : spans) {
      if(index < span.m_end) {
        return *span.m_style;
      }
    }
    if(!spans.empty()) {
      return *spans.back().m_style;
    }
    return text.m_base_style;
  }

  std::vector<Item> itemize(const std::u16string& text,
      const std::shared_ptr<const Typeface>& primary,
      const FontLibrary& library) {
    auto items = std::vector<Item>();
    auto i = std::size_t(0);
    while(i != text.size()) {
      auto character = static_cast<char32_t>(text[i]);
      auto length = std::size_t(1);
      if(QChar::isHighSurrogate(text[i]) && i + 1 != text.size() &&
          QChar::isLowSurrogate(text[i + 1])) {
        character = QChar::surrogateToUcs4(text[i], text[i + 1]);
        length = 2;
      }
      auto typeface = primary;
      if(is_mark(character) && !items.empty()) {
        typeface = items.back().m_typeface;
      } else if(!is_invisible(character) && !primary->has_glyph(character)) {
        if(auto fallback = library.find_fallback(character)) {
          typeface = std::move(fallback);
        }
      }
      if(items.empty() || items.back().m_typeface != typeface) {
        items.push_back(Item(std::move(typeface), i, length));
      } else {
        items.back().m_length += length;
      }
      i += length;
    }
    return items;
  }

  double place(const std::u16string& content, std::size_t start,
      std::size_t end, const TextStyle& style, const Font& font, Color color,
      Point pen, double origin, const FontLibrary& library,
      TextLayout* layout) {
    auto resolution = library.resolve(font);
    if(!resolution.m_typeface || start >= end) {
      return 0;
    }
    auto& metrics = resolution.m_typeface->get_metrics();
    auto size = font.m_size;
    auto baseline = pen.m_y;
    if(style.m_script == TextScript::SUPERSCRIPT) {
      baseline -= metrics.m_superscript_offset * size;
      size *= metrics.m_superscript_size;
    } else if(style.m_script == TextScript::SUBSCRIPT) {
      baseline += metrics.m_subscript_offset * size;
      size *= metrics.m_subscript_size;
    }
    auto spacing = style.m_letter_spacing * font.m_size / 1000;
    auto x = pen.m_x;
    auto display = std::u16string();
    auto flush = [&] {
      display = apply_transform(display, style.m_transform);
      for(auto& item : itemize(display, resolution.m_typeface, library)) {
        auto glyphs =
          item.m_typeface->shape(display, item.m_start, item.m_length);
        for(auto i = std::size_t(0); i != glyphs.size(); ++i) {
          auto& glyph = glyphs[i];
          if(layout &&
              !item.m_typeface->get_outline(glyph.m_index).is_empty()) {
            layout->m_glyphs.push_back(PlacedGlyph(item.m_typeface,
              glyph.m_index, Point(x + glyph.m_offset.m_x * size,
                baseline + glyph.m_offset.m_y * size), size, color));
          }
          x += glyph.m_advance * size;
          if(i + 1 == glyphs.size() ||
              glyphs[i + 1].m_cluster != glyph.m_cluster) {
            x += spacing;
          }
        }
      }
      display.clear();
    };
    for(auto i = start; i != end; ++i) {
      if(content[i] == u'\t') {
        flush();
        auto interval = TAB_INTERVAL * font.m_size;
        x = origin + (std::floor((x - origin) / interval) + 1) * interval;
      } else if(!is_line_separator(content[i])) {
        display.push_back(content[i]);
      }
    }
    flush();
    if(layout && x > pen.m_x) {
      if(style.m_is_underlined) {
        layout->m_decorations.push_back(TextDecoration(Rect(pen.m_x,
          baseline - metrics.m_underline_offset * size, x - pen.m_x,
          metrics.m_underline_thickness * size), color));
      }
      if(style.m_is_struck_through) {
        layout->m_decorations.push_back(TextDecoration(Rect(pen.m_x,
          baseline - metrics.m_strikeout_offset * size, x - pen.m_x,
          metrics.m_strikeout_thickness * size), color));
      }
    }
    return x - pen.m_x;
  }

  double place(const std::u16string& content, const Segment& segment,
      std::size_t end, const std::vector<Span>& spans, Point pen,
      double origin, const FontLibrary& library, TextLayout* layout) {
    auto x = pen.m_x;
    for(auto& span : spans) {
      auto from = std::max(segment.m_start, span.m_start);
      auto to = std::min({end, segment.m_end, span.m_end});
      if(from < to) {
        auto& style = *span.m_style;
        auto& font = segment.m_font ? *segment.m_font : style.m_font;
        auto color = segment.m_color ? *segment.m_color : style.m_color;
        x += place(content, from, to, style, font, color, Point(x, pen.m_y),
          origin, library, layout);
      }
    }
    return x - pen.m_x;
  }

  double place(const std::u16string& content, const Line& line,
      std::size_t end, const std::vector<Span>& spans, Point pen,
      const FontLibrary& library, TextLayout* layout) {
    auto x = pen.m_x;
    for(auto& segment : line.m_segments) {
      x += place(content, segment, end, spans, Point(x, pen.m_y), pen.m_x,
        library, layout);
    }
    return x - pen.m_x;
  }

  std::size_t trim_end(
      const std::u16string& content, std::size_t start, std::size_t end) {
    while(end > start && (QChar::isSpace(content[end - 1]) ||
        is_line_separator(content[end - 1]))) {
      --end;
    }
    return end;
  }

  double measure_width(const std::u16string& content, const Line& line,
      const std::vector<Span>& spans, const FontLibrary& library) {
    if(line.m_segments.empty()) {
      return 0;
    }
    auto start = line.m_segments.front().m_start;
    auto end = line.m_segments.back().m_end;
    if(line.m_is_paragraph_end) {
      while(end > start && is_line_separator(content[end - 1])) {
        --end;
      }
    } else {
      end = trim_end(content, start, end);
    }
    return place(content, line, end, spans, Point(0, 0), library, nullptr);
  }

  double measure(const std::u16string& content, std::size_t start,
      std::size_t end, const std::vector<Span>& spans,
      const FontLibrary& library) {
    auto line = Line({Segment(start, end, nullptr, nullptr)}, false);
    return place(content, line, end, spans, Point(0, 0), library, nullptr);
  }

  std::vector<std::pair<std::size_t, std::size_t>> break_paragraph(
      const std::u16string& content, std::size_t start, std::size_t end,
      const std::vector<Span>& spans, const FontLibrary& library,
      std::optional<double> width) {
    auto lines = std::vector<std::pair<std::size_t, std::size_t>>();
    if(!width || end - start < 2) {
      lines.emplace_back(start, end);
      return lines;
    }
    auto paragraph = to_qstring(
      std::u16string_view(content).substr(start, end - start));
    auto finder = QTextBoundaryFinder(QTextBoundaryFinder::Line, paragraph);
    auto line_start = start;
    auto line_width = 0.0;
    auto word_start = start;
    while(finder.toNextBoundary() != -1) {
      auto word_end = start + static_cast<std::size_t>(finder.position());
      if(word_end <= word_start) {
        continue;
      }
      auto visible = measure(content, word_start,
        trim_end(content, word_start, word_end), spans, library);
      auto total = measure(content, word_start, word_end, spans, library);
      if(word_start != line_start && line_width + visible > *width + 1e-6) {
        lines.emplace_back(line_start, word_start);
        line_start = word_start;
        line_width = 0;
      }
      line_width += total;
      word_start = word_end;
    }
    lines.emplace_back(line_start, end);
    return lines;
  }

  std::optional<double> get_wrap_width(const TextFrame& frame) {
    if(auto area = std::get_if<AreaFrame>(&frame)) {
      return area->m_size.m_width;
    } else if(auto automatic = std::get_if<AutoHeightFrame>(&frame)) {
      return automatic->m_width;
    }
    return std::nullopt;
  }

  std::vector<Line> break_lines(const Text& text,
      const std::vector<Span>& spans, const FontLibrary& library) {
    auto& content = *text.m_content;
    auto width = get_wrap_width(text.m_frame);
    auto lines = std::vector<Line>();
    auto start = std::size_t(0);
    while(true) {
      auto end = start;
      while(end != content.size() && !is_line_separator(content[end])) {
        ++end;
      }
      for(auto& range :
          break_paragraph(content, start, end, spans, library, width)) {
        lines.push_back(Line(
          {Segment(range.first, range.second, nullptr, nullptr)}, false));
      }
      lines.back().m_is_paragraph_end = true;
      if(end == content.size()) {
        break;
      }
      start = end + 1;
      if(content[end] == u'\r' && start != content.size() &&
          content[start] == u'\n') {
        ++start;
      }
    }
    return lines;
  }

  std::vector<Line> read_lines(const StoredLayout& stored, std::size_t size) {
    auto lines = std::vector<Line>();
    for(auto& paragraph : stored.m_paragraphs) {
      for(auto& stored_line : paragraph.m_lines) {
        auto line = Line();
        for(auto& run : stored_line.m_runs) {
          auto from = std::min(run.m_from, size);
          auto to = std::clamp(run.m_to, from, size);
          line.m_segments.push_back(Segment(from, to,
            run.m_font ? &*run.m_font : nullptr,
            run.m_color ? &*run.m_color : nullptr));
        }
        if(!line.m_segments.empty()) {
          lines.push_back(std::move(line));
        }
      }
      if(!lines.empty()) {
        lines.back().m_is_paragraph_end = true;
      }
    }
    return lines;
  }

  LineMetrics measure_line(const Line& line, const std::vector<Span>& spans,
      const Text& text, const FontLibrary& library) {
    auto metrics = LineMetrics(0, 0);
    auto add = [&] (const Font& font) {
      auto resolution = library.resolve(font);
      if(!resolution.m_typeface) {
        return;
      }
      auto& face = resolution.m_typeface->get_metrics();
      auto ascent = std::floor(face.m_line_ascent * font.m_size + 0.5);
      auto descent = std::floor(face.m_line_descent * font.m_size + 0.5);
      metrics.m_ascent = std::max(metrics.m_ascent, ascent);
      metrics.m_descent = std::max(metrics.m_descent, descent);
    };
    auto is_found = false;
    for(auto& segment : line.m_segments) {
      for(auto& span : spans) {
        if(std::max(segment.m_start, span.m_start) <
            std::min(segment.m_end, span.m_end)) {
          add(segment.m_font ? *segment.m_font : span.m_style->m_font);
          is_found = true;
        }
      }
    }
    if(!is_found && !line.m_segments.empty()) {
      auto& segment = line.m_segments.front();
      add(segment.m_font ? *segment.m_font :
        find_style(spans, text, segment.m_start).m_font);
    }
    return metrics;
  }

  void place_lines(const Text& text, const std::vector<Line>& lines,
      const std::vector<Span>& spans, const FontLibrary& library,
      TextLayout& layout) {
    auto& content = *text.m_content;
    auto& style = text.m_paragraph_style;
    auto width = get_wrap_width(text.m_frame).value_or(0);
    auto is_positioned = std::holds_alternative<PositionedFrame>(text.m_frame);
    auto baseline = 0.0;
    auto descent = 0.0;
    for(auto i = std::size_t(0); i != lines.size(); ++i) {
      auto& line = lines[i];
      auto metrics = measure_line(line, spans, text, library);
      if(i == 0) {
        if(!is_positioned) {
          baseline = metrics.m_ascent;
        }
      } else {
        baseline += std::round(
          style.m_line_height.value_or(descent + metrics.m_ascent));
        if(lines[i - 1].m_is_paragraph_end) {
          baseline += style.m_paragraph_spacing;
        }
      }
      descent = metrics.m_descent;
      auto x = 0.0;
      if(style.m_alignment != TextAlignment::LEFT) {
        auto line_width = measure_width(content, line, spans, library);
        if(style.m_alignment == TextAlignment::CENTER) {
          x = (width - line_width) / 2;
        } else {
          x = width - line_width;
        }
      }
      place(content, line, content.size(), spans, Point(x, baseline),
        library, &layout);
    }
  }
}

std::u16string Rellu::apply_transform(
    std::u16string_view text, TextTransform transform) {
  if(transform == TextTransform::NONE) {
    return std::u16string(text);
  }
  auto string = to_qstring(text);
  if(transform == TextTransform::UPPERCASE) {
    return string.toUpper().toStdU16String();
  } else if(transform == TextTransform::LOWERCASE) {
    return string.toLower().toStdU16String();
  }
  auto starts = std::vector<qsizetype>();
  auto finder = QTextBoundaryFinder(QTextBoundaryFinder::Word, string);
  do {
    if(finder.boundaryReasons() & QTextBoundaryFinder::StartOfItem) {
      starts.push_back(finder.position());
    }
  } while(finder.toNextBoundary() != -1);
  auto result = QString();
  result.reserve(string.size());
  auto position = qsizetype(0);
  for(auto start : starts) {
    if(start < position || start >= string.size()) {
      continue;
    }
    auto length = qsizetype(1);
    if(string.at(start).isHighSurrogate() && start + 1 < string.size()) {
      length = 2;
    }
    result += string.mid(position, start - position);
    result += string.mid(start, length).toUpper();
    position = start + length;
  }
  result += string.mid(position);
  return result.toStdU16String();
}

TextLayout Rellu::lay_out(const Text& text, const FontLibrary& library) {
  auto layout = TextLayout();
  auto spans = make_spans(text);
  if(text.m_layout) {
    place_lines(text, read_lines(**text.m_layout, text.m_content->size()),
      spans, library, layout);
  } else {
    place_lines(text, break_lines(text, spans, library), spans, library,
      layout);
  }
  if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
    layout.m_clip =
      Rect(0, 0, area->m_size.m_width, area->m_size.m_height);
  }
  return layout;
}

std::vector<TextFill> Rellu::outline(const TextLayout& layout) {
  auto fills = std::vector<TextFill>();
  auto get_fill = [&] (std::size_t first, Color color) -> TextFill& {
    for(auto i = first; i != fills.size(); ++i) {
      if(fills[i].m_color == color) {
        return fills[i];
      }
    }
    return fills.emplace_back(Path(), color);
  };
  for(auto& glyph : layout.m_glyphs) {
    auto& path = get_fill(0, glyph.m_color).m_path;
    auto map = [&] (Point point) {
      return Point(glyph.m_origin.m_x + point.m_x * glyph.m_size,
        glyph.m_origin.m_y + point.m_y * glyph.m_size);
    };
    for(auto& segment : glyph.m_typeface->get_outline(glyph.m_index).
        get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        path.move_to(map(points[0]));
      } else if(segment.m_type == SegmentType::LINE) {
        path.line_to(map(points[0]));
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        path.quadratic_to(map(points[0]), map(points[1]));
      } else if(segment.m_type == SegmentType::CUBIC) {
        path.cubic_to(map(points[0]), map(points[1]), map(points[2]));
      } else {
        path.close();
      }
    }
  }
  auto first = fills.size();
  for(auto& decoration : layout.m_decorations) {
    auto& path = get_fill(first, decoration.m_color).m_path;
    auto& rect = decoration.m_rect;
    path.move_to(rect.get_top_left());
    path.line_to(Point(rect.get_right(), rect.get_top()));
    path.line_to(rect.get_bottom_right());
    path.line_to(Point(rect.get_left(), rect.get_bottom()));
    path.close();
  }
  return fills;
}

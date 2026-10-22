#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <QChar>
#include <QString>
#include <QTextBoundaryFinder>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Text/TextLayout.hpp"

using namespace Rellu;

namespace {
  constexpr auto TAB_INTERVAL = 4.0;
  constexpr auto EMBOLDENING_DIVISOR = 50.0;

  struct Span {
    std::size_t m_start;
    std::size_t m_end;
    const TextStyle* m_style;
  };

  struct Item {
    FontFallback m_face;
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
    double m_clipping_ascent;
    double m_clipping_descent;
  };

  struct CaretSink {
    std::size_t m_start;
    std::vector<double> m_positions;

    void set(std::size_t index, double x) {
      if(index >= m_start && index - m_start < m_positions.size()) {
        m_positions[index - m_start] = x;
      }
    }
  };

  struct ClusterStop {
    std::size_t m_cluster;
    double m_x;
  };

  bool is_line_separator(char16_t character) {
    return character == u'\n' || character == u'\r' || character == u'\v' ||
      character == u'\u2028' || character == u'\u2029';
  }

  std::size_t skip_line_separator(
      const std::u16string& content, std::size_t index) {
    if(index >= content.size()) {
      return content.size();
    } else if(content[index] == u'\r' && index + 1 != content.size() &&
        content[index + 1] == u'\n') {
      return index + 2;
    }
    return index + 1;
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

  void append(Path& path, const Path& outline, const Matrix& transform) {
    for(auto& segment : outline.get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        path.move_to(transform.map(points[0]));
      } else if(segment.m_type == SegmentType::LINE) {
        path.line_to(transform.map(points[0]));
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        path.quadratic_to(transform.map(points[0]), transform.map(points[1]));
      } else if(segment.m_type == SegmentType::CUBIC) {
        path.cubic_to(transform.map(points[0]), transform.map(points[1]),
          transform.map(points[2]));
      } else {
        path.close();
      }
    }
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
      auto face = FontFallback(primary, false);
      if(is_mark(character) && !items.empty()) {
        face = items.back().m_face;
      } else if(!is_invisible(character) && !primary->has_glyph(character)) {
        if(auto fallback =
            library.find_fallback(character, primary->get_names());
            fallback.m_typeface) {
          face = std::move(fallback);
        }
      }
      if(items.empty() ||
          items.back().m_face.m_typeface != face.m_typeface ||
          items.back().m_face.m_is_emboldened != face.m_is_emboldened) {
        items.push_back(Item(std::move(face), i, length));
      } else {
        items.back().m_length += length;
      }
      i += length;
    }
    return items;
  }

  void record(CaretSink& carets, const std::vector<std::size_t>& sources,
      std::vector<ClusterStop>& stops, bool is_mapped, double start,
      double end) {
    if(sources.empty()) {
      return;
    }
    if(!is_mapped || stops.empty()) {
      for(auto i = std::size_t(0); i != sources.size(); ++i) {
        carets.set(sources[i], start + (end - start) *
          static_cast<double>(i) / static_cast<double>(sources.size()));
      }
      return;
    }
    std::stable_sort(stops.begin(), stops.end(),
      [] (const auto& left, const auto& right) {
        return left.m_cluster < right.m_cluster;
      });
    stops.push_back(ClusterStop(sources.size(), end));
    if(stops.front().m_cluster != 0) {
      stops.insert(stops.begin(), ClusterStop(0, start));
    }
    for(auto i = std::size_t(0); i + 1 < stops.size(); ++i) {
      auto& from = stops[i];
      auto& to = stops[i + 1];
      if(to.m_cluster <= from.m_cluster) {
        continue;
      }
      for(auto j = from.m_cluster; j != to.m_cluster && j < sources.size();
          ++j) {
        carets.set(sources[j], from.m_x + (to.m_x - from.m_x) *
          static_cast<double>(j - from.m_cluster) /
          static_cast<double>(to.m_cluster - from.m_cluster));
      }
    }
  }

  double place(const std::u16string& content, std::size_t start,
      std::size_t end, const TextStyle& style, const Font& font, Color color,
      Point pen, double origin, const FontLibrary& library,
      TextLayout* layout, CaretSink* carets = nullptr) {
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
    auto sources = std::vector<std::size_t>();
    auto flush = [&] {
      auto converted = apply_transform(display, style.m_transform);
      auto is_mapped = converted.size() == display.size();
      display = std::move(converted);
      auto flush_start = x;
      auto stops = std::vector<ClusterStop>();
      for(auto& item : itemize(display, resolution.m_typeface, library)) {
        auto& typeface = item.m_face.m_typeface;
        auto glyphs = typeface->shape(display, item.m_start, item.m_length);
        auto emboldening = 0.0;
        if(item.m_face.m_is_emboldened) {
          auto units = typeface->get_units_per_em();
          emboldening = std::round(units / EMBOLDENING_DIVISOR) / units;
        }
        for(auto i = std::size_t(0); i != glyphs.size(); ++i) {
          auto& glyph = glyphs[i];
          if(carets &&
              (i == 0 || glyphs[i - 1].m_cluster != glyph.m_cluster)) {
            stops.push_back(ClusterStop(glyph.m_cluster, x));
          }
          if(layout && (!typeface->get_outline(glyph.m_index).is_empty() ||
              !typeface->get_color_layers(glyph.m_index).empty())) {
            layout->m_glyphs.push_back(PlacedGlyph(typeface, glyph.m_index,
              Point(x + glyph.m_offset.m_x * size,
                baseline + glyph.m_offset.m_y * size), size, color));
          }
          x += glyph.m_advance * size;
          if(glyph.m_advance != 0) {
            x += emboldening * size;
          }
          if(i + 1 == glyphs.size() ||
              glyphs[i + 1].m_cluster != glyph.m_cluster) {
            x += spacing;
          }
        }
      }
      if(carets) {
        record(*carets, sources, stops, is_mapped, flush_start, x);
      }
      display.clear();
      sources.clear();
    };
    for(auto i = start; i != end; ++i) {
      if(content[i] == u'\t') {
        flush();
        if(carets) {
          carets->set(i, x);
        }
        auto interval = TAB_INTERVAL * font.m_size;
        x = origin + (std::floor((x - origin) / interval) + 1) * interval;
      } else if(!is_line_separator(content[i])) {
        display.push_back(content[i]);
        if(carets) {
          sources.push_back(i);
        }
      } else if(carets) {
        flush();
        carets->set(i, x);
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

  bool is_kerned(char16_t character) {
    return !QChar::isSurrogate(character) && character != u'\t' &&
      !is_line_separator(character);
  }

  double get_kerning(char16_t left, char16_t right, const TextStyle& style,
      const Font& font, const FontLibrary& library) {
    if(style.m_script != TextScript::NORMAL || !is_kerned(left) ||
        !is_kerned(right)) {
      return 0;
    }
    auto resolution = library.resolve(font);
    if(!resolution.m_typeface) {
      return 0;
    }
    auto& typeface = *resolution.m_typeface;
    auto pair =
      apply_transform(std::u16string({left, right}), style.m_transform);
    if(pair.size() != 2 || !typeface.has_glyph(pair[0]) ||
        !typeface.has_glyph(pair[1])) {
      return 0;
    }
    auto joined = typeface.shape(pair, 0, 2);
    auto single = typeface.shape(pair, 0, 1);
    if(joined.size() != 2 || single.size() != 1) {
      return 0;
    }
    return (joined[0].m_advance - single[0].m_advance) * font.m_size;
  }

  double place(const std::u16string& content, const Segment& segment,
      std::size_t end, const std::vector<Span>& spans, Point pen,
      double origin, const FontLibrary& library, TextLayout* layout,
      CaretSink* carets = nullptr) {
    auto x = pen.m_x;
    auto previous = static_cast<const TextStyle*>(nullptr);
    auto previous_font = static_cast<const Font*>(nullptr);
    auto previous_end = std::size_t(0);
    for(auto& span : spans) {
      auto from = std::max(segment.m_start, span.m_start);
      auto to = std::min({end, segment.m_end, span.m_end});
      if(from < to) {
        auto& style = *span.m_style;
        auto& font = segment.m_font ? *segment.m_font : style.m_font;
        auto color = segment.m_color ? *segment.m_color : style.m_color;
        if(previous && previous_end == from && *previous_font == font &&
            previous->m_letter_spacing == style.m_letter_spacing &&
            previous->m_transform == style.m_transform &&
            previous->m_script == style.m_script) {
          x += get_kerning(
            content[from - 1], content[from], style, font, library);
        }
        x += place(content, from, to, style, font, color, Point(x, pen.m_y),
          origin, library, layout, carets);
        previous = &style;
        previous_font = &font;
        previous_end = to;
      }
    }
    return x - pen.m_x;
  }

  double place(const std::u16string& content, const Line& line,
      std::size_t end, const std::vector<Span>& spans, Point pen,
      const FontLibrary& library, TextLayout* layout,
      CaretSink* carets = nullptr) {
    auto x = pen.m_x;
    for(auto& segment : line.m_segments) {
      x += place(content, segment, end, spans, Point(x, pen.m_y), pen.m_x,
        library, layout, carets);
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
    auto word_start = start;
    while(finder.toNextBoundary() != -1) {
      auto word_end = start + static_cast<std::size_t>(finder.position());
      if(word_end <= word_start) {
        continue;
      }
      if(word_start != line_start && measure(content, line_start,
          trim_end(content, line_start, word_end), spans, library) >
            *width + 1e-6) {
        lines.emplace_back(line_start, word_start);
        line_start = word_start;
      }
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
    auto metrics = LineMetrics(0, 0, 0, 0);
    auto add_face = [&] (const Typeface& typeface, double size) {
      auto& face = typeface.get_metrics();
      auto round = [&] (double value) {
        return std::floor(value * size + 0.5);
      };
      metrics.m_ascent = std::max(metrics.m_ascent, round(face.m_line_ascent));
      metrics.m_descent =
        std::max(metrics.m_descent, round(face.m_line_descent));
      metrics.m_clipping_ascent =
        std::max(metrics.m_clipping_ascent, round(face.m_clipping_ascent));
      metrics.m_clipping_descent =
        std::max(metrics.m_clipping_descent, round(face.m_clipping_descent));
    };
    auto add = [&] (const Font& font, std::size_t start, std::size_t end) {
      auto resolution = library.resolve(font);
      if(!resolution.m_typeface) {
        return;
      }
      add_face(*resolution.m_typeface, font.m_size);
      auto content = std::u16string_view(*text.m_content).substr(
        start, end - start);
      for(auto& item :
          itemize(std::u16string(content), resolution.m_typeface, library)) {
        if(item.m_face.m_typeface != resolution.m_typeface) {
          add_face(*item.m_face.m_typeface, font.m_size);
        }
      }
    };
    auto is_found = false;
    for(auto& segment : line.m_segments) {
      for(auto& span : spans) {
        auto start = std::max(segment.m_start, span.m_start);
        auto end = std::min(segment.m_end, span.m_end);
        if(start < end) {
          add(segment.m_font ? *segment.m_font : span.m_style->m_font, start,
            end);
          is_found = true;
        }
      }
    }
    if(!is_found && !line.m_segments.empty()) {
      auto& segment = line.m_segments.front();
      add(segment.m_font ? *segment.m_font :
        find_style(spans, text, segment.m_start).m_font, 0, 0);
    }
    return metrics;
  }

  void visit_lines(const Text& text, const std::vector<Line>& lines,
      const std::vector<Span>& spans, const FontLibrary& library,
      auto visit) {
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
      visit(line, Point(x, baseline), metrics);
    }
  }

  void place_lines(const Text& text, const std::vector<Line>& lines,
      const std::vector<Span>& spans, const FontLibrary& library,
      TextLayout& layout) {
    auto& content = *text.m_content;
    visit_lines(text, lines, spans, library,
      [&] (const Line& line, Point origin, const LineMetrics&) {
        place(content, line, content.size(), spans, origin, library, &layout);
      });
  }

  std::vector<Line> get_lines(const Text& text,
      const std::vector<Span>& spans, const FontLibrary& library) {
    if(text.m_layout) {
      auto lines = read_lines(**text.m_layout, text.m_content->size());
      if(!lines.empty() || text.m_content->empty()) {
        return lines;
      }
    }
    return break_lines(text, spans, library);
  }

  std::vector<Line> get_all_lines(const Text& text,
      const std::vector<Span>& spans, const FontLibrary& library) {
    auto lines = get_lines(text, spans, library);
    if(!text.m_layout) {
      return lines;
    }
    auto& content = *text.m_content;
    auto end = std::size_t(0);
    for(auto& line : lines) {
      for(auto& segment : line.m_segments) {
        end = std::max(end, segment.m_end);
      }
    }
    for(auto i = end; i < content.size(); ++i) {
      if(!is_line_separator(content[i])) {
        return break_lines(text, spans, library);
      }
    }
    return lines;
  }

  std::vector<Line> fit_lines(const Text& text, std::vector<Line> lines,
      const std::vector<Span>& spans, const FontLibrary& library) {
    auto area = std::get_if<AreaFrame>(&text.m_frame);
    if(!area || lines.size() < 2) {
      return lines;
    }
    auto count = std::size_t(0);
    auto is_full = false;
    auto top = 0.0;
    auto first = 0.0;
    visit_lines(text, lines, spans, library,
      [&] (const Line&, Point origin, const LineMetrics& metrics) {
        if(is_full) {
          return;
        } else if(count == 0) {
          top = metrics.m_clipping_ascent;
          first = origin.m_y;
        } else if(top + origin.m_y - first + metrics.m_clipping_descent >
            area->m_size.m_height) {
          is_full = true;
          return;
        }
        ++count;
      });
    lines.resize(count);
    return lines;
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
  return lay_out(text, library, false);
}

TextLayout Rellu::lay_out(
    const Text& text, const FontLibrary& library, bool is_edited) {
  auto layout = TextLayout();
  auto spans = make_spans(text);
  auto lines = std::vector<Line>();
  if(is_edited) {
    lines = get_all_lines(text, spans, library);
  } else {
    lines = get_lines(text, spans, library);
    if(!text.m_layout) {
      lines = fit_lines(text, std::move(lines), spans, library);
    }
  }
  place_lines(text, lines, spans, library, layout);
  return layout;
}

bool Rellu::is_overflowing(const Text& text, const FontLibrary& library) {
  if(!std::holds_alternative<AreaFrame>(text.m_frame)) {
    return false;
  }
  auto spans = make_spans(text);
  auto lines = get_all_lines(text, spans, library);
  auto count = lines.size();
  return fit_lines(text, std::move(lines), spans, library).size() != count;
}

Rect Rellu::measure_text(const Text& text, const FontLibrary& library) {
  if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
    return Rect(0, 0, area->m_size.m_width, area->m_size.m_height);
  }
  auto& content = *text.m_content;
  auto spans = make_spans(text);
  auto bounds = std::optional<Rect>();
  visit_lines(text, get_lines(text, spans, library), spans, library,
    [&] (const Line& line, Point origin, const LineMetrics& metrics) {
      auto box = Rect(origin.m_x, origin.m_y - metrics.m_ascent,
        measure_width(content, line, spans, library),
        metrics.m_ascent + metrics.m_descent);
      if(bounds) {
        bounds = unite(*bounds, box);
      } else {
        bounds = box;
      }
    });
  if(!bounds) {
    bounds = Rect(0, 0, 0, 0);
  }
  auto left = std::round(bounds->get_left());
  auto top = std::round(bounds->get_top());
  auto right = std::round(bounds->get_right());
  auto bottom = std::round(bounds->get_bottom());
  if(auto frame = std::get_if<AutoHeightFrame>(&text.m_frame)) {
    return Rect(0, 0, frame->m_width, std::max(bottom, 0.0));
  }
  return Rect(left, top, right - left, bottom - top);
}

StoredLayout Rellu::store_layout(const Text& text, const FontLibrary& library) {
  if(text.m_layout) {
    return **text.m_layout;
  }
  auto& content = *text.m_content;
  auto spans = make_spans(text);
  auto layout = StoredLayout();
  auto is_paragraph_start = true;
  visit_lines(text, fit_lines(text, break_lines(text, spans, library), spans,
      library), spans, library,
    [&] (const Line& line, Point origin, const LineMetrics&) {
      if(is_paragraph_start) {
        layout.m_paragraphs.emplace_back();
      }
      auto& runs = layout.m_paragraphs.back().m_lines.emplace_back().m_runs;
      auto x = origin.m_x;
      for(auto& segment : line.m_segments) {
        auto is_placed = false;
        for(auto& span : spans) {
          auto from = std::max(segment.m_start, span.m_start);
          auto to = std::min(segment.m_end, span.m_end);
          if(from < to) {
            runs.push_back(StoredRun(from, to, Point(x, origin.m_y)));
            auto& style = *span.m_style;
            x += place(content, from, to, style, style.m_font, style.m_color,
              Point(x, origin.m_y), origin.m_x, library, nullptr);
            is_placed = true;
          }
        }
        if(!is_placed) {
          runs.push_back(StoredRun(
            segment.m_start, segment.m_end, Point(x, origin.m_y)));
        }
      }
      is_paragraph_start = line.m_is_paragraph_end;
    });
  if(auto& paragraphs = layout.m_paragraphs; !paragraphs.empty() &&
      paragraphs.back().m_lines.size() == 1 &&
      paragraphs.back().m_lines.front().m_runs.size() == 1) {
    auto& run = paragraphs.back().m_lines.front().m_runs.front();
    if(run.m_from == content.size() && run.m_to == content.size()) {
      run.m_from = 0;
      run.m_to = 0;
    }
  }
  return layout;
}

std::vector<LineGeometry> Rellu::measure_lines(
    const Text& text, const FontLibrary& library) {
  auto& content = *text.m_content;
  auto spans = make_spans(text);
  auto lines = std::vector<LineGeometry>();
  visit_lines(text, get_all_lines(text, spans, library), spans, library,
    [&] (const Line& line, Point origin, const LineMetrics& metrics) {
      auto start = std::size_t(0);
      if(!lines.empty()) {
        start = lines.back().m_end;
        if(lines.back().m_is_paragraph_end) {
          start = skip_line_separator(content, start);
        }
      }
      auto end = start;
      if(!line.m_segments.empty()) {
        auto from = std::min(line.m_segments.front().m_start, content.size());
        auto to =
          std::clamp(line.m_segments.back().m_end, from, content.size());
        if(from != to || lines.empty() || from >= start) {
          start = from;
          end = to;
        }
      }
      while(end > start && is_line_separator(content[end - 1])) {
        --end;
      }
      auto carets = CaretSink(start, std::vector<double>(end - start + 1,
        std::numeric_limits<double>::quiet_NaN()));
      auto width = place(content, line, end, spans, origin, library, nullptr,
        &carets);
      carets.set(end, origin.m_x + width);
      auto previous = origin.m_x;
      for(auto& position : carets.m_positions) {
        if(std::isnan(position)) {
          position = previous;
        }
        previous = position;
      }
      lines.push_back(LineGeometry(start, end, line.m_is_paragraph_end,
        origin.m_y, metrics.m_ascent, metrics.m_descent,
        std::move(carets.m_positions)));
    });
  if(lines.empty()) {
    auto relaid = text;
    relaid.m_layout = std::nullopt;
    return measure_lines(relaid, library);
  }
  return lines;
}

std::vector<std::size_t> Rellu::find_wraps(
    const Text& text, const FontLibrary& library) {
  auto wraps = std::vector<std::size_t>();
  auto lines = measure_lines(text, library);
  for(auto i = std::size_t(1); i < lines.size(); ++i) {
    if(!lines[i - 1].m_is_paragraph_end) {
      wraps.push_back(lines[i].m_start);
    }
  }
  return wraps;
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
    auto transform = Matrix(glyph.m_size, 0, 0, glyph.m_size,
      glyph.m_origin.m_x, glyph.m_origin.m_y);
    auto& layers = glyph.m_typeface->get_color_layers(glyph.m_index);
    if(layers.empty()) {
      append(get_fill(0, glyph.m_color).m_path,
        glyph.m_typeface->get_outline(glyph.m_index), transform);
      continue;
    }
    for(auto& layer : layers) {
      auto color = glyph.m_color;
      if(layer.m_color) {
        color = *layer.m_color;
        color.m_alpha *= glyph.m_color.m_alpha;
      }
      if(fills.empty() || fills.back().m_color != color) {
        fills.emplace_back(Path(), color);
      }
      append(fills.back().m_path,
        glyph.m_typeface->get_outline(layer.m_index), transform);
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

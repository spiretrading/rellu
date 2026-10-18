#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  std::u16string to_u16string(const QString& text) {
    return std::u16string(
      reinterpret_cast<const char16_t*>(text.utf16()),
      static_cast<std::size_t>(text.size()));
  }

  std::optional<TextFrame> read_frame(XdObject& frame) {
    auto type = frame.get_json().value("type"_L1).toString();
    if(type == "positioned"_L1) {
      frame.consume("type"_L1);
      return PositionedFrame();
    } else if(type == "area"_L1) {
      auto width = frame.get_number("width"_L1);
      auto height = frame.get_number("height"_L1);
      if(!width || !height) {
        return std::nullopt;
      }
      frame.consume("type"_L1);
      return AreaFrame(Size(*width, *height));
    } else if(type == "autoHeight"_L1) {
      auto width = frame.get_number("width"_L1);
      if(!width) {
        return std::nullopt;
      }
      frame.consume("type"_L1);
      return AutoHeightFrame(*width);
    }
    return std::nullopt;
  }

  Font read_font(XdObject& font) {
    auto result = Font();
    if(auto family = font.get_string("family"_L1)) {
      result.m_family = to_std_string(*family);
    }
    if(auto style = font.get_string("style"_L1)) {
      result.m_style = to_std_string(*style);
    }
    if(auto name = font.get_string("postscriptName"_L1)) {
      result.m_postscript_name = to_std_string(*name);
    }
    result.m_size = font.get_number("size"_L1, result.m_size);
    return result;
  }

  std::optional<Color> read_solid_color(XdObject& fill) {
    if(fill.get_json().value("type"_L1).toString() != "solid"_L1) {
      return std::nullopt;
    }
    auto color = fill.get_object("color"_L1);
    if(!color) {
      return std::nullopt;
    }
    auto result = read_color(*color);
    if(result) {
      fill.consume("type"_L1);
    }
    return result;
  }

  void read_paragraph_style(XdObject& attributes, TextStyle& base,
      ParagraphStyle& style) {
    auto& json = attributes.get_json();
    auto alignment = json.value("paragraphAlign"_L1).toString();
    if(alignment == "left"_L1) {
      attributes.consume("paragraphAlign"_L1);
    } else if(alignment == "center"_L1) {
      style.m_alignment = TextAlignment::CENTER;
      attributes.consume("paragraphAlign"_L1);
    } else if(alignment == "right"_L1) {
      style.m_alignment = TextAlignment::RIGHT;
      attributes.consume("paragraphAlign"_L1);
    }
    if(auto height = attributes.get_number("lineHeight"_L1)) {
      if(*height > 0) {
        style.m_line_height = *height;
      }
    }
    style.m_paragraph_spacing =
      attributes.get_number("paragraphAfterSpacing"_L1, 0);
    base.m_letter_spacing =
      attributes.get_number("letterSpacing"_L1, base.m_letter_spacing);
  }

  TextRun read_range(XdObject& range, const TextStyle& base,
      XdImport& import) {
    auto run = TextRun();
    auto& style = run.m_style;
    style = base;
    if(auto family = range.get_string("fontFamily"_L1)) {
      style.m_font.m_family = to_std_string(*family);
    }
    if(auto font_style = range.get_string("fontStyle"_L1)) {
      style.m_font.m_style = to_std_string(*font_style);
    }
    if(auto name = range.get_string("postscriptName"_L1)) {
      style.m_font.m_postscript_name = to_std_string(*name);
    } else if(style.m_font.m_family != base.m_font.m_family ||
        style.m_font.m_style != base.m_font.m_style) {
      style.m_font.m_postscript_name.clear();
    }
    style.m_font.m_size = range.get_number("fontSize"_L1, base.m_font.m_size);
    style.m_letter_spacing =
      range.get_number("charSpacing"_L1, base.m_letter_spacing);
    if(auto fill = range.get_object("fill"_L1)) {
      auto& json = fill->get_json();
      if(json.value("value"_L1).isDouble()) {
        if(auto color = to_argb_color(json.value("value"_L1).toDouble())) {
          style.m_color = *color;
          fill->consume("value"_L1);
        }
      }
    }
    style.m_is_underlined = range.get_bool("underline"_L1, false);
    style.m_is_struck_through = range.get_bool("strikethrough"_L1, false);
    auto& json = range.get_json();
    if(auto transform =
        to_text_transform(json.value("textTransform"_L1).toString())) {
      style.m_transform = *transform;
      range.consume("textTransform"_L1);
    }
    if(auto script = to_text_script(json.value("textScript"_L1).toString())) {
      style.m_script = *script;
      range.consume("textScript"_L1);
    }
    if(auto link = range.get_string("linkId"_L1); link && !link->isEmpty()) {
      auto id = to_node_id(*link);
      if(auto ids = import.get_states().find<XdResourceIds>()) {
        if(ids->m_character_styles.contains(id)) {
          run.m_character_style = id;
        }
      }
    }
    if(json.value("hyperlinkUrl"_L1).toString().isEmpty()) {
      range.consume("hyperlinkUrl"_L1);
    }
    return run;
  }

  std::vector<TextRun> read_runs(XdArray& ranges, std::size_t length,
      const TextStyle& base, XdImport& import) {
    auto runs = std::vector<TextRun>();
    runs.reserve(ranges.get_size());
    auto remaining = length;
    for(auto i = std::size_t(0); i != ranges.get_size(); ++i) {
      auto range = ranges.get_object(i);
      if(!range) {
        continue;
      }
      auto run = read_range(*range, base, import);
      auto count = static_cast<std::size_t>(
        std::max(0.0, range->get_number("length"_L1, 0)));
      if(i + 1 == ranges.get_size()) {
        count = remaining;
      } else {
        count = std::min(count, remaining);
      }
      if(count != 0) {
        run.m_length = count;
        remaining -= count;
        runs.push_back(std::move(run));
      }
    }
    if(remaining != 0 && !runs.empty()) {
      runs.back().m_length += remaining;
    }
    return runs;
  }

  const TextStyle& find_style(const std::vector<TextRun>& runs,
      const TextStyle& base, std::size_t index) {
    auto start = std::size_t(0);
    for(auto& run : runs) {
      if(index < start + run.m_length) {
        return run.m_style;
      }
      start += run.m_length;
    }
    if(!runs.empty()) {
      return runs.back().m_style;
    }
    return base;
  }

  void read_overrides(XdObject& style, const TextStyle& covering,
      StoredRun& run) {
    if(auto font = style.get_object("font"_L1)) {
      font->consume_all();
    }
    if(auto fill = style.get_object("fill"_L1)) {
      if(auto color = read_solid_color(*fill)) {
        if(*color != covering.m_color) {
          run.m_color = *color;
        }
      }
    }
    if(auto attributes = style.get_object("textAttributes"_L1)) {
      auto decoration = attributes->get_json().value("decoration"_L1);
      if(decoration.isArray()) {
        auto is_underlined = false;
        auto is_struck_through = false;
        auto is_known = true;
        for(auto value : decoration.toArray()) {
          auto name = value.toString();
          if(name == "underline"_L1) {
            is_underlined = true;
          } else if(name == "line-through"_L1) {
            is_struck_through = true;
          } else {
            is_known = false;
          }
        }
        if(is_known && is_underlined == covering.m_is_underlined &&
            is_struck_through == covering.m_is_struck_through) {
          attributes->consume("decoration"_L1);
        }
      }
    }
  }

  StoredLayout read_layout(XdArray& paragraphs,
      const std::vector<TextRun>& runs, const TextStyle& base) {
    auto layout = StoredLayout();
    layout.m_paragraphs.reserve(paragraphs.get_size());
    for(auto i = std::size_t(0); i != paragraphs.get_size(); ++i) {
      auto paragraph_json = paragraphs.get_object(i);
      if(!paragraph_json) {
        continue;
      }
      auto& paragraph = layout.m_paragraphs.emplace_back();
      auto lines = paragraph_json->get_array("lines"_L1);
      if(!lines) {
        continue;
      }
      paragraph.m_lines.reserve(lines->get_size());
      for(auto j = std::size_t(0); j != lines->get_size(); ++j) {
        auto line_json = lines->get_array(j);
        if(!line_json) {
          continue;
        }
        auto& line = paragraph.m_lines.emplace_back();
        for(auto k = std::size_t(0); k != line_json->get_size(); ++k) {
          auto run_json = line_json->get_object(k);
          if(!run_json) {
            continue;
          }
          auto run = StoredRun();
          run.m_from = static_cast<std::size_t>(
            std::max(0.0, run_json->get_number("from"_L1, 0)));
          run.m_to = static_cast<std::size_t>(
            std::max(0.0, run_json->get_number("to"_L1, 0)));
          auto x = run_json->get_number("x"_L1);
          run.m_position =
            Point(x.value_or(0), run_json->get_number("y"_L1, 0));
          if(auto style = run_json->get_object("style"_L1)) {
            read_overrides(*style, find_style(runs, base, run.m_from), run);
          }
          if(!x && !line.m_runs.empty()) {
            auto& previous = line.m_runs.back();
            previous.m_to = std::max(previous.m_to, run.m_to);
            if(previous.m_font != run.m_font) {
              previous.m_font = std::nullopt;
            }
            if(previous.m_color != run.m_color) {
              previous.m_color = std::nullopt;
            }
          } else {
            line.m_runs.push_back(std::move(run));
          }
        }
      }
    }
    return layout;
  }
}

void Rellu::add_text_importer(XdImporter& importer) {
  importer.set_node_importer("text"_L1, [] (XdNode& node) {
    auto& json = *node.m_json;
    auto& import = *node.m_import;
    auto text_json = json.get_object("text"_L1);
    if(!text_json) {
      import.warn("Text without content");
      return;
    }
    auto text = Text();
    auto content = text_json->get_string("rawText"_L1).value_or(QString());
    auto length = static_cast<std::size_t>(content.size());
    text.m_content = Shared(to_u16string(content));
    if(auto frame_json = text_json->get_object("frame"_L1)) {
      if(auto frame = read_frame(*frame_json)) {
        text.m_frame = std::move(*frame);
      } else {
        import.warn("Unsupported text frame");
      }
    }
    if(auto style = json.get_object("style"_L1)) {
      if(auto font = style->get_object("font"_L1)) {
        text.m_base_style.m_font = read_font(*font);
      }
      if(auto fill = style->get_object("fill"_L1)) {
        if(auto color = read_solid_color(*fill)) {
          text.m_base_style.m_color = *color;
        }
      }
      if(auto attributes = style->get_object("textAttributes"_L1)) {
        read_paragraph_style(
          *attributes, text.m_base_style, text.m_paragraph_style);
      }
    }
    if(auto ux = get_ux(json)) {
      if(auto ranges = ux->get_array("rangedStyles"_L1)) {
        if(length != 0) {
          text.m_runs = read_runs(*ranges, length, text.m_base_style, import);
        } else {
          for(auto i = std::size_t(0); i != ranges->get_size(); ++i) {
            if(auto range = ranges->get_object(i)) {
              range->consume_all();
            }
          }
        }
      }
      ux->ignore({"singleTextObject"_L1, "repeatOverrides"_L1});
    }
    if(auto paragraphs = text_json->get_array("paragraphs"_L1)) {
      text.m_layout = Shared(
        read_layout(*paragraphs, text.m_runs, text.m_base_style));
    }
    node.m_payload = std::move(text);
  });
}

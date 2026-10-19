#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  bool is_high_surrogate(char16_t character) {
    return character >= 0xD800 && character <= 0xDBFF;
  }

  bool is_low_surrogate(char16_t character) {
    return character >= 0xDC00 && character <= 0xDFFF;
  }

  void append_utf8(std::string& output, char32_t code) {
    if(code < 0x80) {
      output.push_back(static_cast<char>(code));
    } else if(code < 0x800) {
      output.push_back(static_cast<char>(0xC0 | (code >> 6)));
      output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if(code < 0x10000) {
      output.push_back(static_cast<char>(0xE0 | (code >> 12)));
      output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      output.push_back(static_cast<char>(0xF0 | (code >> 18)));
      output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  std::vector<TextRun> normalize(std::vector<TextRun> runs) {
    auto result = std::vector<TextRun>();
    for(auto& run : runs) {
      if(run.m_length == 0) {
        continue;
      }
      if(!result.empty() && result.back().m_style == run.m_style &&
          result.back().m_character_style == run.m_character_style &&
          result.back().m_hyperlink == run.m_hyperlink) {
        result.back().m_length += run.m_length;
      } else {
        result.push_back(std::move(run));
      }
    }
    return result;
  }

  std::size_t find_run(const std::vector<TextRun>& runs, std::size_t index) {
    auto start = std::size_t(0);
    for(auto i = std::size_t(0); i != runs.size(); ++i) {
      if(runs[i].m_length != 0 && index < start + runs[i].m_length) {
        return i;
      }
      start += runs[i].m_length;
    }
    for(auto i = runs.size(); i != 0; --i) {
      if(runs[i - 1].m_length != 0) {
        return i - 1;
      }
    }
    return 0;
  }

  TextRange clamp(TextRange range, std::size_t size) {
    range.m_start = std::min(range.m_start, size);
    range.m_end = std::clamp(range.m_end, range.m_start, size);
    return range;
  }

  void apply(TextStyle& style, const TextStyleEdit& edit) {
    auto& font = style.m_font;
    if(edit.m_family || edit.m_style || edit.m_postscript_name) {
      if(edit.m_family) {
        font.m_family = *edit.m_family;
      }
      if(edit.m_style) {
        font.m_style = *edit.m_style;
      }
      font.m_postscript_name = edit.m_postscript_name.value_or(std::string());
    }
    if(edit.m_size) {
      font.m_size = *edit.m_size;
    }
    if(edit.m_letter_spacing) {
      style.m_letter_spacing = *edit.m_letter_spacing;
    }
    if(edit.m_color) {
      style.m_color = *edit.m_color;
    }
    if(edit.m_is_underlined) {
      style.m_is_underlined = *edit.m_is_underlined;
    }
    if(edit.m_is_struck_through) {
      style.m_is_struck_through = *edit.m_is_struck_through;
    }
    if(edit.m_transform) {
      style.m_transform = *edit.m_transform;
    }
    if(edit.m_script) {
      style.m_script = *edit.m_script;
    }
  }

  void combine(TextStyleEdit& target, const TextStyleEdit& source) {
    auto take = [] (auto& to, const auto& from) {
      if(from) {
        to = from;
      }
    };
    if(source.m_family || source.m_style || source.m_postscript_name) {
      take(target.m_family, source.m_family);
      take(target.m_style, source.m_style);
      target.m_postscript_name = source.m_postscript_name;
    }
    take(target.m_size, source.m_size);
    take(target.m_letter_spacing, source.m_letter_spacing);
    take(target.m_color, source.m_color);
    take(target.m_is_underlined, source.m_is_underlined);
    take(target.m_is_struck_through, source.m_is_struck_through);
    take(target.m_transform, source.m_transform);
    take(target.m_script, source.m_script);
    take(target.m_hyperlink, source.m_hyperlink);
  }

  std::vector<NodeId> get_texts(
      const Document& document, const std::vector<NodeId>& nodes) {
    auto visited = std::unordered_set<NodeId>();
    auto texts = std::vector<NodeId>();
    for(auto& id : nodes) {
      auto node = document.find(id);
      if(node && node->get_text() && visited.insert(id).second) {
        texts.push_back(id);
      }
    }
    return texts;
  }

  void relay(Text& text, const TextLayouter& layouter) {
    text.m_layout = std::nullopt;
    if(layouter) {
      text.m_layout = Shared(layouter(text));
    }
  }

  Text restyle(const Text& text, TextRange range,
      const TextStyleEditor& editor, bool& is_relaid) {
    auto size = text.m_content->size();
    range = clamp(range, size);
    auto result = text;
    auto is_whole = range.m_start == 0 && range.m_end == size;
    if(!is_whole && range.m_start == range.m_end) {
      return result;
    }
    auto is_affected = false;
    auto runs = std::vector<TextRun>();
    auto start = std::size_t(0);
    for(auto& run : get_runs(text)) {
      auto end = start + run.m_length;
      auto add = [&] (std::size_t from, std::size_t to, bool is_edited) {
        if(from >= to) {
          return;
        }
        auto& piece = runs.emplace_back(run);
        piece.m_length = to - from;
        if(is_edited) {
          auto edit = editor(piece.m_style);
          auto previous = piece.m_style;
          apply(piece.m_style, edit);
          is_affected |=
            is_layout_affected(edit) && !(previous == piece.m_style);
          if(edit.m_hyperlink) {
            piece.m_hyperlink = *edit.m_hyperlink;
          }
        }
      };
      add(start, std::min(end, range.m_start), false);
      add(std::max(start, range.m_start), std::min(end, range.m_end), true);
      add(std::max(start, range.m_end), end, false);
      start = end;
    }
    if(is_whole) {
      auto edit = editor(result.m_base_style);
      auto previous = result.m_base_style;
      apply(result.m_base_style, edit);
      is_affected |=
        is_layout_affected(edit) && !(previous == result.m_base_style);
      if(edit.m_color) {
        result.m_style.m_fill = SolidFill(*edit.m_color);
      }
    }
    if(size == 0 && result.m_runs.empty()) {
      return result;
    }
    result.m_runs = normalize(std::move(runs));
    if(text.m_runs.empty() && result.m_runs.size() == 1 &&
        result.m_runs.front().m_style == result.m_base_style &&
        result.m_runs.front().m_character_style.is_nil() &&
        result.m_runs.front().m_hyperlink.empty()) {
      result.m_runs.clear();
    }
    is_relaid = is_affected;
    return result;
  }
}

TextStyle Rellu::get_default_text_style() {
  auto style = TextStyle();
  style.m_font = Font("Segoe UI", "Regular", "SegoeUI", 20);
  style.m_color = make_color(112, 112, 112);
  return style;
}

std::string Rellu::make_text_name(std::u16string_view content) {
  auto length = std::min(content.size(), MAXIMUM_TEXT_NAME_LENGTH);
  if(length != 0 && length < content.size() &&
      is_high_surrogate(content[length - 1]) &&
      is_low_surrogate(content[length])) {
    --length;
  }
  auto name = std::string();
  name.reserve(length);
  for(auto i = std::size_t(0); i != length; ++i) {
    auto code = static_cast<char32_t>(content[i]);
    if(is_high_surrogate(content[i]) && i + 1 != length &&
        is_low_surrogate(content[i + 1])) {
      code = 0x10000 + ((code - 0xD800) << 10) +
        (static_cast<char32_t>(content[i + 1]) - 0xDC00);
      ++i;
    } else if(is_high_surrogate(content[i]) || is_low_surrogate(content[i])) {
      code = 0xFFFD;
    }
    append_utf8(name, code);
  }
  return name;
}

TextStyle Rellu::edit_style(TextStyle style, const TextStyleEdit& edit) {
  apply(style, edit);
  return style;
}

TextStyleEdit Rellu::make_style_edit(const TextStyle& style) {
  auto edit = TextStyleEdit();
  edit.m_family = style.m_font.m_family;
  edit.m_style = style.m_font.m_style;
  edit.m_postscript_name = style.m_font.m_postscript_name;
  edit.m_size = style.m_font.m_size;
  edit.m_letter_spacing = style.m_letter_spacing;
  edit.m_color = style.m_color;
  edit.m_is_underlined = style.m_is_underlined;
  edit.m_is_struck_through = style.m_is_struck_through;
  edit.m_transform = style.m_transform;
  edit.m_script = style.m_script;
  return edit;
}

std::vector<TextRun> Rellu::get_runs(const Text& text) {
  auto size = text.m_content->size();
  auto runs = std::vector<TextRun>();
  if(text.m_runs.empty()) {
    runs.push_back(TextRun(size, text.m_base_style, CharacterStyleId()));
    return runs;
  }
  auto start = std::size_t(0);
  for(auto& run : text.m_runs) {
    auto& added = runs.emplace_back(run);
    added.m_length = std::min(run.m_length, size - start);
    start += added.m_length;
  }
  runs.back().m_length += size - start;
  return runs;
}

TextRun Rellu::get_typing_run(const Text& text, std::size_t index) {
  auto size = text.m_content->size();
  if(size == 0) {
    if(!text.m_runs.empty()) {
      return text.m_runs.front();
    }
    return TextRun(0, text.m_base_style, CharacterStyleId());
  }
  auto runs = get_runs(text);
  auto target = std::min(index, size);
  if(target != 0) {
    --target;
  }
  return runs[find_run(runs, target)];
}

TextStyle Rellu::get_style(const Text& text, const TextRange& range) {
  auto clamped = clamp(range, text.m_content->size());
  if(clamped.m_start == clamped.m_end) {
    return get_typing_run(text, clamped.m_start).m_style;
  }
  auto runs = get_runs(text);
  return runs[find_run(runs, clamped.m_start)].m_style;
}

Text Rellu::replace_text(
    const Text& text, TextRange range, std::u16string_view replacement) {
  auto& content = *text.m_content;
  range = clamp(range, content.size());
  auto runs = get_runs(text);
  auto target = range.m_start;
  if(range.m_start == range.m_end && target != 0) {
    --target;
  }
  auto chosen = find_run(runs, std::min(target, content.size()));
  auto start = std::size_t(0);
  for(auto& run : runs) {
    auto end = start + run.m_length;
    auto from = std::max(start, range.m_start);
    auto to = std::min(end, range.m_end);
    if(from < to) {
      run.m_length -= to - from;
    }
    start = end;
  }
  runs[chosen].m_length += replacement.size();
  auto edited = std::u16string();
  edited.reserve(content.size() - (range.m_end - range.m_start) +
    replacement.size());
  edited.append(content, 0, range.m_start);
  edited.append(replacement);
  edited.append(content, range.m_end);
  auto result = text;
  if(edited.empty()) {
    result.m_base_style = runs[chosen].m_style;
    result.m_runs.clear();
  } else if(!text.m_runs.empty() || runs.size() != 1) {
    result.m_runs = normalize(std::move(runs));
  }
  result.m_content = Shared(std::move(edited));
  result.m_layout = std::nullopt;
  return result;
}

Text Rellu::edit_text_style(
    const Text& text, TextRange range, const TextStyleEdit& edit) {
  return edit_text_style(text, range, [&] (const TextStyle&) {
    return edit;
  });
}

Text Rellu::edit_text_style(
    const Text& text, TextRange range, const TextStyleEditor& editor) {
  auto is_relaid = false;
  auto result = restyle(text, range, editor, is_relaid);
  if(is_relaid) {
    result.m_layout = std::nullopt;
  }
  return result;
}

Text Rellu::edit_paragraph_style(
    const Text& text, const ParagraphStyleEdit& edit) {
  auto result = text;
  auto& style = result.m_paragraph_style;
  if(edit.m_alignment) {
    style.m_alignment = *edit.m_alignment;
  }
  if(edit.m_line_height) {
    style.m_line_height = *edit.m_line_height;
  }
  if(edit.m_paragraph_spacing) {
    style.m_paragraph_spacing = *edit.m_paragraph_spacing;
  }
  if(!(style == text.m_paragraph_style)) {
    result.m_layout = std::nullopt;
  }
  return result;
}

bool Rellu::is_layout_affected(const TextStyleEdit& edit) {
  return edit.m_family || edit.m_style || edit.m_postscript_name ||
    edit.m_size || edit.m_letter_spacing || edit.m_transform ||
    edit.m_script;
}

EditTextCommand::EditTextCommand(
  const NodeId& id, Text text, std::optional<std::string> name) noexcept
  : m_id(id),
    m_text(std::move(text)),
    m_name(std::move(name)) {}

EditTextCommand::EditTextCommand(TextInsertion insertion, Text text) noexcept
  : m_id(insertion.m_header.m_id),
    m_insertion(std::move(insertion)),
    m_text(std::move(text)) {}

EditTextCommand::EditTextCommand(const NodeId& id) noexcept
  : m_id(id) {}

const NodeId& EditTextCommand::get_node() const {
  return m_id;
}

std::string EditTextCommand::get_label() const {
  if(m_insertion) {
    if(m_text && !std::holds_alternative<PositionedFrame>(m_text->m_frame)) {
      return "Draw Text";
    }
    return "Create Auto Width Text";
  } else if(!m_text) {
    return "Delete";
  }
  return "Edit Text";
}

void EditTextCommand::apply(
    const Document& document, Transaction& transaction) {
  auto node = document.find(m_id);
  if(m_insertion && !node) {
    if(!m_text) {
      return;
    }
    auto header = m_insertion->m_header;
    header.m_id = m_id;
    if(m_name) {
      header.m_name = *m_name;
    }
    transaction.append(m_insertion->m_parent, std::move(header), *m_text);
    return;
  }
  if(!node || !node->get_text()) {
    throw std::invalid_argument("The node edited is not text.");
  }
  if(!m_text) {
    transaction.remove(m_id);
    return;
  }
  if(!(*node->get_text() == *m_text)) {
    transaction.replace_payload(m_id, *m_text);
  }
  if(m_name && *m_name != node->m_header.m_name) {
    auto header = node->m_header;
    header.m_name = *m_name;
    transaction.replace_header(std::move(header));
  }
}

bool EditTextCommand::merge(const Command& command) {
  auto edit = dynamic_cast<const EditTextCommand*>(&command);
  if(!edit || edit->m_id != m_id || edit->m_insertion) {
    return false;
  }
  m_text = edit->m_text;
  if(edit->m_name) {
    m_name = edit->m_name;
  }
  return true;
}

std::optional<std::vector<NodeId>> EditTextCommand::get_selection() const {
  if(m_insertion && m_text) {
    return std::vector{m_id};
  }
  return std::nullopt;
}

SetTextStyleCommand::SetTextStyleCommand(std::vector<NodeId> nodes,
  TextStyleEdit edit, TextLayouter layouter, std::string label) noexcept
  : m_nodes(std::move(nodes)),
    m_edit(std::move(edit)),
    m_layouter(std::move(layouter)),
    m_label(std::move(label)) {}

SetTextStyleCommand::SetTextStyleCommand(const NodeId& id, TextRange range,
  TextStyleEdit edit, TextLayouter layouter, std::string label) noexcept
  : m_nodes({id}),
    m_range(range),
    m_edit(std::move(edit)),
    m_layouter(std::move(layouter)),
    m_label(std::move(label)) {}

std::string SetTextStyleCommand::get_label() const {
  if(!m_label.empty()) {
    return m_label;
  } else if(m_edit.m_hyperlink) {
    return "Text Hyperlink";
  }
  auto labels = std::vector<std::string>();
  if(m_edit.m_family || m_edit.m_style || m_edit.m_postscript_name) {
    labels.push_back("Font Family");
  }
  if(m_edit.m_size) {
    labels.push_back("Font Size");
  }
  if(m_edit.m_letter_spacing) {
    labels.push_back("Character Spacing");
  }
  if(m_edit.m_color) {
    labels.push_back("Fill");
  }
  if(m_edit.m_is_underlined) {
    labels.push_back("Underline");
  }
  if(m_edit.m_is_struck_through) {
    labels.push_back("Strikethrough");
  }
  if(m_edit.m_transform) {
    labels.push_back("Text Transform");
  }
  if(m_edit.m_script) {
    labels.push_back("Script");
  }
  if(labels.size() == 1) {
    return labels.front();
  }
  return "Change Text Style";
}

void SetTextStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_texts(document, m_nodes)) {
    auto& text = *document.get(id).get_text();
    auto range = m_range.value_or(TextRange(0, text.m_content->size()));
    auto payload = edit_text_style(text, range, m_edit);
    if(is_layout_affected(m_edit)) {
      relay(payload, m_layouter);
    }
    if(!(payload == text)) {
      transaction.replace_payload(id, std::move(payload));
    }
  }
}

bool SetTextStyleCommand::merge(const Command& command) {
  auto style = dynamic_cast<const SetTextStyleCommand*>(&command);
  if(!style || style->m_nodes != m_nodes || style->m_range != m_range) {
    return false;
  }
  combine(m_edit, style->m_edit);
  return true;
}

RestyleTextCommand::RestyleTextCommand(std::vector<NodeId> nodes,
  std::string label, TextStyleEditor editor, TextLayouter layouter) noexcept
  : m_nodes(std::move(nodes)),
    m_label(std::move(label)),
    m_editor(std::move(editor)),
    m_layouter(std::move(layouter)) {}

RestyleTextCommand::RestyleTextCommand(const NodeId& id, TextRange range,
  std::string label, TextStyleEditor editor, TextLayouter layouter) noexcept
  : m_nodes({id}),
    m_range(range),
    m_label(std::move(label)),
    m_editor(std::move(editor)),
    m_layouter(std::move(layouter)) {}

std::string RestyleTextCommand::get_label() const {
  return m_label;
}

void RestyleTextCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_texts(document, m_nodes)) {
    auto& text = *document.get(id).get_text();
    auto range = m_range.value_or(TextRange(0, text.m_content->size()));
    auto is_relaid = false;
    auto payload = restyle(text, range, m_editor, is_relaid);
    if(is_relaid) {
      relay(payload, m_layouter);
    }
    if(!(payload == text)) {
      transaction.replace_payload(id, std::move(payload));
    }
  }
}

SetParagraphStyleCommand::SetParagraphStyleCommand(std::vector<NodeId> nodes,
  ParagraphStyleEdit edit, TextLayouter layouter) noexcept
  : m_nodes(std::move(nodes)),
    m_edit(std::move(edit)),
    m_layouter(std::move(layouter)) {}

std::string SetParagraphStyleCommand::get_label() const {
  auto count = 0;
  if(m_edit.m_alignment) {
    ++count;
  }
  if(m_edit.m_line_height) {
    ++count;
  }
  if(m_edit.m_paragraph_spacing) {
    ++count;
  }
  if(count == 1) {
    if(m_edit.m_alignment) {
      return "Text Align";
    } else if(m_edit.m_line_height) {
      return "Line Spacing";
    }
    return "Paragraph Spacing";
  }
  return "Change Paragraph Style";
}

void SetParagraphStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_texts(document, m_nodes)) {
    auto& text = *document.get(id).get_text();
    auto payload = edit_paragraph_style(text, m_edit);
    if(payload == text) {
      continue;
    }
    relay(payload, m_layouter);
    transaction.replace_payload(id, std::move(payload));
  }
}

bool SetParagraphStyleCommand::merge(const Command& command) {
  auto style = dynamic_cast<const SetParagraphStyleCommand*>(&command);
  if(!style || style->m_nodes != m_nodes) {
    return false;
  }
  if(style->m_edit.m_alignment) {
    m_edit.m_alignment = style->m_edit.m_alignment;
  }
  if(style->m_edit.m_line_height) {
    m_edit.m_line_height = style->m_edit.m_line_height;
  }
  if(style->m_edit.m_paragraph_spacing) {
    m_edit.m_paragraph_spacing = style->m_edit.m_paragraph_spacing;
  }
  return true;
}

SetTextFrameCommand::SetTextFrameCommand(std::vector<NodeId> nodes,
  TextFrameKind kind, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_kind(kind),
    m_measurer(std::move(measurer)) {}

std::string SetTextFrameCommand::get_label() const {
  if(m_kind == TextFrameKind::AUTO_WIDTH) {
    return "Convert To Auto Width";
  } else if(m_kind == TextFrameKind::AUTO_HEIGHT) {
    return "Convert To Auto Height";
  }
  return "Convert To Fixed Size";
}

void SetTextFrameCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_texts(document, m_nodes)) {
    auto& node = document.get(id);
    auto& text = *node.get_text();
    auto bounds = m_measurer(text);
    auto width = bounds.m_width;
    if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
      width = area->m_size.m_width;
    } else if(auto automatic = std::get_if<AutoHeightFrame>(&text.m_frame)) {
      width = automatic->m_width;
    }
    auto is_positioned = std::holds_alternative<PositionedFrame>(text.m_frame);
    auto payload = text;
    payload.m_layout = std::nullopt;
    auto offset = Point(0, 0);
    if(m_kind == TextFrameKind::AUTO_WIDTH) {
      if(is_positioned) {
        continue;
      }
      payload.m_frame = PositionedFrame();
      auto alignment = text.m_paragraph_style.m_alignment;
      if(alignment == TextAlignment::CENTER) {
        offset.m_x = width / 2;
      } else if(alignment == TextAlignment::RIGHT) {
        offset.m_x = width;
      }
      offset.m_y = -m_measurer(payload).m_y;
    } else if(m_kind == TextFrameKind::AUTO_HEIGHT) {
      if(std::holds_alternative<AutoHeightFrame>(text.m_frame)) {
        continue;
      }
      payload.m_frame = AutoHeightFrame(width);
      if(is_positioned) {
        offset = bounds.get_top_left();
      }
    } else {
      if(std::holds_alternative<AreaFrame>(text.m_frame)) {
        continue;
      }
      payload.m_frame = AreaFrame(Size(width, bounds.m_height));
      if(is_positioned) {
        offset = bounds.get_top_left();
      }
    }
    transaction.replace_payload(id, std::move(payload));
    if(offset != Point(0, 0)) {
      auto header = node.m_header;
      header.m_transform =
        header.m_transform * make_translation(offset.m_x, offset.m_y);
      transaction.replace_header(std::move(header));
    }
  }
}

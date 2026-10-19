#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include "Rellu/Text/TextLayout.hpp"
#include "Rellu/Ui/TextPropertyEditors.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_SIZE = 1.0;
  constexpr auto MAXIMUM_SIZE = 1000.0;
  constexpr auto MINIMUM_LETTER_SPACING = -299.0;
  constexpr auto MAXIMUM_LETTER_SPACING = 9999.0;

  std::optional<double> get_number(const PropertyValue& value) {
    if(auto number = std::get_if<double>(&value); number &&
        std::isfinite(*number)) {
      return *number;
    } else if(auto integer = std::get_if<int>(&value)) {
      return static_cast<double>(*integer);
    }
    return std::nullopt;
  }

  std::optional<int> get_index(const PropertyValue& value, int count) {
    auto choice = std::get_if<Choice>(&value);
    if(!choice || choice->m_index < 0 || choice->m_index >= count) {
      return std::nullopt;
    }
    return choice->m_index;
  }

  bool is_edited(const Document& document, const std::vector<NodeId>& nodes,
      const TextSelection* selection) {
    if(!selection || nodes.size() != 1 || !selection->is_editing() ||
        selection->get_node() != nodes.front()) {
      return false;
    }
    auto node = document.find(nodes.front());
    return node && node->get_text();
  }

  TextSelection* find_selection(const TextSelectionSource& source) {
    if(!source) {
      return nullptr;
    }
    return source();
  }

  const Text* find_first_text(
      const Document& document, const std::vector<NodeId>& nodes) {
    for(auto& id : nodes) {
      if(auto node = document.find(id); node && node->get_text()) {
        return node->get_text();
      }
    }
    return nullptr;
  }

  void remember(const Document& document, const std::vector<NodeId>& nodes,
      TextSelection* selection, const TextStyleEdit& edit) {
    if(!selection || edit.m_hyperlink) {
      return;
    }
    auto text = find_first_text(document, nodes);
    auto style = find_text_style(document, nodes, selection);
    if(text && style) {
      selection->set_last_style(
        edit_style(*style, edit), text->m_paragraph_style);
    }
  }

  TextRange get_range(const Text& text, const TextSelection& selection) {
    auto size = text.m_content->size();
    auto range = selection.get_range();
    return TextRange(
      std::min(range.m_start, size), std::min(range.m_end, size));
  }

  bool is_caret(const Text& text, const TextSelection& selection) {
    auto range = get_range(text, selection);
    return range.m_start == range.m_end && !text.m_content->empty();
  }

  bool is_found(FontMatch match) {
    return match == FontMatch::POSTSCRIPT_NAME ||
      match == FontMatch::FAMILY_AND_STYLE || match == FontMatch::FAMILY;
  }

  TextStyleEdit make_font_edit(const FontLibrary& library, Font font) {
    auto edit = TextStyleEdit();
    font.m_postscript_name.clear();
    auto resolution = library.resolve(font);
    if(resolution.m_typeface && is_found(resolution.m_match)) {
      auto& names = resolution.m_typeface->get_names();
      edit.m_family = names.m_typographic_family;
      edit.m_style = names.m_typographic_style;
      edit.m_postscript_name = names.m_postscript_name;
    } else {
      edit.m_family = font.m_family;
      edit.m_style = font.m_style;
    }
    return edit;
  }

  PropertySetter make_style_setter(TextSelectionSource source,
      TextLayouter layouter, auto make_edit, std::string label = {}) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto selection = find_selection(source);
      auto edit = make_edit(document, nodes, selection, value);
      if(!edit) {
        return nullptr;
      }
      return make_text_style_command(
        document, nodes, selection, std::move(*edit), layouter, label);
    };
  }

  PropertySetter make_simple_style_setter(TextSelectionSource source,
      TextLayouter layouter, auto make_edit) {
    return make_style_setter(std::move(source), std::move(layouter),
      [=] (const Document&, const std::vector<NodeId>&,
          const TextSelection*, const PropertyValue& value) {
        return make_edit(value);
      });
  }

  PropertySetter make_paragraph_setter(TextSelectionSource source,
      TextLayouter layouter, auto make_edit) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      if(nodes.empty()) {
        return nullptr;
      }
      auto edit = make_edit(value);
      if(!edit) {
        return nullptr;
      }
      if(auto selection = find_selection(source)) {
        if(auto text = find_first_text(document, nodes)) {
          selection->set_last_style(
            *find_text_style(document, nodes, selection),
            edit_paragraph_style(*text, *edit).m_paragraph_style);
        }
      }
      return std::make_unique<SetParagraphStyleCommand>(
        nodes, std::move(*edit), layouter);
    };
  }

  TextMeasurer make_frame_measurer(
      const FontLibrary& library, TextMeasurer measurer) {
    return [library = &library, measurer = std::move(measurer)] (
        const Text& text) {
      auto bounds = measurer(text);
      if(!std::holds_alternative<PositionedFrame>(text.m_frame)) {
        return bounds;
      }
      auto left = std::numeric_limits<double>::infinity();
      auto right = -std::numeric_limits<double>::infinity();
      for(auto& line : measure_lines(text, *library)) {
        left = std::min(left, line.m_carets.front());
        right = std::max(right, line.m_carets.back());
      }
      bounds.m_width = std::max(bounds.m_width, std::ceil(right - left));
      return bounds;
    };
  }

  PropertyEditor make_editor(PropertySetter setter,
      std::optional<double> minimum = std::nullopt,
      std::optional<double> maximum = std::nullopt) {
    auto editor = PropertyEditor();
    editor.m_setter = std::move(setter);
    editor.m_minimum = minimum;
    editor.m_maximum = maximum;
    return editor;
  }
}

void Rellu::add_text_property_editors(PropertyEditors& editors,
    const FontLibrary& library, TextMeasurer measurer,
    TextSelectionSource selection) {
  auto layouter = make_text_layouter(library);
  editors.add("text.family", make_editor(make_style_setter(selection,
    layouter, [library = &library] (const Document& document,
        const std::vector<NodeId>& nodes, const TextSelection* selection,
        const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto family = std::get_if<QString>(&value);
      auto style = find_text_style(document, nodes, selection);
      if(!family || family->trimmed().isEmpty() || !style) {
        return std::nullopt;
      }
      auto font = style->m_font;
      font.m_family = family->trimmed().toStdString();
      return make_font_edit(*library, std::move(font));
    }, "Font Family")));
  editors.add("text.style", make_editor(make_style_setter(selection,
    layouter, [library = &library] (const Document& document,
        const std::vector<NodeId>& nodes, const TextSelection* selection,
        const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto name = std::get_if<QString>(&value);
      auto style = find_text_style(document, nodes, selection);
      if(!name || name->trimmed().isEmpty() || !style) {
        return std::nullopt;
      }
      auto font = style->m_font;
      font.m_style = name->trimmed().toStdString();
      return make_font_edit(*library, std::move(font));
    }, "Font Style")));
  editors.add("text.size", make_editor(make_simple_style_setter(selection,
    layouter, [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto size = get_number(value);
      if(!size || !(*size > 0)) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_size = std::min(*size, MAXIMUM_SIZE);
      return edit;
    }), MINIMUM_SIZE, MAXIMUM_SIZE));
  editors.add("text.letter_spacing", make_editor(make_simple_style_setter(
    selection, layouter,
    [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto spacing = get_number(value);
      if(!spacing) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_letter_spacing = std::clamp(
        std::round(*spacing), MINIMUM_LETTER_SPACING, MAXIMUM_LETTER_SPACING);
      return edit;
    }), MINIMUM_LETTER_SPACING, MAXIMUM_LETTER_SPACING));
  editors.add("text.color", make_editor(make_simple_style_setter(selection,
    layouter, [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto color = std::get_if<Color>(&value);
      if(!color) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_color = *color;
      return edit;
    })));
  editors.add("text.underline", make_editor(make_simple_style_setter(
    selection, layouter,
    [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto is_set = std::get_if<bool>(&value);
      if(!is_set) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_is_underlined = *is_set;
      return edit;
    })));
  editors.add("text.strikethrough", make_editor(make_simple_style_setter(
    selection, layouter,
    [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto is_set = std::get_if<bool>(&value);
      if(!is_set) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_is_struck_through = *is_set;
      return edit;
    })));
  editors.add("text.transform", make_editor(make_simple_style_setter(
    selection, layouter,
    [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto index = get_index(value, 4);
      if(!index) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_transform = static_cast<TextTransform>(*index);
      return edit;
    })));
  editors.add("text.script", make_editor(make_simple_style_setter(selection,
    layouter, [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto index = get_index(value, 3);
      if(!index) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_script = static_cast<TextScript>(*index);
      return edit;
    })));
  editors.add("text.hyperlink", make_editor(make_simple_style_setter(
    selection, layouter,
    [] (const PropertyValue& value) -> std::optional<TextStyleEdit> {
      auto url = std::get_if<QString>(&value);
      if(!url) {
        return std::nullopt;
      }
      auto edit = TextStyleEdit();
      edit.m_hyperlink = url->toStdString();
      edit.m_is_underlined = !url->isEmpty();
      return edit;
    })));
  editors.add("text.alignment", make_editor(make_paragraph_setter(selection,
    layouter,
    [] (const PropertyValue& value) -> std::optional<ParagraphStyleEdit> {
      auto index = get_index(value, 3);
      if(!index) {
        return std::nullopt;
      }
      auto edit = ParagraphStyleEdit();
      edit.m_alignment = static_cast<TextAlignment>(*index);
      return edit;
    })));
  editors.add("text.line_height", make_editor(make_paragraph_setter(selection,
    layouter,
    [] (const PropertyValue& value) -> std::optional<ParagraphStyleEdit> {
      auto height = get_number(value);
      if(!height) {
        return std::nullopt;
      }
      auto edit = ParagraphStyleEdit();
      if(*height > 0) {
        edit.m_line_height = std::optional<double>(*height);
      } else {
        edit.m_line_height = std::optional<double>();
      }
      return edit;
    }), 0.0));
  editors.add("text.paragraph_spacing", make_editor(make_paragraph_setter(
    selection, layouter,
    [] (const PropertyValue& value) -> std::optional<ParagraphStyleEdit> {
      auto spacing = get_number(value);
      if(!spacing || *spacing < 0) {
        return std::nullopt;
      }
      auto edit = ParagraphStyleEdit();
      edit.m_paragraph_spacing = *spacing;
      return edit;
    }), 0.0));
  editors.add("text.frame", make_editor(
    [measurer = make_frame_measurer(library, std::move(measurer))] (
        const Document& document,
        const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto index = get_index(value, 3);
      if(!index || nodes.empty()) {
        return nullptr;
      }
      auto kind = TextFrameKind::AUTO_WIDTH;
      if(*index == 1) {
        kind = TextFrameKind::AUTO_HEIGHT;
      } else if(*index == 2) {
        kind = TextFrameKind::FIXED_SIZE;
      }
      return std::make_unique<SetTextFrameCommand>(nodes, kind, measurer);
    }));
}

std::optional<TextStyle> Rellu::find_text_style(const Document& document,
    const std::vector<NodeId>& nodes, const TextSelection* selection) {
  if(!is_edited(document, nodes, selection)) {
    selection = nullptr;
  }
  for(auto& id : nodes) {
    if(auto node = document.find(id); node && node->get_text()) {
      auto& text = *node->get_text();
      if(!selection) {
        return get_style(text, TextRange(0, text.m_content->size()));
      }
      if(auto& pending = selection->get_pending_style();
          pending && is_caret(text, *selection)) {
        return *pending;
      }
      return get_style(text, get_range(text, *selection));
    }
  }
  return std::nullopt;
}

std::unique_ptr<Command> Rellu::make_text_style_command(
    const Document& document, const std::vector<NodeId>& nodes,
    TextSelection* selection, TextStyleEdit edit, TextLayouter layouter,
    std::string label) {
  if(nodes.empty()) {
    return nullptr;
  }
  remember(document, nodes, selection, edit);
  if(is_edited(document, nodes, selection)) {
    auto& text = *document.get(nodes.front()).get_text();
    if(is_caret(text, *selection)) {
      selection->set_pending_style(edit_style(
        *find_text_style(document, nodes, selection), edit));
      if(edit.m_hyperlink) {
        selection->set_pending_hyperlink(*edit.m_hyperlink);
      }
      return nullptr;
    }
    auto range = get_range(text, *selection);
    if(range.m_start != range.m_end) {
      return std::make_unique<SetTextStyleCommand>(nodes.front(), range,
        std::move(edit), std::move(layouter), std::move(label));
    }
  }
  return std::make_unique<SetTextStyleCommand>(
    nodes, std::move(edit), std::move(layouter), std::move(label));
}

std::unique_ptr<Command> Rellu::make_text_style_command(
    const Document& document, const std::vector<NodeId>& nodes,
    TextSelection* selection, std::string label, TextStyleEditor editor,
    TextLayouter layouter) {
  if(nodes.empty()) {
    return nullptr;
  }
  if(auto style = find_text_style(document, nodes, selection)) {
    remember(document, nodes, selection, editor(*style));
  }
  if(is_edited(document, nodes, selection)) {
    auto& text = *document.get(nodes.front()).get_text();
    if(is_caret(text, *selection)) {
      auto style = *find_text_style(document, nodes, selection);
      selection->set_pending_style(edit_style(style, editor(style)));
      return nullptr;
    }
    auto range = get_range(text, *selection);
    if(range.m_start != range.m_end) {
      return std::make_unique<RestyleTextCommand>(nodes.front(), range,
        std::move(label), std::move(editor), std::move(layouter));
    }
  }
  return std::make_unique<RestyleTextCommand>(nodes, std::move(label),
    std::move(editor), std::move(layouter));
}

TextLayouter Rellu::make_text_layouter(const FontLibrary& library) {
  return [library = &library] (const Text& text) {
    return store_layout(text, *library);
  };
}

QStringList Rellu::list_font_families(const FontLibrary& library) {
  auto families = std::set<QString>();
  for(auto& face : library.get_faces()) {
    families.insert(QString::fromStdString(face.m_names.m_typographic_family));
  }
  auto list = QStringList(families.begin(), families.end());
  std::sort(list.begin(), list.end(), [] (const auto& left, const auto& right) {
    return QString::compare(left, right, Qt::CaseInsensitive) < 0;
  });
  return list;
}

bool Rellu::is_valid_hyperlink(const QString& url) {
  auto start = qsizetype(0);
  while(start != url.size() && (url[start].isSpace() || url[start] == ',' ||
      url[start] == '#')) {
    ++start;
  }
  auto trimmed = url.mid(start).trimmed();
  return trimmed.isEmpty() || trimmed.startsWith("https://") ||
    trimmed.startsWith("mailto:");
}

QStringList Rellu::list_font_styles(
    const FontLibrary& library, const QString& family) {
  auto styles = QStringList();
  auto name = family.toStdString();
  for(auto& face : library.get_faces()) {
    if(face.m_names.m_typographic_family == name) {
      auto style = QString::fromStdString(face.m_names.m_typographic_style);
      if(!styles.contains(style)) {
        styles.push_back(style);
      }
    }
  }
  return styles;
}

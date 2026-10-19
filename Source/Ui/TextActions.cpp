#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include "Rellu/Ui/TextActions.hpp"
#include "Rellu/Ui/TextPropertyEditors.hpp"

using namespace Rellu;

namespace {
  constexpr auto BOLD_WEIGHT = 700.0;
  constexpr auto SEMIBOLD_WEIGHT = 600.0;
  constexpr auto REGULAR_WEIGHT = 400.0;
  constexpr auto MINIMUM_SIZE = 1.0;
  constexpr auto MAXIMUM_SIZE = 1000.0;

  const auto BOLD = "object.text.bold";
  const auto ITALIC = "object.text.italic";
  const auto UNDERLINE = "object.text.underline";
  const auto STRIKETHROUGH = "object.text.strikethrough";
  const auto UPPERCASE = "object.text.uppercase";
  const auto LOWERCASE = "object.text.lowercase";
  const auto TITLE_CASE = "object.text.title_case";
  const auto SUPERSCRIPT = "object.text.superscript";
  const auto SUBSCRIPT = "object.text.subscript";
  const auto INCREASE_SIZE = "object.text.increase_size";
  const auto DECREASE_SIZE = "object.text.decrease_size";
  const auto NAMES = std::to_array({BOLD, ITALIC, UNDERLINE, STRIKETHROUGH,
    UPPERCASE, LOWERCASE, TITLE_CASE, SUPERSCRIPT, SUBSCRIPT, INCREASE_SIZE,
    DECREASE_SIZE});

  std::optional<TypefaceNames> find_face(const FontLibrary& library,
      const std::string& family, auto is_candidate, double weight) {
    auto face = std::optional<TypefaceNames>();
    auto distance = std::numeric_limits<double>::infinity();
    for(auto& candidate : library.get_faces()) {
      auto& names = candidate.m_names;
      if(names.m_typographic_family != family || !is_candidate(names)) {
        continue;
      }
      if(auto gap = std::abs(names.m_weight - weight); gap < distance) {
        distance = gap;
        face = names;
      }
    }
    return face;
  }

  std::optional<TypefaceNames> find_names(
      const FontLibrary& library, const Font& font) {
    auto resolution = library.resolve(font);
    if(!resolution.m_typeface || (resolution.m_match != FontMatch::FAMILY &&
        resolution.m_match != FontMatch::FAMILY_AND_STYLE &&
        resolution.m_match != FontMatch::POSTSCRIPT_NAME)) {
      return std::nullopt;
    }
    return resolution.m_typeface->get_names();
  }

  bool is_bold_weight(double weight) {
    return weight >= BOLD_WEIGHT;
  }

  std::optional<TypefaceNames> find_bold_toggle(
      const FontLibrary& library, const Font& font) {
    auto names = find_names(library, font);
    if(!names) {
      return std::nullopt;
    }
    auto is_bolding = names->m_weight < SEMIBOLD_WEIGHT;
    if(!is_bolding && !is_bold_weight(names->m_weight)) {
      return std::nullopt;
    }
    return find_face(library, names->m_typographic_family,
      [&] (const TypefaceNames& candidate) {
        return candidate.m_is_italic == names->m_is_italic && (is_bolding ?
          is_bold_weight(candidate.m_weight) :
          candidate.m_weight < SEMIBOLD_WEIGHT);
      }, is_bolding ? BOLD_WEIGHT : REGULAR_WEIGHT);
  }

  std::optional<TypefaceNames> find_italic_toggle(
      const FontLibrary& library, const Font& font) {
    auto names = find_names(library, font);
    if(!names) {
      return std::nullopt;
    }
    return find_face(library, names->m_typographic_family,
      [&] (const TypefaceNames& candidate) {
        return candidate.m_is_italic != names->m_is_italic;
      }, names->m_weight);
  }

  TextStyleEdit make_face_edit(const TypefaceNames& names) {
    auto edit = TextStyleEdit();
    edit.m_family = names.m_typographic_family;
    edit.m_style = names.m_typographic_style;
    edit.m_postscript_name = names.m_postscript_name;
    return edit;
  }
}

TextActions::TextActions(ActionRegistry& actions, const FontLibrary& library)
    : m_actions(&actions),
      m_library(&library),
      m_layouter(make_text_layouter(library)),
      m_editor(nullptr) {
  m_actions->set_handler(BOLD, [this] { toggle_weight(); });
  m_actions->set_handler(ITALIC, [this] { toggle_slant(); });
  auto toggle = [this] (const char* name, const char* label,
      auto is_set, auto make_edit) {
    m_actions->set_handler(name, [=, this] {
      auto styles = get_styles();
      if(styles.empty()) {
        return;
      }
      apply(make_edit(std::ranges::all_of(styles, is_set)), label);
    });
  };
  toggle(UNDERLINE, "Underline", [] (const TextStyle& style) {
    return style.m_is_underlined;
  }, [] (bool is_set) {
    auto edit = TextStyleEdit();
    edit.m_is_underlined = !is_set;
    return edit;
  });
  toggle(STRIKETHROUGH, "Strikethrough", [] (const TextStyle& style) {
    return style.m_is_struck_through;
  }, [] (bool is_set) {
    auto edit = TextStyleEdit();
    edit.m_is_struck_through = !is_set;
    return edit;
  });
  auto transform = [&] (const char* name, const char* label,
      TextTransform value) {
    toggle(name, label, [=] (const TextStyle& style) {
      return style.m_transform == value;
    }, [=] (bool is_set) {
      auto edit = TextStyleEdit();
      edit.m_transform = is_set ? TextTransform::NONE : value;
      return edit;
    });
  };
  transform(UPPERCASE, "Uppercase", TextTransform::UPPERCASE);
  transform(LOWERCASE, "Lowercase", TextTransform::LOWERCASE);
  transform(TITLE_CASE, "Title Case", TextTransform::TITLE_CASE);
  auto script = [&] (const char* name, const char* label, TextScript value) {
    toggle(name, label, [=] (const TextStyle& style) {
      return style.m_script == value;
    }, [=] (bool is_set) {
      auto edit = TextStyleEdit();
      edit.m_script = is_set ? TextScript::NORMAL : value;
      return edit;
    });
  };
  script(SUPERSCRIPT, "Superscript", TextScript::SUPERSCRIPT);
  script(SUBSCRIPT, "Subscript", TextScript::SUBSCRIPT);
  m_actions->set_handler(INCREASE_SIZE, [this] { change_size(SIZE_STEP); });
  m_actions->set_handler(DECREASE_SIZE, [this] { change_size(-SIZE_STEP); });
  update();
}

TextActions::~TextActions() = default;

Editor* TextActions::get_editor() const {
  return m_editor;
}

void TextActions::set_editor(Editor* editor) {
  m_selection_subscription.release();
  m_history_subscription.release();
  m_text_selection_subscription.release();
  m_editor = editor;
  if(m_editor) {
    m_selection_subscription = m_editor->get_selection().subscribe([this] {
      update();
    });
    m_history_subscription = m_editor->get_history().subscribe([this] {
      update();
    });
    m_text_selection_subscription =
      m_editor->get_text_selection().subscribe([this] {
        update();
      });
  }
  update();
}

void TextActions::update() {
  auto style = get_style();
  for(auto name : NAMES) {
    m_actions->set_enabled(name, static_cast<bool>(style));
  }
  auto check = [&] (const char* name, bool is_checked) {
    m_actions->get(name).setChecked(is_checked);
  };
  check(UNDERLINE, style && style->m_is_underlined);
  check(STRIKETHROUGH, style && style->m_is_struck_through);
  check(UPPERCASE, style && style->m_transform == TextTransform::UPPERCASE);
  check(LOWERCASE, style && style->m_transform == TextTransform::LOWERCASE);
  check(TITLE_CASE, style && style->m_transform == TextTransform::TITLE_CASE);
  check(SUPERSCRIPT, style && style->m_script == TextScript::SUPERSCRIPT);
  check(SUBSCRIPT, style && style->m_script == TextScript::SUBSCRIPT);
}

std::vector<NodeId> TextActions::get_texts() const {
  auto texts = std::vector<NodeId>();
  if(!m_editor) {
    return texts;
  }
  auto& document = m_editor->get_document();
  for(auto& id : m_editor->get_selection().get_nodes()) {
    if(auto node = document.find(id); node && node->get_text()) {
      texts.push_back(id);
    }
  }
  return texts;
}

std::optional<TextStyle> TextActions::get_style() const {
  auto texts = get_texts();
  if(texts.empty()) {
    return std::nullopt;
  }
  return find_text_style(m_editor->get_document(), texts,
    &m_editor->get_text_selection());
}

void TextActions::apply(TextStyleEdit edit, std::string label) {
  auto texts = get_texts();
  if(texts.empty()) {
    return;
  }
  auto command = make_text_style_command(m_editor->get_document(), texts,
    &m_editor->get_text_selection(), std::move(edit), m_layouter,
    std::move(label));
  if(!command) {
    return;
  }
  try {
    m_editor->execute(std::move(command));
  } catch(const std::exception&) {}
}

std::vector<TextStyle> TextActions::get_styles() const {
  auto texts = get_texts();
  auto styles = std::vector<TextStyle>();
  if(texts.empty()) {
    return styles;
  }
  auto& document = m_editor->get_document();
  auto& selection = m_editor->get_text_selection();
  if(texts.size() == 1 && selection.get_node() == texts.front()) {
    auto& text = *document.get(texts.front()).get_text();
    auto size = text.m_content->size();
    auto range = selection.get_range();
    range.m_start = std::min(range.m_start, size);
    range.m_end = std::min(range.m_end, size);
    if(range.m_start == range.m_end) {
      styles.push_back(*get_style());
      return styles;
    }
    auto start = std::size_t(0);
    for(auto& run : get_runs(text)) {
      auto end = start + run.m_length;
      if(start < range.m_end && end > range.m_start) {
        styles.push_back(run.m_style);
      }
      start = end;
    }
    return styles;
  }
  for(auto& id : texts) {
    auto& text = *document.get(id).get_text();
    for(auto& run : get_runs(text)) {
      if(run.m_length != 0 || text.m_content->empty()) {
        styles.push_back(run.m_style);
      }
    }
  }
  return styles;
}

void TextActions::apply(std::string label, TextStyleEditor editor) {
  auto texts = get_texts();
  if(texts.empty()) {
    return;
  }
  auto command = make_text_style_command(m_editor->get_document(), texts,
    &m_editor->get_text_selection(), std::move(label), std::move(editor),
    m_layouter);
  if(!command) {
    return;
  }
  try {
    m_editor->execute(std::move(command));
  } catch(const std::exception&) {}
}

void TextActions::toggle_weight() {
  auto toggle = [library = m_library] (const TextStyle& style) {
    return find_bold_toggle(*library, style.m_font);
  };
  auto is_bold = std::ranges::any_of(get_styles(), [&] (const auto& style) {
    auto face = toggle(style);
    return face && is_bold_weight(face->m_weight);
  });
  apply("Bold", [=] (const TextStyle& style) {
    auto face = toggle(style);
    if(!face || is_bold_weight(face->m_weight) != is_bold) {
      return TextStyleEdit();
    }
    return make_face_edit(*face);
  });
}

void TextActions::toggle_slant() {
  auto toggle = [library = m_library] (const TextStyle& style) {
    return find_italic_toggle(*library, style.m_font);
  };
  auto is_italic = std::ranges::any_of(get_styles(), [&] (const auto& style) {
    auto face = toggle(style);
    return face && face->m_is_italic;
  });
  apply("Italic", [=] (const TextStyle& style) {
    auto face = toggle(style);
    if(!face || face->m_is_italic != is_italic) {
      return TextStyleEdit();
    }
    return make_face_edit(*face);
  });
}

void TextActions::change_size(double delta) {
  if(get_styles().empty()) {
    return;
  }
  auto label = delta > 0 ? "Increase Font Size" : "Decrease Font Size";
  apply(label, [delta] (const TextStyle& style) {
    auto edit = TextStyleEdit();
    edit.m_size = std::clamp(
      style.m_font.m_size + delta, MINIMUM_SIZE, MAXIMUM_SIZE);
    return edit;
  });
}

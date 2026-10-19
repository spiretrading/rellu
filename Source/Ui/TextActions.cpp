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
  constexpr auto BOLD_WEIGHT = 600.0;
  constexpr auto REGULAR_WEIGHT = 400.0;
  constexpr auto TARGET_BOLD_WEIGHT = 700.0;

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
  auto toggle = [this] (const char* name, auto make_edit) {
    m_actions->set_handler(name, [=, this] {
      if(auto style = get_style()) {
        apply(make_edit(*style));
      }
    });
  };
  toggle(UNDERLINE, [] (const TextStyle& style) {
    auto edit = TextStyleEdit();
    edit.m_is_underlined = !style.m_is_underlined;
    return edit;
  });
  toggle(STRIKETHROUGH, [] (const TextStyle& style) {
    auto edit = TextStyleEdit();
    edit.m_is_struck_through = !style.m_is_struck_through;
    return edit;
  });
  auto transform = [&] (const char* name, TextTransform value) {
    toggle(name, [=] (const TextStyle& style) {
      auto edit = TextStyleEdit();
      edit.m_transform =
        style.m_transform == value ? TextTransform::NONE : value;
      return edit;
    });
  };
  transform(UPPERCASE, TextTransform::UPPERCASE);
  transform(LOWERCASE, TextTransform::LOWERCASE);
  transform(TITLE_CASE, TextTransform::TITLE_CASE);
  auto script = [&] (const char* name, TextScript value) {
    toggle(name, [=] (const TextStyle& style) {
      auto edit = TextStyleEdit();
      edit.m_script = style.m_script == value ? TextScript::NORMAL : value;
      return edit;
    });
  };
  script(SUPERSCRIPT, TextScript::SUPERSCRIPT);
  script(SUBSCRIPT, TextScript::SUBSCRIPT);
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

void TextActions::apply(TextStyleEdit edit) {
  auto texts = get_texts();
  if(texts.empty()) {
    return;
  }
  auto command = make_text_style_command(m_editor->get_document(), texts,
    &m_editor->get_text_selection(), std::move(edit), m_layouter);
  if(!command) {
    return;
  }
  try {
    m_editor->execute(std::move(command));
  } catch(const std::exception&) {}
}

void TextActions::toggle_weight() {
  auto style = get_style();
  if(!style) {
    return;
  }
  auto resolution = m_library->resolve(style->m_font);
  if(!resolution.m_typeface) {
    return;
  }
  auto& current = resolution.m_typeface->get_names();
  auto is_bold = current.m_weight >= BOLD_WEIGHT;
  auto face = find_face(*m_library, style->m_font.m_family,
    [&] (const TypefaceNames& names) {
      return names.m_is_italic == current.m_is_italic &&
        (names.m_weight >= BOLD_WEIGHT) != is_bold;
    }, is_bold ? REGULAR_WEIGHT : TARGET_BOLD_WEIGHT);
  if(face) {
    apply(make_face_edit(*face));
  }
}

void TextActions::toggle_slant() {
  auto style = get_style();
  if(!style) {
    return;
  }
  auto resolution = m_library->resolve(style->m_font);
  if(!resolution.m_typeface) {
    return;
  }
  auto& current = resolution.m_typeface->get_names();
  auto face = find_face(*m_library, style->m_font.m_family,
    [&] (const TypefaceNames& names) {
      return names.m_is_italic != current.m_is_italic;
    }, current.m_weight);
  if(face) {
    apply(make_face_edit(*face));
  }
}

void TextActions::change_size(double delta) {
  if(auto style = get_style()) {
    auto edit = TextStyleEdit();
    edit.m_size = std::max(1.0, style->m_font.m_size + delta);
    apply(std::move(edit));
  }
}

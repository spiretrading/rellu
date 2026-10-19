#include <doctest/doctest.h>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Text/TextLayout.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "Rellu/Ui/TextPropertyEditors.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  PropertyEditors make_editors(Editor& editor) {
    auto& library = get_default_font_library();
    auto editors = PropertyEditors();
    add_text_property_editors(editors, library, make_text_measurer(library),
      [&] {
        return &editor.get_text_selection();
      });
    return editors;
  }

  bool apply(Editor& editor, const PropertyEditors& editors,
      std::string_view path, const NodeId& id, const PropertyValue& value) {
    auto property = editors.find(path);
    REQUIRE(property);
    auto command = property->m_setter(
      editor.get_document(), std::vector{id}, value);
    if(!command) {
      return false;
    }
    return editor.execute(std::move(command)) != nullptr;
  }

  const Text& get_text(const Document& document, const NodeId& id) {
    return *document.get(id).get_text();
  }
}

TEST_SUITE("TextPropertyEditors") {
  TEST_CASE("whole_text") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    REQUIRE(apply(editor, editors, "text.size", ids.m_label, 30.0));
    auto& text = get_text(document, ids.m_label);
    REQUIRE(text.m_runs.size() == 1);
    REQUIRE(text.m_runs.front().m_style.m_font.m_size == 30);
    REQUIRE(text.m_layout);
    auto relaid = text;
    relaid.m_layout = std::nullopt;
    REQUIRE(**text.m_layout ==
      store_layout(relaid, get_default_font_library()));
    REQUIRE(!apply(editor, editors, "text.size", ids.m_label, -2.0));
    REQUIRE(editors.find("text.size")->m_minimum == 1.0);
  }

  TEST_CASE("selected_range") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    editor.get_text_selection().set(ids.m_label, 6, 11);
    REQUIRE(apply(editor, editors, "text.underline", ids.m_label, true));
    auto& runs = get_text(document, ids.m_label).m_runs;
    REQUIRE(runs.size() == 3);
    REQUIRE(runs[1].m_length == 5);
    REQUIRE(runs[1].m_style.m_is_underlined);
    REQUIRE(!runs[0].m_style.m_is_underlined);
    REQUIRE(editor.get_history().get_undo_label() == "Underline");
    editor.get_text_selection().set(ids.m_label, 3, 3);
    REQUIRE(apply(editor, editors, "text.color", ids.m_label,
      make_color(255, 0, 0)));
    for(auto& run : get_text(document, ids.m_label).m_runs) {
      REQUIRE(run.m_style.m_color == make_color(255, 0, 0));
    }
  }

  TEST_CASE("fonts") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    REQUIRE(apply(editor, editors, "text.style", ids.m_label,
      QString("Bold")));
    auto& font = get_text(document, ids.m_label).m_runs.front().m_style.m_font;
    REQUIRE(font.m_family == "Roboto");
    REQUIRE(font.m_style == "Bold");
    REQUIRE(font.m_postscript_name == "Roboto-Bold");
    REQUIRE(apply(editor, editors, "text.family", ids.m_label,
      QString("Open Sans")));
    auto& changed =
      get_text(document, ids.m_label).m_runs.front().m_style.m_font;
    REQUIRE(changed.m_family == "Open Sans");
    REQUIRE(changed.m_style == "Bold");
    REQUIRE(changed.m_postscript_name == "OpenSans-Bold");
    auto families = list_font_families(get_default_font_library());
    REQUIRE(families.contains("Roboto"));
    REQUIRE(list_font_styles(get_default_font_library(), "Roboto").contains(
      "Medium"));
  }

  TEST_CASE("paragraph") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    REQUIRE(apply(editor, editors, "text.alignment", ids.m_label,
      Choice(1, QStringList())));
    REQUIRE(apply(editor, editors, "text.line_height", ids.m_label, 24.0));
    REQUIRE(apply(editor, editors, "text.paragraph_spacing", ids.m_label,
      6.0));
    auto& style = get_text(document, ids.m_label).m_paragraph_style;
    REQUIRE(style.m_alignment == TextAlignment::CENTER);
    REQUIRE(style.m_line_height == 24);
    REQUIRE(style.m_paragraph_spacing == 6);
    REQUIRE(apply(editor, editors, "text.line_height", ids.m_label, 0.0));
    REQUIRE(!get_text(document, ids.m_label).m_paragraph_style.m_line_height);
  }

  TEST_CASE("frame") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    REQUIRE(apply(editor, editors, "text.frame", ids.m_label,
      Choice(1, QStringList())));
    REQUIRE(std::get<AutoHeightFrame>(
      get_text(document, ids.m_label).m_frame).m_width == 200);
    REQUIRE(apply(editor, editors, "text.frame", ids.m_label,
      Choice(0, QStringList())));
    REQUIRE(std::holds_alternative<PositionedFrame>(
      get_text(document, ids.m_label).m_frame));
  }

  TEST_CASE("descriptors") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_label);
    auto family = find_property(properties, "text.family");
    REQUIRE(family);
    REQUIRE(family->m_value == PropertyValue(QString("Roboto")));
    auto style = find_property(properties, "text.style");
    REQUIRE(style);
    REQUIRE(style->m_value == PropertyValue(QString("Medium")));
    REQUIRE(find_property(properties, "text.underline"));
    REQUIRE(find_property(properties, "text.strikethrough"));
  }
}

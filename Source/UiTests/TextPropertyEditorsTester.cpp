#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/TextCommands.hpp"
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
    editor.get_text_selection().set(ids.m_label, 0, 23);
    REQUIRE(apply(editor, editors, "text.color", ids.m_label,
      make_color(255, 0, 0)));
    for(auto& run : get_text(document, ids.m_label).m_runs) {
      REQUIRE(run.m_style.m_color == make_color(255, 0, 0));
    }
  }

  TEST_CASE("caret") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    auto& selection = editor.get_text_selection();
    selection.set(ids.m_label, 3, 3);
    REQUIRE(!apply(editor, editors, "text.size", ids.m_label, 30.0));
    REQUIRE(!apply(editor, editors, "text.underline", ids.m_label, true));
    REQUIRE(editor.get_history().get_undo_count() == 0);
    REQUIRE(get_text(document, ids.m_label).m_runs.size() == 1);
    auto& pending = selection.get_pending_style();
    REQUIRE(pending);
    REQUIRE(pending->m_font.m_size == 30);
    REQUIRE(pending->m_is_underlined);
    REQUIRE(pending->m_font.m_family == "Roboto");
    auto properties = extract_properties(
      document, ids.m_label, estimate_text_bounds, &selection);
    REQUIRE(find_property(properties, "text.size")->m_value ==
      PropertyValue(30.0));
    REQUIRE(!find_property(properties, "text.hyperlink")->m_is_read_only);
    selection.set(ids.m_label, 4, 4);
    REQUIRE(!selection.get_pending_style());
  }

  TEST_CASE("caret_line_spacing") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    {
      auto transaction = Transaction(document);
      auto text = get_text(document, ids.m_label);
      text.m_base_style = get_default_text_style();
      text.m_runs.clear();
      transaction.replace_payload(ids.m_label, text);
      transaction.commit();
    }
    auto& selection = editor.get_text_selection();
    selection.set(ids.m_label, 3, 3);
    REQUIRE(!apply(editor, editors, "text.size", ids.m_label, 40.0));
    auto& library = get_default_font_library();
    auto properties = extract_properties(
      document, ids.m_label, make_text_measurer(library), &selection);
    REQUIRE(find_property(properties, "text.size")->m_value ==
      PropertyValue(40.0));
    REQUIRE(find_property(properties, "text.line_height")->m_value ==
      PropertyValue(53.0));
    REQUIRE(get_text(document, ids.m_label).m_base_style.m_font.m_size == 20);
    selection.clear();
    for(auto [size, spacing] : {std::pair(15.0, 20.0), std::pair(16.0, 21.0),
        std::pair(20.0, 27.0), std::pair(30.0, 40.0), std::pair(40.0, 53.0)}) {
      REQUIRE(apply(editor, editors, "text.size", ids.m_label, size));
      auto described = extract_properties(
        document, ids.m_label, make_text_measurer(library), &selection);
      REQUIRE(find_property(described, "text.line_height")->m_value ==
        PropertyValue(spacing));
    }
  }

  TEST_CASE("last_style") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    auto& selection = editor.get_text_selection();
    REQUIRE(selection.get_last_style() == get_default_text_style());
    REQUIRE(apply(editor, editors, "text.size", ids.m_label, 30.0));
    REQUIRE(selection.get_last_style() ==
      get_text(document, ids.m_label).m_runs.front().m_style);
    REQUIRE(apply(editor, editors, "text.paragraph_spacing", ids.m_label,
      20.0));
    REQUIRE(selection.get_last_paragraph_style().m_paragraph_spacing == 20);
    REQUIRE(selection.get_last_style().m_font.m_size == 30);
  }

  TEST_CASE("empty_text") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    {
      auto transaction = Transaction(document);
      auto text = get_text(document, ids.m_label);
      text.m_content = Shared(std::u16string());
      text.m_runs.clear();
      transaction.replace_payload(ids.m_label, text);
      transaction.commit();
    }
    editor.get_text_selection().set(ids.m_label, 0, 0);
    REQUIRE(apply(editor, editors, "text.size", ids.m_label, 30.0));
    REQUIRE(get_text(document, ids.m_label).m_base_style.m_font.m_size == 30);
    REQUIRE(!editor.get_text_selection().get_pending_style());
  }

  TEST_CASE("selection_descriptors") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    auto& selection = editor.get_text_selection();
    selection.set(ids.m_label, 0, 5);
    REQUIRE(apply(editor, editors, "text.size", ids.m_label, 30.0));
    auto describe = [&] (std::size_t anchor, std::size_t caret) {
      selection.set(ids.m_label, anchor, caret);
      return extract_properties(
        document, ids.m_label, estimate_text_bounds, &selection);
    };
    auto first = describe(1, 2);
    auto size = find_property(first, "text.size");
    REQUIRE(size->m_value == PropertyValue(30.0));
    REQUIRE(!size->m_is_mixed);
    auto second = describe(8, 9);
    REQUIRE(find_property(second, "text.size")->m_value ==
      PropertyValue(14.0));
    auto both = describe(3, 9);
    REQUIRE(find_property(both, "text.size")->m_is_mixed);
    auto caret = describe(5, 5);
    REQUIRE(find_property(caret, "text.size")->m_value ==
      PropertyValue(30.0));
    selection.clear();
    auto whole = extract_properties(
      document, ids.m_label, estimate_text_bounds, &selection);
    REQUIRE(find_property(whole, "text.size")->m_is_mixed);
  }

  TEST_CASE("hyperlink") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    auto& selection = editor.get_text_selection();
    selection.set(ids.m_label, 0, 5);
    REQUIRE(apply(editor, editors, "text.hyperlink", ids.m_label,
      QString("HTTPS://EXAMPLE.COM")));
    REQUIRE(get_text(document, ids.m_label).m_runs.front().m_hyperlink ==
      "HTTPS://EXAMPLE.COM");
    REQUIRE(apply(editor, editors, "text.hyperlink", ids.m_label,
      QString("")));
    REQUIRE(get_text(document, ids.m_label).m_runs.size() == 1);
    REQUIRE(!get_text(document, ids.m_label).m_runs.front().m_style.
      m_is_underlined);
    REQUIRE(editor.get_history().get_undo_label() == "Text Hyperlink");
    REQUIRE(apply(editor, editors, "text.hyperlink", ids.m_label,
      QString("https://example.com")));
    auto& runs = get_text(document, ids.m_label).m_runs;
    REQUIRE(runs.size() == 2);
    REQUIRE(runs.front().m_hyperlink == "https://example.com");
    REQUIRE(runs.front().m_style.m_is_underlined);
    REQUIRE(runs.front().m_style.m_color == runs.back().m_style.m_color);
    REQUIRE(runs.back().m_hyperlink.empty());
    REQUIRE(!runs.back().m_style.m_is_underlined);
    REQUIRE(editor.get_history().get_undo_label() == "Text Hyperlink");
    REQUIRE(selection.get_last_style() == get_default_text_style());
    auto properties = extract_properties(
      document, ids.m_label, estimate_text_bounds, &selection);
    auto hyperlink = find_property(properties, "text.hyperlink");
    REQUIRE(hyperlink->m_value ==
      PropertyValue(QString("https://example.com")));
    REQUIRE(!hyperlink->m_is_read_only);
    selection.clear();
    REQUIRE(find_property(extract_properties(document, ids.m_label),
      "text.hyperlink")->m_is_mixed);
    selection.set(ids.m_label, 8, 8);
    REQUIRE(!apply(editor, editors, "text.hyperlink", ids.m_label,
      QString("mailto:someone@example.com")));
    REQUIRE(selection.get_pending_hyperlink() ==
      std::optional<std::string>("mailto:someone@example.com"));
    REQUIRE(selection.get_pending_style()->m_is_underlined);
    auto pending = extract_properties(
      document, ids.m_label, estimate_text_bounds, &selection);
    REQUIRE(find_property(pending, "text.hyperlink")->m_value ==
      PropertyValue(QString("mailto:someone@example.com")));
    REQUIRE(get_text(document, ids.m_label).m_runs.back().m_hyperlink.empty());
    REQUIRE(is_valid_hyperlink(""));
    REQUIRE(is_valid_hyperlink(" ,#https://example.com "));
    REQUIRE(!is_valid_hyperlink("HTTPS://example.com"));
    REQUIRE(!is_valid_hyperlink("ftp://example.com"));
  }

  TEST_CASE("hyperlink_validity") {
    REQUIRE(check_hyperlink(" , ") == HyperlinkValidity::EMPTY);
    for(auto url : {"example.com", "ftp://example.com", "https:example.com",
        "HTTPS://example.com"}) {
      REQUIRE(check_hyperlink(url) == HyperlinkValidity::INVALID_PROTOCOL);
    }
    for(auto url : {"https://", "https://example", "https://localhost",
        "https://example.c", "https://exa mple.com", "https://exa_mple.com",
        "https://ex-.com", "https://.com", "https://1.2.3.4",
        "https://example.com:8080", "https://example.com:abc", "mailto:",
        "mailto:abc"}) {
      REQUIRE(check_hyperlink(url) == HyperlinkValidity::INVALID_DOMAIN);
    }
    for(auto url : {"https://example.com", "https://example.zzzz",
        "https://www.ex-ample.co.uk", "https://example.com/a b",
        " ,#https://example.com ", "mailto:someone@example.com"}) {
      REQUIRE(check_hyperlink(url) == HyperlinkValidity::VALID);
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
    REQUIRE(editor.get_history().get_undo_label() == "Font Style");
    REQUIRE(apply(editor, editors, "text.family", ids.m_label,
      QString("Open Sans")));
    REQUIRE(editor.get_history().get_undo_label() == "Font Family");
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
    REQUIRE(editor.get_history().get_undo_label() == "Text Align");
    REQUIRE(apply(editor, editors, "text.line_height", ids.m_label, 24.0));
    REQUIRE(editor.get_history().get_undo_label() == "Line Spacing");
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
    auto& library = get_default_font_library();
    auto width = 0.0;
    for(auto& line : measure_lines(get_text(document, ids.m_label), library)) {
      width = std::max(width, line.m_carets.back() - line.m_carets.front());
    }
    REQUIRE(apply(editor, editors, "text.frame", ids.m_label,
      Choice(1, QStringList())));
    auto& text = get_text(document, ids.m_label);
    REQUIRE(std::get<AutoHeightFrame>(text.m_frame).m_width ==
      std::ceil(width));
    REQUIRE(measure_lines(text, library).size() == 2);
  }

  TEST_CASE("auto_width_line_breaks") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto text = get_text(document, ids.m_label);
      text.m_content = Shared(std::u16string(u"aaaa bbbb cccc dddd\nee"));
      text.m_frame = AutoHeightFrame(60);
      text.m_runs.clear();
      text.m_layout = std::nullopt;
      transaction.replace_payload(ids.m_label, text);
      transaction.commit();
    }
    auto& library = get_default_font_library();
    auto lines = measure_lines(get_text(document, ids.m_label), library);
    REQUIRE(lines.size() > 2);
    auto expected = std::u16string();
    auto& content = *get_text(document, ids.m_label).m_content;
    for(auto i = std::size_t(0); i != lines.size(); ++i) {
      auto end = i + 1 == lines.size() ? content.size() : lines[i + 1].m_start;
      expected += content.substr(lines[i].m_start, end - lines[i].m_start);
      if(i + 1 != lines.size() && !lines[i].m_is_paragraph_end) {
        expected += u'\n';
      }
    }
    REQUIRE(expected.starts_with(u"aaaa \n"));
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    REQUIRE(apply(editor, editors, "text.frame", ids.m_label,
      Choice(0, QStringList())));
    auto& text = get_text(document, ids.m_label);
    REQUIRE(std::holds_alternative<PositionedFrame>(text.m_frame));
    REQUIRE(*text.m_content == expected);
    REQUIRE(measure_lines(text, library).size() == lines.size());
    REQUIRE(document.get(ids.m_label).m_header.m_name ==
      make_text_name(expected));
    REQUIRE(editor.undo());
    REQUIRE(*get_text(document, ids.m_label).m_content ==
      u"aaaa bbbb cccc dddd\nee");
    REQUIRE(document.get(ids.m_label).m_header.m_name.empty());
  }

  TEST_CASE("auto_width_conversion") {
    auto& library = get_default_font_library();
    auto convert = [&] (TextFrame frame, double y) {
      auto document = Document();
      auto ids = build_test_document(document);
      {
        auto transaction = Transaction(document);
        auto text = get_text(document, ids.m_label);
        text.m_content =
          Shared(std::u16string(u"alpha beta gamma delta epsilon"));
        text.m_frame = frame;
        text.m_base_style.m_font =
          Font("Roboto", "Regular", "Roboto-Regular", 26);
        text.m_paragraph_style.m_line_height = 18;
        text.m_runs.clear();
        text.m_layout = std::nullopt;
        transaction.replace_payload(ids.m_label, text);
        auto header = document.get(ids.m_label).m_header;
        header.m_transform = make_translation(292, y);
        transaction.replace_header(header);
        transaction.commit();
      }
      auto editor = Editor(document);
      auto editors = make_editors(editor);
      REQUIRE(apply(editor, editors, "text.frame", ids.m_label,
        Choice(0, QStringList())));
      auto& text = get_text(document, ids.m_label);
      REQUIRE(*text.m_content == u"alpha \nbeta \ngamma \ndelta \nepsilon");
      REQUIRE(document.get(ids.m_label).m_header.m_name ==
        "alpha \nbeta \ngamma \ndelta \nepsilon");
      REQUIRE(document.get(ids.m_label).m_header.m_transform ==
        make_translation(292, y + 25));
      auto layout = store_layout(text, library);
      using Range = std::tuple<std::size_t, std::size_t, double>;
      auto ranges = std::vector<Range>();
      for(auto& paragraph : layout.m_paragraphs) {
        REQUIRE(paragraph.m_lines.size() == 1);
        auto& runs = paragraph.m_lines.front().m_runs;
        ranges.emplace_back(runs.front().m_from, runs.back().m_to,
          runs.front().m_position.m_y);
      }
      REQUIRE(ranges == std::vector<Range>({{0, 6, 0}, {7, 12, 18},
        {13, 19, 36}, {20, 26, 54}, {27, 34, 72}}));
    };
    convert(AutoHeightFrame(120), 30);
    convert(AreaFrame(Size(120, 104)), 250);
  }

  TEST_CASE("auto_height_width") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto editors = make_editors(editor);
    auto& library = get_default_font_library();
    auto contents = {u"Hello world", u"The quick brown fox jumps",
      u"Wavy AVA tyre, yes.", u"lorem ipsum dolor sit amet", u"a b c d e f",
      u"Typography Kerning: AWAY To Ty"};
    auto fonts = {Font("Segoe UI", "Regular", "SegoeUI", 20),
      Font("Segoe UI", "Bold", "SegoeUI-Bold", 15),
      Font("Roboto", "Regular", "Roboto-Regular", 13),
      Font("Arial", "Regular", "ArialMT", 17.5),
      Font("Open Sans", "Regular", "OpenSans-Regular", 11)};
    for(auto& content : contents) {
      for(auto& font : fonts) {
        {
          auto transaction = Transaction(document);
          auto text = get_text(document, ids.m_label);
          text.m_content = Shared(std::u16string(content));
          text.m_frame = PositionedFrame();
          text.m_base_style.m_font = font;
          text.m_runs.clear();
          text.m_layout = std::nullopt;
          transaction.replace_payload(ids.m_label, text);
          transaction.commit();
        }
        REQUIRE(apply(editor, editors, "text.frame", ids.m_label,
          Choice(1, QStringList())));
        auto& text = get_text(document, ids.m_label);
        auto width = std::get<AutoHeightFrame>(text.m_frame).m_width;
        REQUIRE(width == std::round(width));
        REQUIRE(measure_lines(text, library).size() == 1);
      }
    }
  }

  TEST_CASE("descriptors") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_label);
    REQUIRE(find_property(properties, "text.line_height")->m_value ==
      PropertyValue(17.0));
    auto& library = get_default_font_library();
    auto measured = extract_properties(
      document, ids.m_label, make_text_measurer(library));
    auto& text = get_text(document, ids.m_label);
    auto lines = measure_lines(text, library);
    REQUIRE(find_property(measured, "text.line_height")->m_value ==
      PropertyValue(lines.front().m_ascent + lines.front().m_descent));
    {
      auto transaction = Transaction(document);
      auto label = text;
      label.m_base_style = get_default_text_style();
      label.m_runs.clear();
      transaction.replace_payload(ids.m_label, label);
      transaction.commit();
    }
    auto default_style = extract_properties(
      document, ids.m_label, make_text_measurer(library));
    REQUIRE(find_property(default_style, "text.line_height")->m_value ==
      PropertyValue(27.0));
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

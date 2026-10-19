#include <memory>
#include <utility>
#include <doctest/doctest.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "Rellu/Ui/TextPropertyEditors.hpp"
#include "../Ui/HyperlinkStatus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  QLabel& get_label(QWidget& container, const char* name) {
    auto label = container.findChild<QLabel*>(name);
    REQUIRE(label);
    return *label;
  }

  void type_url(QLineEdit& line, const QString& url) {
    line.setText(url);
    line.setModified(true);
    Q_EMIT line.editingFinished();
  }
}

TEST_SUITE("HyperlinkStatus") {
  TEST_CASE("status") {
    auto row = new QWidget();
    auto layout = new QHBoxLayout(row);
    auto line = new QLineEdit();
    layout->addWidget(line);
    auto container = std::unique_ptr<QWidget>(add_hyperlink_status(row, *line));
    auto& icon = get_label(*container, "hyperlink_status");
    auto& message = get_label(*container, "hyperlink_message");
    REQUIRE(icon.parentWidget() == row);
    REQUIRE(icon.pixmap().isNull());
    REQUIRE(message.isHidden());
    line->setText("www.example.com");
    REQUIRE(!icon.pixmap().isNull());
    REQUIRE(!message.isHidden());
    REQUIRE(message.text() == "URL protocol must be https:// or mailto:");
    REQUIRE(line->toolTip() == "www.example.com");
    REQUIRE(!line->styleSheet().isEmpty());
    line->setText("https://localhost");
    REQUIRE(message.text() == "URL includes an invalid domain");
    line->setText("https://www.example.com");
    REQUIRE(!icon.pixmap().isNull());
    REQUIRE(message.isHidden());
    REQUIRE(line->styleSheet().isEmpty());
    line->setText("");
    REQUIRE(icon.pixmap().isNull());
    line->setText("HTTPS://EXAMPLE.COM");
    REQUIRE(!message.isHidden());
    line->selectAll();
    line->insert("https://example.com");
    REQUIRE(line->isModified());
    REQUIRE(!message.isHidden());
    Q_EMIT line->editingFinished();
    REQUIRE(message.isHidden());
  }

  TEST_CASE("inspector_field") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto inspector = PropertyInspector();
    auto& library = get_default_font_library();
    auto editors = make_standard_property_editors(estimate_text_bounds);
    add_text_property_editors(editors, library, make_text_measurer(library),
      [&] {
        return &editor.get_text_selection();
      });
    inspector.set_property_editors(std::move(editors));
    inspector.set_editor(&editor);
    editor.get_selection().select(ids.m_label);
    auto line =
      qobject_cast<QLineEdit*>(inspector.find_field("text.hyperlink"));
    REQUIRE(line);
    REQUIRE(!line->isReadOnly());
    type_url(*line, "example.com");
    auto& run = document.get(ids.m_label).get_text()->m_runs.front();
    REQUIRE(run.m_hyperlink == "example.com");
    REQUIRE(run.m_style.m_is_underlined);
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.get_history().get_undo_label() == "Text Hyperlink");
    line =
      qobject_cast<QLineEdit*>(inspector.find_field("text.hyperlink"));
    REQUIRE(line->text() == "example.com");
    Q_EMIT line->editingFinished();
    REQUIRE(editor.get_history().get_undo_count() == 1);
    type_url(*line, "");
    auto& cleared = document.get(ids.m_label).get_text()->m_runs.front();
    REQUIRE(cleared.m_hyperlink.empty());
    REQUIRE(!cleared.m_style.m_is_underlined);
    REQUIRE(editor.get_history().get_undo_count() == 2);
  }
}

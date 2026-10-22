#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <doctest/doctest.h>
#include "Rellu/Ui/ExportDialog.hpp"

using namespace Rellu;

namespace {
  QPushButton* find_button(QWidget& widget, const QString& text) {
    for(auto button : widget.findChildren<QPushButton*>()) {
      if(button->text() == text) {
        return button;
      }
    }
    return nullptr;
  }

  bool is_label_shown(QWidget& widget, const QString& text) {
    for(auto label : widget.findChildren<QLabel*>()) {
      if(label->text() == text) {
        return label->isVisibleTo(&widget);
      }
    }
    return false;
  }
}

TEST_SUITE("ExportDialog") {
  TEST_CASE("rows") {
    auto window = QWidget();
    auto dialog = ExportDialog(ExportOptions(), QString(), false, 2, &window);
    REQUIRE(is_label_shown(dialog, "Format:"));
    REQUIRE(is_label_shown(dialog, "Export size:"));
    REQUIRE(!is_label_shown(dialog, "Quality:"));
    REQUIRE(is_label_shown(dialog, "Export to:"));
    REQUIRE(!is_label_shown(dialog, "Styling:"));
    auto options = ExportOptions();
    options.m_format = ExportFormat::JPG;
    dialog.set_options(options);
    REQUIRE(is_label_shown(dialog, "Quality:"));
    REQUIRE(is_label_shown(dialog, "Export size:"));
    options.m_format = ExportFormat::SVG;
    dialog.set_options(options);
    REQUIRE(!is_label_shown(dialog, "Export size:"));
    REQUIRE(is_label_shown(dialog, "Styling:"));
    REQUIRE(is_label_shown(dialog, "Save images:"));
    REQUIRE(is_label_shown(dialog, "File size:"));
    REQUIRE(is_label_shown(dialog, "Path Options:"));
    options.m_format = ExportFormat::PDF;
    dialog.set_options(options);
    REQUIRE(is_label_shown(dialog, "Save assets as:"));
    REQUIRE(!is_label_shown(dialog, "Styling:"));
  }

  TEST_CASE("hint") {
    auto window = QWidget();
    auto options = ExportOptions();
    auto dialog = ExportDialog(options, QString(), false, 1, &window);
    REQUIRE(dialog.get_hint().isEmpty());
    options.m_preset = ExportPreset::WEB;
    dialog.set_options(options);
    REQUIRE(dialog.get_hint() == "Selected assets will export at 1x and 2x");
    options.m_preset = ExportPreset::IOS;
    dialog.set_options(options);
    REQUIRE(dialog.get_hint() ==
      "Selected assets will export at 1x, 2x and 3x");
    options.m_preset = ExportPreset::ANDROID;
    dialog.set_options(options);
    REQUIRE(dialog.get_hint() == "Selected assets will export at ldpi, mdpi, "
      "hdpi, xhdpi, xxhdpi and xxxhdpi");
    options.m_format = ExportFormat::SVG;
    dialog.set_options(options);
    REQUIRE(dialog.get_hint().isEmpty());
  }

  TEST_CASE("export_label") {
    auto window = QWidget();
    auto selected = ExportDialog(ExportOptions(), QString(), false, 1, &window);
    REQUIRE(selected.get_export_label() == "Export");
    auto all = ExportDialog(ExportOptions(), QString(), true, 3, &window);
    REQUIRE(all.get_export_label() == "Export All Artboards");
    REQUIRE(find_button(all, "Export All Artboards"));
  }

  TEST_CASE("destination") {
    auto window = QWidget();
    auto dialog = ExportDialog(ExportOptions(), QString(), false, 1, &window);
    REQUIRE(find_button(dialog, "Choose Destination"));
    auto chosen = QString();
    auto exports = 0;
    dialog.set_folder_chooser([&] (const QString& folder) {
      return chosen;
    });
    dialog.set_export_handler([&] {
      ++exports;
    });
    dialog.submit();
    REQUIRE(exports == 0);
    REQUIRE(dialog.get_folder().isEmpty());
    chosen = "C:/out/assets";
    dialog.submit();
    REQUIRE(exports == 1);
    REQUIRE(dialog.get_folder() == "C:/out/assets");
    REQUIRE(find_button(dialog, "Change"));
    REQUIRE(!find_button(dialog, "Choose Destination"));
  }

  TEST_CASE("options") {
    auto window = QWidget();
    auto options = ExportOptions();
    options.m_format = ExportFormat::JPG;
    options.m_jpg_quality = 60;
    options.m_scale = 1.5;
    auto dialog = ExportDialog(options, QString(), false, 1, &window);
    REQUIRE(dialog.get_options() == options);
    options.m_preset = ExportPreset::ANDROID;
    options.m_svg_styling = SvgStyling::INTERNAL_CSS;
    options.m_svg_images = SvgImages::LINK;
    options.m_is_svg_minified = true;
    options.m_is_pdf_single = false;
    dialog.set_options(options);
    REQUIRE(dialog.get_options() == options);
  }

  TEST_CASE("progress") {
    auto window = QWidget();
    auto dialog = ExportDialog(ExportOptions(), "C:/out", false, 1, &window);
    REQUIRE(!dialog.is_exporting());
    REQUIRE(find_button(dialog, "Export")->isVisibleTo(&dialog));
    dialog.set_exporting(true);
    REQUIRE(dialog.is_exporting());
    REQUIRE(!find_button(dialog, "Export")->isVisibleTo(&dialog));
    REQUIRE(is_label_shown(dialog, "Exporting assets"));
    dialog.set_progress(1, 4);
    REQUIRE(!dialog.is_cancel_requested());
    dialog.request_cancel();
    REQUIRE(dialog.is_cancel_requested());
    dialog.set_exporting(false);
    REQUIRE(!dialog.is_cancel_requested());
    REQUIRE(find_button(dialog, "Export")->isVisibleTo(&dialog));
  }

  TEST_CASE("typed_scale") {
    auto window = QWidget();
    auto dialog = ExportDialog(ExportOptions(), QString(), false, 1, &window);
    REQUIRE(dialog.get_scale_text().isEmpty());
    REQUIRE(dialog.set_scale_text("1/3"));
    REQUIRE(dialog.get_scale_text() == "1/3x");
    REQUIRE(dialog.get_options().m_scale == doctest::Approx(1.0 / 3));
    REQUIRE(dialog.set_scale_text("2.5"));
    REQUIRE(dialog.get_scale_text() == "2.5x");
    REQUIRE(dialog.get_options().m_scale == 2.5);
    REQUIRE(!dialog.set_scale_text("abc"));
    REQUIRE(dialog.get_scale_text() == "2.5x");
    REQUIRE(dialog.set_scale_text("3"));
    REQUIRE(dialog.get_scale_text().isEmpty());
    REQUIRE(dialog.get_options().m_scale == 3);
    auto options = ExportOptions();
    options.m_scale = 0.25;
    auto typed = ExportDialog(options, "1/4x", QString(), false, 1, &window);
    REQUIRE(typed.get_scale_text() == "1/4x");
    REQUIRE(typed.get_options().m_scale == 0.25);
  }
}
#include <memory>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QWidget>
#include <doctest/doctest.h>
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/ExportActions.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    Document m_document;
    Editor m_editor;
    std::unique_ptr<ExportActions> m_exports;
    std::vector<XdAlertContent> m_alerts;
    int m_choice;

    Fixture()
        : m_actions(m_window),
          m_editor(m_document),
          m_choice(0) {
      add_xd_menus(m_actions);
      m_exports = std::make_unique<ExportActions>(m_actions, m_window);
      m_exports->set_alert_handler([this] (const auto& content) {
        m_alerts.push_back(content);
        return m_choice;
      });
    }
  };

  NodeId add_icon(Document& document, const std::string& name) {
    auto transaction = Transaction(document);
    auto header = make_header(name);
    auto id = transaction.append(
      NodeId(), std::move(header), make_rectangle_shape(10, 20));
    transaction.commit();
    return id;
  }
}

TEST_SUITE("ExportActions") {
  TEST_CASE("enabled_actions") {
    auto fixture = Fixture();
    REQUIRE(!fixture.m_actions.get("file.export.selected").isEnabled());
    REQUIRE(!fixture.m_actions.get("file.export.all").isEnabled());
    build_test_document(fixture.m_document);
    fixture.m_exports->set_editor(&fixture.m_editor);
    REQUIRE(fixture.m_actions.get("file.export.all").isEnabled());
    REQUIRE(!fixture.m_actions.get("file.export.batch").isEnabled());
    REQUIRE(!fixture.m_actions.get("file.export.selected").isEnabled());
    fixture.m_editor.select_all();
    REQUIRE(fixture.m_actions.get("file.export.selected").isEnabled());
  }

  TEST_CASE("export_selected") {
    auto directory = QTemporaryDir();
    auto fixture = Fixture();
    auto icon = add_icon(fixture.m_document, "Icon");
    fixture.m_exports->set_editor(&fixture.m_editor);
    auto shown = 0;
    fixture.m_exports->set_dialog_handler([&] (auto& dialog) {
      ++shown;
      REQUIRE(dialog.get_folder() ==
        (shown == 1 ? QString() : directory.path()));
      auto options = dialog.get_options();
      options.m_scale = 2;
      dialog.set_options(options);
      dialog.set_folder(directory.path());
      dialog.submit();
      REQUIRE(dialog.result() == QDialog::Accepted);
    });
    fixture.m_exports->export_nodes({icon}, false);
    REQUIRE(QImage(directory.filePath("Icon.png")).size() == QSize(20, 40));
    REQUIRE(fixture.m_exports->get_options().m_scale == 2);
    REQUIRE(fixture.m_exports->get_folder() == directory.path());
    REQUIRE(fixture.m_alerts.empty());
    fixture.m_choice = 1;
    fixture.m_exports->set_dialog_handler([&] (auto& dialog) {
      ++shown;
      dialog.submit();
      REQUIRE(dialog.result() != QDialog::Accepted);
      REQUIRE(!dialog.is_exporting());
    });
    fixture.m_exports->export_nodes({icon}, false);
    REQUIRE(fixture.m_alerts.size() == 1);
    REQUIRE(fixture.m_alerts[0].m_message ==
      "\"Icon.png\" already exists. Do you want to replace it?");
  }

  TEST_CASE("cancelled_options") {
    auto fixture = Fixture();
    auto icon = add_icon(fixture.m_document, "Icon");
    fixture.m_exports->set_editor(&fixture.m_editor);
    fixture.m_exports->set_dialog_handler([&] (auto& dialog) {
      auto options = dialog.get_options();
      options.m_format = ExportFormat::JPG;
      dialog.set_options(options);
      dialog.reject();
    });
    fixture.m_exports->export_nodes({icon}, false);
    REQUIRE(fixture.m_exports->get_options() == ExportOptions());
  }

  TEST_CASE("restored_settings") {
    auto directory = QTemporaryDir();
    auto fixture = Fixture();
    auto icon = add_icon(fixture.m_document, "Icon");
    fixture.m_exports->set_editor(&fixture.m_editor);
    auto stored = 0;
    fixture.m_exports->set_settings_handler([&] {
      ++stored;
    });
    auto options = ExportOptions();
    options.m_format = ExportFormat::JPG;
    options.m_jpg_quality = 60;
    options.m_scale = 0.25;
    fixture.m_exports->set_settings(options, "1/4x", directory.path());
    REQUIRE(fixture.m_exports->get_options() == options);
    REQUIRE(fixture.m_exports->get_scale_text() == "1/4x");
    REQUIRE(fixture.m_exports->get_folder() == directory.path());
    fixture.m_exports->set_dialog_handler([&] (auto& dialog) {
      REQUIRE(dialog.get_options() == options);
      REQUIRE(dialog.get_scale_text() == "1/4x");
      REQUIRE(dialog.get_folder() == directory.path());
      dialog.reject();
    });
    fixture.m_exports->export_nodes({icon}, false);
    REQUIRE(stored == 0);
    fixture.m_exports->set_dialog_handler([&] (auto& dialog) {
      dialog.submit();
    });
    fixture.m_exports->export_nodes({icon}, false);
    REQUIRE(stored == 1);
    REQUIRE(QFileInfo(directory.filePath("Icon.jpg")).exists());
  }

  TEST_CASE("failed_export") {
    auto directory = QTemporaryDir();
    auto fixture = Fixture();
    auto icon = add_icon(fixture.m_document, "Icon");
    fixture.m_exports->set_editor(&fixture.m_editor);
    fixture.m_exports->set_dialog_handler([&] (auto& dialog) {
      auto options = dialog.get_options();
      options.m_scale = 100000;
      dialog.set_options(options);
      dialog.set_folder(directory.path());
      dialog.submit();
    });
    fixture.m_exports->export_nodes({icon}, false);
    REQUIRE(fixture.m_alerts.size() == 1);
    REQUIRE(fixture.m_alerts[0].m_message ==
      "There was a problem exporting your asset.");
    REQUIRE(fixture.m_alerts[0].m_buttons == std::vector<QString>{"OK"});
  }

  TEST_CASE("overwrite_alert") {
    auto single = make_overwrite_alert({"Solid.png"}, "C:/out/c_sel1/");
    REQUIRE(single.m_message ==
      "\"Solid.png\" already exists. Do you want to replace it?");
    REQUIRE(single.m_explanation == "A file with the same name already "
      "exists in the folder \"c_sel1\". Replacing it will overwrite its "
      "current contents.");
    REQUIRE(single.m_buttons == std::vector<QString>{"Replace", "Cancel"});
    auto pair = make_overwrite_alert({"a.png", "b.png"}, "C:/out");
    REQUIRE(pair.m_message ==
      "\"a.png\" and \"b.png\" already exist. Do you want to replace them?");
    REQUIRE(pair.m_explanation == "Files with the same name already exist in "
      "the folder \"out\". Replacing them will overwrite their current "
      "contents.");
    auto many = make_overwrite_alert({"a.png", "b.png", "c.png"}, "C:/out");
    REQUIRE(many.m_message == "\"a.png\" and 2 other files already exist. "
      "Do you want to replace them?");
  }

  TEST_CASE("failure_alert") {
    auto multiple = make_failure_alert(ExportFailure::GENERIC, 3, {});
    REQUIRE(multiple.m_message == "There was a problem exporting 3 assets.");
    REQUIRE(multiple.m_explanation.isEmpty());
    REQUIRE(multiple.m_link.endsWith(
      "Please tell us about the problem [here|http://adobe.com/go/"
      "xd_export_errors]."));
    auto read_only = make_failure_alert(
      ExportFailure::READ_ONLY, 1, {"C:/out/Icon.png"});
    REQUIRE(read_only.m_message ==
      "There was a problem overwriting a read-only asset");
    REQUIRE(read_only.m_explanation == "Icon.png");
    auto denied = make_failure_alert(ExportFailure::ACCESS_DENIED, 2,
      {"C:/out/a.png", "C:/out/b.png"});
    REQUIRE(denied.m_message.startsWith(
      "There was a problem exporting 2 assets. Could not overwrite"));
    REQUIRE(denied.m_explanation == "a.png\nb.png");
  }

  TEST_CASE("translated_alerts") {
    set_language(Language::GERMAN);
    auto alert = make_overwrite_alert({"Solid.png"}, "C:/out/c_sel1");
    auto failure = make_failure_alert(ExportFailure::GENERIC, 2, {});
    set_language(Language::ENGLISH);
    REQUIRE(alert.m_message.contains("Solid.png"));
    REQUIRE(alert.m_message !=
      "\"Solid.png\" already exists. Do you want to replace it?");
    REQUIRE(alert.m_explanation.contains("c_sel1"));
    REQUIRE(alert.m_buttons[0] != "Replace");
    REQUIRE(failure.m_message.contains("2"));
    REQUIRE(failure.m_message != "There was a problem exporting 2 assets.");
    REQUIRE(failure.m_link.contains('['));
  }
}
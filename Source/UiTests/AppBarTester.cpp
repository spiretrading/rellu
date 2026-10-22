#include <array>
#include <vector>
#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include "Rellu/Ui/AppBar.hpp"

using namespace Rellu;

TEST_SUITE("AppBar") {
  TEST_CASE("zoom_text") {
    REQUIRE(format_zoom(0.306) == "30.6%");
    REQUIRE(format_zoom(1.689) == "168.9%");
    REQUIRE(format_zoom(1.93) == "193%");
    REQUIRE(format_zoom(1) == "100%");
    REQUIRE(format_zoom(0.25) == "25%");
    REQUIRE(format_zoom(1173.0 / 2000) == "58.6%");
    REQUIRE(format_zoom(1173.0 / 1200) == "97.8%");
    REQUIRE(format_zoom(2539.0 / 1800) == "141.1%");
    REQUIRE(parse_zoom("150%") == 1.5);
    REQUIRE(*parse_zoom(" 30.6 ") == doctest::Approx(0.306));
    REQUIRE(!parse_zoom("wide"));
    REQUIRE(!parse_zoom("0%"));
    REQUIRE(!parse_zoom("-50"));
  }

  TEST_CASE("zoom_field") {
    auto bar = AppBar();
    auto zooms = std::vector<double>();
    bar.set_zoom_handler([&] (auto zoom) {
      zooms.push_back(zoom);
    });
    bar.set_zoom(0.306);
    REQUIRE(bar.get_zoom_field().text() == "30.6%");
    REQUIRE(zooms.empty());
    bar.get_zoom_field().setText("200");
    Q_EMIT bar.get_zoom_field().editingFinished();
    REQUIRE(zooms == std::vector<double>({2}));
    REQUIRE(bar.get_zoom_field().text() == "200%");
    bar.get_zoom_field().setText("big");
    Q_EMIT bar.get_zoom_field().editingFinished();
    REQUIRE(zooms.size() == 1);
    REQUIRE(bar.get_zoom_field().text() == "200%");
    REQUIRE(AppBar::ZOOM_PRESETS ==
      std::to_array({0.25, 0.33, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0}));
  }

  TEST_CASE("zoom_field_focus") {
    auto bar = AppBar();
    auto zooms = std::vector<double>();
    bar.set_zoom_handler([&] (auto zoom) {
      zooms.push_back(zoom);
    });
    auto releases = 0;
    bar.set_release_handler([&] {
      ++releases;
    });
    bar.set_zoom(0.306);
    auto& field = bar.get_zoom_field();
    auto focus_in = QFocusEvent(QEvent::FocusIn, Qt::MouseFocusReason);
    QCoreApplication::sendEvent(&field, &focus_in);
    QCoreApplication::processEvents();
    REQUIRE(field.text() == "30.6");
    REQUIRE(field.selectedText() == "30.6");
    field.setText("50");
    auto enter = QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&field, &enter);
    REQUIRE(!zooms.empty());
    REQUIRE(zooms.back() == 0.5);
    REQUIRE(releases == 1);
    auto focus_out = QFocusEvent(QEvent::FocusOut, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(&field, &focus_out);
    QCoreApplication::processEvents();
    REQUIRE(field.text() == "50%");
    field.setText("75");
    auto escape = QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&field, &escape);
    REQUIRE(field.text() == "50%");
    REQUIRE(zooms.back() == 0.5);
    REQUIRE(releases == 2);
  }

  TEST_CASE("workspaces") {
    auto bar = AppBar();
    auto workspaces = std::vector<Workspace>();
    bar.set_workspace_handler([&] (auto workspace) {
      workspaces.push_back(workspace);
    });
    REQUIRE(bar.get_workspace() == Workspace::DESIGN);
    REQUIRE(bar.get_workspace_button(Workspace::DESIGN).isChecked());
    bar.get_workspace_button(Workspace::PROTOTYPE).click();
    REQUIRE(bar.get_workspace() == Workspace::PROTOTYPE);
    REQUIRE(!bar.get_workspace_button(Workspace::DESIGN).isChecked());
    REQUIRE(bar.get_workspace_button(Workspace::PROTOTYPE).isChecked());
    bar.set_workspace(Workspace::SHARE);
    REQUIRE(bar.get_workspace_button(Workspace::SHARE).isChecked());
    REQUIRE(workspaces == std::vector<Workspace>({Workspace::PROTOTYPE}));
  }

  TEST_CASE("title") {
    auto bar = AppBar();
    bar.set_title("shapes", false);
    REQUIRE(bar.get_title() == "shapes");
    bar.set_title("shapes", true);
    REQUIRE(bar.get_title() == "shapes *");
  }
  TEST_CASE("narrow") {
    auto bar = AppBar();
    bar.set_title("svgcalls2", false);
    bar.resize(2000, AppBar::HEIGHT);
    bar.show();
    QCoreApplication::processEvents();
    auto field = bar.findChild<QLineEdit*>("zoom_field");
    REQUIRE(field);
    REQUIRE(field->isVisible());
    REQUIRE(bar.minimumSize().width() == 0);
    bar.resize(500, AppBar::HEIGHT);
    QCoreApplication::processEvents();
    REQUIRE(!field->isVisible());
    auto title = bar.findChild<QLabel*>("document_title");
    REQUIRE(title);
    auto design = bar.get_workspace_button(Workspace::SHARE).mapTo(&bar,
      QPoint(bar.get_workspace_button(Workspace::SHARE).width(), 0)).x();
    REQUIRE(title->mapTo(&bar, QPoint(0, 0)).x() > design);
    bar.resize(2000, AppBar::HEIGHT);
    QCoreApplication::processEvents();
    REQUIRE(field->isVisible());
  }
}

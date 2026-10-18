#include <stdexcept>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenuBar>
#include "Rellu/Ui/ActionRegistry.hpp"

using namespace Rellu;

namespace {
  const auto NONE = QKeySequence();

  std::vector<std::string> describe(const QWidget& menu) {
    auto items = std::vector<std::string>();
    for(auto action : menu.actions()) {
      if(action->isSeparator()) {
        items.push_back("-");
      } else if(action->menu()) {
        items.push_back(action->text().toStdString() + ">");
      } else {
        items.push_back(action->text().toStdString());
      }
    }
    return items;
  }

  QMenu* find_menu(const QWidget& parent, const QString& title) {
    for(auto action : parent.actions()) {
      if(action->menu() && action->text() == title) {
        return action->menu();
      }
    }
    return nullptr;
  }
}

TEST_SUITE("ActionRegistry") {
  TEST_CASE("add") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    auto& open = registry.add(
      ActionDefinition("file.open", "Open", QKeySequence("Ctrl+O")));
    registry.add(ActionDefinition("file.exit", "Exit"));
    REQUIRE(open.text() == "Open");
    REQUIRE(open.shortcut() == QKeySequence("Ctrl+O"));
    REQUIRE(registry.find("file.open") == &open);
    REQUIRE(&registry.get("file.open") == &open);
    REQUIRE(!registry.find("file.save"));
    REQUIRE_THROWS_AS(registry.get("file.save"), std::out_of_range);
    REQUIRE(registry.get_names() ==
      std::vector<std::string>({"file.open", "file.exit"}));
    REQUIRE(owner.actions().contains(&open));
  }

  TEST_CASE("duplicate_names") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    registry.add(ActionDefinition("edit.undo", "Undo"));
    REQUIRE_THROWS_AS(registry.add(ActionDefinition("edit.undo", "Again")),
      std::invalid_argument);
    registry.add(MenuDefinition("edit", "Edit"));
    REQUIRE_THROWS_AS(registry.add(MenuDefinition("edit", "Again")),
      std::invalid_argument);
    REQUIRE(registry.get_names() == std::vector<std::string>({"edit.undo"}));
  }

  TEST_CASE("enabled_state") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    auto& undo = registry.add(ActionDefinition("edit.undo", "Undo"));
    REQUIRE(!undo.isEnabled());
    registry.set_handler("edit.undo", [] {});
    REQUIRE(undo.isEnabled());
    registry.set_enabled("edit.undo", false);
    REQUIRE(!undo.isEnabled());
    registry.set_handler("edit.undo", [] {});
    REQUIRE(!undo.isEnabled());
    registry.set_enabled("edit.undo", true);
    REQUIRE(undo.isEnabled());
    registry.set_handler("edit.undo", nullptr);
    REQUIRE(!undo.isEnabled());
    REQUIRE_THROWS_AS(registry.set_enabled("edit.redo", true),
      std::out_of_range);
  }

  TEST_CASE("trigger") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    registry.add(ActionDefinition("edit.copy", "Copy"));
    auto count = 0;
    REQUIRE(!registry.trigger("edit.copy"));
    registry.set_handler("edit.copy", [&] { ++count; });
    REQUIRE(registry.trigger("edit.copy"));
    REQUIRE(count == 1);
    registry.get("edit.copy").trigger();
    REQUIRE(count == 2);
    registry.set_enabled("edit.copy", false);
    REQUIRE(!registry.trigger("edit.copy"));
    REQUIRE(count == 2);
  }

  TEST_CASE("checkable") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    auto& panel = registry.add(
      ActionDefinition("view.layers", "Layers", NONE, "", "", true));
    auto checks = std::vector<bool>();
    registry.set_handler("view.layers", [&] {
      checks.push_back(panel.isChecked());
    });
    REQUIRE(panel.isCheckable());
    registry.trigger("view.layers");
    registry.trigger("view.layers");
    REQUIRE(checks == std::vector<bool>({true, false}));
  }

  TEST_CASE("shortcuts") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    auto& zoom = registry.add(
      ActionDefinition("view.zoom_in", "Zoom In", QKeySequence("Ctrl+=")));
    zoom.setShortcut(QKeySequence("Ctrl+Up"));
    REQUIRE(zoom.shortcut() == QKeySequence("Ctrl+Up"));
    REQUIRE(registry.get_default_shortcut("view.zoom_in") ==
      QKeySequence("Ctrl+="));
    REQUIRE_THROWS_AS(
      registry.get_default_shortcut("view.zoom_out"), std::out_of_range);
  }

  TEST_CASE("remove") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    registry.add(ActionDefinition("file.recent.0", "a.xd"));
    registry.add(ActionDefinition("file.recent.1", "b.xd"));
    registry.remove("file.recent.0");
    registry.remove("file.recent.2");
    REQUIRE(!registry.find("file.recent.0"));
    REQUIRE(registry.get_names() ==
      std::vector<std::string>({"file.recent.1"}));
    REQUIRE(owner.actions().size() == 1);
    registry.add(ActionDefinition("file.recent.0", "c.xd"));
    REQUIRE(registry.get_names() ==
      std::vector<std::string>({"file.recent.1", "file.recent.0"}));
  }

  TEST_CASE("remove_from_handler") {
    auto owner = QWidget();
    auto registry = ActionRegistry(owner);
    registry.add(ActionDefinition("file.recent.0", "a.xd"));
    auto count = 0;
    registry.set_handler("file.recent.0", [&] {
      ++count;
      registry.remove("file.recent.0");
      registry.add(ActionDefinition("file.recent.0", "a.xd"));
    });
    REQUIRE(registry.trigger("file.recent.0"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(count == 1);
    REQUIRE(registry.find("file.recent.0"));
    REQUIRE(!registry.find("file.recent.0")->isEnabled());
  }

  TEST_CASE("menus") {
    auto window = QMainWindow();
    auto registry = ActionRegistry(window);
    registry.add(MenuDefinition("file", "File"));
    registry.add(ActionDefinition("file.open", "Open", NONE, "file", "open"));
    registry.add(MenuDefinition("file.recent", "Recent", "file", "open"));
    registry.add(ActionDefinition("file.exit", "Exit", NONE, "file", "exit"));
    registry.add(
      ActionDefinition("file.recent.clear", "Clear", NONE, "file.recent"));
    registry.add(MenuDefinition("edit", "Edit"));
    registry.add(
      ActionDefinition("edit.undo", "Undo", NONE, "edit", "history"));
    registry.add(
      ActionDefinition("edit.copy", "Copy", NONE, "edit", "clipboard"));
    registry.add(
      ActionDefinition("edit.redo", "Redo", NONE, "edit", "history"));
    registry.add(ActionDefinition("hidden", "Hidden"));
    registry.add(MenuDefinition("view", "View"));
    registry.build(*window.menuBar());
    REQUIRE(describe(*window.menuBar()) ==
      std::vector<std::string>({"File>", "Edit>", "View>"}));
    auto file = find_menu(*window.menuBar(), "File");
    REQUIRE(file);
    REQUIRE(describe(*file) ==
      std::vector<std::string>({"Open", "Recent>", "-", "Exit"}));
    auto recent = find_menu(*file, "Recent");
    REQUIRE(recent);
    REQUIRE(describe(*recent) == std::vector<std::string>({"Clear"}));
    auto edit = find_menu(*window.menuBar(), "Edit");
    REQUIRE(describe(*edit) ==
      std::vector<std::string>({"Undo", "Redo", "-", "Copy"}));
    auto view = find_menu(*window.menuBar(), "View");
    REQUIRE(describe(*view).empty());
    REQUIRE(!view->menuAction()->isEnabled());
    registry.add(ActionDefinition("view.fit", "Fit", NONE, "view", "zoom"));
    registry.remove("edit.copy");
    view = find_menu(*window.menuBar(), "View");
    REQUIRE(describe(*view) == std::vector<std::string>({"Fit"}));
    REQUIRE(view->menuAction()->isEnabled());
    edit = find_menu(*window.menuBar(), "Edit");
    REQUIRE(describe(*edit) == std::vector<std::string>({"Undo", "Redo"}));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

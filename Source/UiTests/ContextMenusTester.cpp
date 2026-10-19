#include <memory>
#include <doctest/doctest.h>
#include <QWidget>
#include "Rellu/Ui/ContextMenus.hpp"
#include "Rellu/Ui/XdMenus.hpp"

using namespace Rellu;

namespace {
  QStringList get_texts(const QMenu& menu) {
    auto texts = QStringList();
    for(auto action : menu.actions()) {
      if(action->isSeparator()) {
        texts.push_back("-");
      } else if(auto submenu = action->menu()) {
        texts.push_back(submenu->title() + " >");
      } else {
        texts.push_back(action->text());
      }
    }
    return texts;
  }
}

TEST_SUITE("ContextMenus") {
  TEST_CASE("canvas") {
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto empty = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::CANVAS, nullptr));
    REQUIRE(empty->isEmpty());
    for(auto name : {"edit.copy", "edit.paste", "edit.delete",
        "edit.rename", "object.group", "arrange.bring_forward",
        "object.align.left"}) {
      actions.set_handler(name, [] {});
    }
    auto menu = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::CANVAS, nullptr));
    REQUIRE(get_texts(*menu) == QStringList({"Copy", "Paste", "-", "Delete",
      "-", "Group", "-", "Arrange >", "Align >"}));
    auto layers = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::LAYERS, nullptr));
    REQUIRE(get_texts(*layers) == QStringList({"Copy", "Paste", "-",
      "Delete", "-", "Rename", "-", "Group", "-", "Arrange >", "Align >"}));
  }

  TEST_CASE("select_all") {
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    actions.set_handler("edit.select_all", [] {});
    auto menu = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::CANVAS, nullptr));
    REQUIRE(get_texts(*menu) == QStringList({"Select All"}));
  }
}

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
    for(auto name : {"edit.cut", "edit.copy", "edit.paste",
        "edit.paste_appearance", "edit.delete", "edit.rename",
        "edit.duplicate", "object.toggle_lock", "object.toggle_visibility",
        "object.group", "object.ungroup", "object.path.outline_stroke",
        "object.make_component", "object.add_color", "object.mark_for_export",
        "arrange.bring_to_front", "arrange.bring_forward",
        "arrange.send_backward", "arrange.send_to_back",
        "object.align_to_pixel_grid", "object.align.left"}) {
      actions.set_handler(name, [] {});
    }
    auto menu = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::CANVAS, nullptr));
    REQUIRE(get_texts(*menu) == QStringList({"Cut", "Copy", "Paste",
      "Paste Appearance", "Delete", "-", "Lock", "Hide", "Group", "Ungroup",
      "Outline Stroke", "-", "Make Component", "-", "Add Color to Assets",
      "-", "Mark for Export", "-", "Bring to Front", "Bring Forward",
      "Send Backward", "Send to Back", "-", "Align to Pixel Grid"}));
    auto layers = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::LAYERS, nullptr));
    REQUIRE(get_texts(*layers) == QStringList({"Copy", "Paste",
      "Paste Appearance", "Duplicate", "Delete", "Rename", "-", "Lock",
      "Hide", "Group", "Ungroup", "Outline Stroke", "-", "Make Component",
      "-", "Add Color to Assets", "-", "Mark for Export", "-",
      "Export Batch...", "Export Selected..."}));
    REQUIRE(!layers->actions().back()->isEnabled());
  }

  TEST_CASE("paste_only") {
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    actions.set_handler("edit.paste", [] {});
    auto menu = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::CANVAS, nullptr));
    REQUIRE(get_texts(*menu) == QStringList({"Paste"}));
  }

  TEST_CASE("nothing_enabled") {
    auto window = QWidget();
    auto actions = ActionRegistry(window);
    add_xd_menus(actions);
    auto menu = std::unique_ptr<QMenu>(
      make_context_menu(actions, ContextMenuKind::CANVAS, nullptr));
    REQUIRE(get_texts(*menu).mid(0, 4) ==
      QStringList({"Cut", "Copy", "Copy SVG Code", "Paste"}));
    for(auto action : menu->actions()) {
      REQUIRE((action->isSeparator() || !action->isEnabled()));
    }
  }
}

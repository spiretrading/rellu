#include <initializer_list>
#include <utility>
#include "Rellu/Ui/ContextMenus.hpp"

using namespace Rellu;

namespace {
  using Names = std::initializer_list<const char*>;

  void add_actions(
      const ActionRegistry& actions, QMenu& menu, Names names) {
    auto is_added = false;
    for(auto name : names) {
      if(auto action = actions.find(name); action && action->isEnabled() &&
          action->isVisible()) {
        if(!is_added && !menu.isEmpty()) {
          menu.addSeparator();
        }
        menu.addAction(action);
        is_added = true;
      }
    }
  }

  void add_submenu(const ActionRegistry& actions, QMenu& menu,
      const QString& title, Names names) {
    auto submenu = new QMenu(title, &menu);
    add_actions(actions, *submenu, names);
    if(submenu->isEmpty()) {
      delete submenu;
      return;
    }
    menu.addMenu(submenu);
  }
}

QMenu* Rellu::make_context_menu(
    const ActionRegistry& actions, ContextMenuKind kind, QWidget* parent) {
  auto menu = new QMenu(parent);
  menu->setObjectName("context_menu");
  add_actions(actions, *menu, {"edit.cut", "edit.copy", "edit.paste",
    "edit.paste_appearance"});
  add_actions(actions, *menu, {"edit.duplicate", "edit.delete"});
  if(kind == ContextMenuKind::LAYERS) {
    add_actions(actions, *menu, {"edit.rename"});
  }
  add_actions(actions, *menu,
    {"object.toggle_lock", "object.toggle_visibility"});
  add_actions(actions, *menu, {"object.group", "object.ungroup"});
  add_actions(actions, *menu, {"object.make_component",
    "object.edit_main_component", "object.reset_main_state"});
  add_actions(actions, *menu, {"object.mark_for_export"});
  add_actions(actions, *menu, {"object.add_color",
    "object.add_character_style"});
  add_actions(actions, *menu, {"object.mask", "object.repeat_grid"});
  auto count = menu->actions().size();
  menu->addSeparator();
  add_submenu(actions, *menu, "Arrange", {"arrange.bring_to_front",
    "arrange.bring_forward", "arrange.send_backward", "arrange.send_to_back"});
  add_submenu(actions, *menu, "Align", {"object.align.left",
    "object.align.center", "object.align.right", "object.align.top",
    "object.align.middle", "object.align.bottom"});
  add_submenu(actions, *menu, "Distribute", {"object.distribute.horizontally",
    "object.distribute.vertically"});
  add_submenu(actions, *menu, "Path", {"object.path.add",
    "object.path.subtract", "object.path.intersect", "object.path.exclude",
    "object.path.convert", "object.path.outline_stroke"});
  add_submenu(actions, *menu, "Transform", {
    "object.transform.flip_horizontally", "object.transform.flip_vertically",
    "object.transform.reset_3d"});
  if(menu->actions().size() == count + 1) {
    menu->removeAction(menu->actions().back());
  }
  if(kind == ContextMenuKind::CANVAS && menu->isEmpty()) {
    add_actions(actions, *menu, {"edit.select_all"});
  }
  return menu;
}

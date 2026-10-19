#ifndef RELLU_UI_CONTEXT_MENUS_HPP
#define RELLU_UI_CONTEXT_MENUS_HPP
#include <QMenu>
#include <QWidget>
#include "Rellu/Ui/ActionRegistry.hpp"

namespace Rellu {

  /** Lists the places a context menu is opened from. */
  enum class ContextMenuKind : unsigned char {

    /** The canvas. */
    CANVAS,

    /** The Layers panel. */
    LAYERS
  };

  /**
   * Makes the context menu acting on the selection from the actions of a
   * registry, as Adobe XD's menus repeat the Edit and Object commands that
   * apply: the clipboard, duplicate and delete, lock and hide, group, the
   * component commands, Mark for Export, the asset commands, masks and
   * repeat grids, then the Arrange, Align, Distribute, Path and Transform
   * submenus. Only the enabled actions are listed; the Layers panel's menu
   * also lists Rename.
   * @param actions The registry holding Adobe XD's menus.
   * @param kind The place the menu is opened from.
   * @param parent The menu's parent.
   * @return The menu, which may be empty.
   */
  QMenu* make_context_menu(
    const ActionRegistry& actions, ContextMenuKind kind, QWidget* parent);
}

#endif

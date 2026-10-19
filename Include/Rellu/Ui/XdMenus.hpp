#ifndef RELLU_UI_XD_MENUS_HPP
#define RELLU_UI_XD_MENUS_HPP
#include <QKeySequence>
#include <QString>
#include "Rellu/Ui/ActionRegistry.hpp"

namespace Rellu {

  /**
   * Adds Adobe XD's menus to a registry: File, Edit, Object, Plugins, View,
   * Window and Help with every item, accelerator and separator in XD's
   * order, each action without a handler and so disabled until one is set.
   * @param actions The registry to add to.
   */
  void add_xd_menus(ActionRegistry& actions);

  /**
   * Formats a shortcut the way Adobe XD's menus show it, such as
   * <code>Shift+Ctrl+Z</code>.
   * @param shortcut The shortcut.
   */
  QString to_xd_shortcut_text(const QKeySequence& shortcut);
}

#endif

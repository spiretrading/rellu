#ifndef RELLU_UI_XD_MENUS_HPP
#define RELLU_UI_XD_MENUS_HPP
#include <QKeySequence>
#include <QString>
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"

namespace Rellu {

  /**
   * Adds Adobe XD's menus to a registry: File, Edit, Object, Plugins, View,
   * Window and Help with every item, accelerator and separator in XD's
   * order, each action without a handler and so disabled until one is set.
   * @param actions The registry to add to.
   */
  void add_xd_menus(ActionRegistry& actions);

  /**
   * Enables the actions acting on a document only while one is open, as
   * Adobe XD shows no editor without a document. While none is open, every
   * action but those making, opening and closing documents, opening recent
   * files, setting preferences and offering help is disabled. While one is
   * open, the zoom, panel and preview actions and the actions of the
   * workspace's canvas tools, named <code>tool.</code> followed by the
   * tool's name, are enabled, the Prototype and Share workspaces having
   * only the select and zoom tools.
   * @param actions The registry holding the actions.
   * @param workspace The current workspace.
   * @param has_document Whether a document is open.
   */
  void update_document_actions(
    ActionRegistry& actions, Workspace workspace, bool has_document);

  /**
   * Formats a shortcut the way Adobe XD's menus show it, such as
   * <code>Shift+Ctrl+Z</code>.
   * @param shortcut The shortcut.
   */
  QString to_xd_shortcut_text(const QKeySequence& shortcut);
}

#endif

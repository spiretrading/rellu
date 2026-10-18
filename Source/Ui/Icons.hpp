#ifndef RELLU_UI_ICONS_HPP
#define RELLU_UI_ICONS_HPP
#include <QIcon>
#include "Rellu/Model/Node.hpp"

namespace Rellu {

  /**
   * Returns the icon of a kind of node.
   * @param kind The kind of node displayed.
   * @param is_reference Whether the node is a reference.
   */
  const QIcon& get_kind_icon(NodeKind kind, bool is_reference);

  /**
   * Returns the icon showing whether a node is visible.
   * @param is_visible Whether the node is visible.
   */
  const QIcon& get_visibility_icon(bool is_visible);

  /** Returns the icon showing that a node is locked. */
  const QIcon& get_lock_icon();
}

#endif

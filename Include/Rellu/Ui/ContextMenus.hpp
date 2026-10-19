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
   * registry as a flat list in Adobe XD's order: Cut (canvas), Copy, Copy
   * SVG Code, Paste, Paste Appearance, Duplicate (Layers), Delete, Rename
   * (Layers) | Reset 3D Transforms | Lock, Hide, Group, Mask with Shape,
   * Ungroup, Outline Stroke | the component commands | the asset commands |
   * Mark for Export | then on the canvas the Arrange commands | Align to
   * Pixel Grid, in the Layers panel Export Batch... and Export Selected....
   * Only the enabled actions are listed, along with Paste and the export
   * commands; when none is enabled, the common commands are listed
   * disabled.
   * @param actions The registry holding Adobe XD's menus.
   * @param kind The place the menu is opened from.
   * @param parent The menu's parent.
   * @return The menu.
   */
  QMenu* make_context_menu(
    const ActionRegistry& actions, ContextMenuKind kind, QWidget* parent);
}

#endif

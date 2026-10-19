#ifndef RELLU_UI_COLORS_HPP
#define RELLU_UI_COLORS_HPP
#include <QColor>

namespace Rellu::Colors {

  /** The color behind the documents on the canvas. */
  inline const auto PASTEBOARD = QColor(0xE4, 0xE4, 0xE4);

  /** The color of the menu row. */
  inline const auto MENU_ROW = QColor(0xE4, 0xE4, 0xE4);

  /** The color of the panels, the toolbar and the app bar. */
  inline const auto PANEL = QColor(0xF5, 0xF5, 0xF5);

  /** The color of the rules separating regions and sections. */
  inline const auto RULE = QColor(0xE4, 0xE4, 0xE4);

  /** The color of the borders of buttons and fields. */
  inline const auto BORDER = QColor(0xDD, 0xDD, 0xDD);

  /** The color of the underline of numeric fields. */
  inline const auto UNDERLINE = QColor(0xCA, 0xCA, 0xCA);

  /** The color of buttons. */
  inline const auto BUTTON = QColor(0xFA, 0xFA, 0xFA);

  /** The color of text. */
  inline const auto TEXT = QColor(0x3F, 0x3F, 0x3F);

  /** The color of secondary text, such as labels and section headers. */
  inline const auto SECONDARY_TEXT = QColor(0x8E, 0x8E, 0x8E);

  /** The color of disabled text and icons. */
  inline const auto DISABLED_TEXT = QColor(0xB8, 0xB8, 0xB8);

  /** The color of icons. */
  inline const auto ICON = QColor(0x66, 0x66, 0x66);

  /** The color of the active tool's icon and of toggled icons. */
  inline const auto ACTIVE = QColor(0x14, 0x92, 0xE6);

  /** The color of the selection on the canvas and of highlighted items. */
  inline const auto SELECTION = QColor(0x26, 0x80, 0xEB);

  /** The color of selected layer rows. */
  inline const auto SELECTED_ROW = QColor(0xE0, 0xF0, 0xFA);

  /** The color of hovered rows and buttons. */
  inline const auto HOVER = QColor(0xEA, 0xEA, 0xEA);

  /** The color of the menu bar item whose menu is open. */
  inline const auto MENU_PRESSED = QColor(0xB6, 0xB6, 0xB6);

  /** The color of the separators in menus. */
  inline const auto MENU_SEPARATOR = QColor(0xE1, 0xE1, 0xE1);

  /** The color of check boxes. */
  inline const auto CHECK = QColor(0x6E, 0x6E, 0x6E);

  /** The color of the application mark. */
  inline const auto MARK = QColor(0x2B, 0x2F, 0x4A);
}

#endif

#ifndef RELLU_UI_THEME_HPP
#define RELLU_UI_THEME_HPP
#include <QApplication>
#include <QColor>
#include <QPalette>
#include "Rellu/Model/Document.hpp"

namespace Rellu {

  /** Returns the palette of the application's light theme. */
  QPalette make_light_palette();

  /**
   * Returns the color of the canvas behind a document: the pasteboard's
   * grey while the document has an artboard, white otherwise.
   * @param document The document shown on the canvas.
   */
  QColor get_canvas_color(const Document& document);

  /**
   * Applies the light theme to an application.
   * @param application The application to style.
   */
  void apply_theme(QApplication& application);
}

#endif

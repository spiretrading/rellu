#ifndef RELLU_UI_THEME_HPP
#define RELLU_UI_THEME_HPP
#include <QApplication>
#include <QPalette>

namespace Rellu {

  /** Returns the palette of the application's light theme. */
  QPalette make_light_palette();

  /**
   * Applies the light theme to an application.
   * @param application The application to style.
   */
  void apply_theme(QApplication& application);
}

#endif

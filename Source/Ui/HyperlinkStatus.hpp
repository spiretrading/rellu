#ifndef RELLU_UI_HYPERLINK_STATUS_HPP
#define RELLU_UI_HYPERLINK_STATUS_HPP
#include <QLineEdit>
#include <QString>
#include <QWidget>
#include "Rellu/Ui/TextPropertyEditors.hpp"

namespace Rellu {

  /**
   * Returns the message Adobe XD shows below a hyperlink field, translated,
   * empty for a URL it accepts.
   * @param validity How the URL is judged.
   */
  QString get_hyperlink_message(HyperlinkValidity validity);

  /**
   * Shows beside a hyperlink field, once its URL is entered or set, a green
   * check while check_hyperlink accepts it and otherwise a warning, an
   * orange underline and get_hyperlink_message below the field, nothing
   * while it is empty, and the URL as the field's tooltip.
   * @param row The row holding the field, laid out horizontally.
   * @param field The hyperlink field.
   * @return The widget holding the row and the message.
   */
  QWidget* add_hyperlink_status(QWidget* row, QLineEdit& field);
}

#endif

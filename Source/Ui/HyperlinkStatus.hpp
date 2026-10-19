#ifndef RELLU_UI_HYPERLINK_STATUS_HPP
#define RELLU_UI_HYPERLINK_STATUS_HPP
#include <QLineEdit>
#include <QWidget>
#include "Rellu/Ui/TextPropertyEditors.hpp"

namespace Rellu {

  /** The message Adobe XD shows for a hyperlink it does not accept. */
  inline constexpr auto HYPERLINK_PROTOCOL_MESSAGE =
    "URL protocol must be https:// or mailto:";

  /**
   * Shows beside a hyperlink field, once its URL is entered or set, a green
   * check while is_valid_hyperlink accepts it and a warning with
   * HYPERLINK_PROTOCOL_MESSAGE below the field otherwise, nothing while it
   * is empty, and the URL as the field's tooltip.
   * @param row The row holding the field, laid out horizontally.
   * @param field The hyperlink field.
   * @return The widget holding the row and the message.
   */
  QWidget* add_hyperlink_status(QWidget* row, QLineEdit& field);
}

#endif

#ifndef RELLU_UI_FIELD_KEYS_HPP
#define RELLU_UI_FIELD_KEYS_HPP
#include <functional>
#include <QWidget>

namespace Rellu {

  /**
   * Gives an inspector's field Adobe XD's keys: Enter and Tab apply the
   * value typed and give up the keyboard focus, and Escape restores the
   * value last applied and gives up the keyboard focus. A value the field
   * does not accept is restored rather than applied.
   * @param field The field, a line edit or an editable combo box.
   * @param cancel The function restoring the value last applied.
   * @param release The function called once the field gave up the focus.
   */
  void bind_field_keys(QWidget& field, std::function<void ()> cancel,
    std::function<void ()> release);
}

#endif

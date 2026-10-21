#include <utility>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QObject>
#include "FieldKeys.hpp"

using namespace Rellu;

namespace {
  bool is_entry(int key) {
    return key == Qt::Key_Return || key == Qt::Key_Enter ||
      key == Qt::Key_Tab || key == Qt::Key_Backtab;
  }

  QLineEdit* find_line(QWidget& field) {
    if(auto line = qobject_cast<QLineEdit*>(&field)) {
      return line;
    } else if(auto box = qobject_cast<QComboBox*>(&field)) {
      return box->lineEdit();
    }
    return nullptr;
  }

  class FieldKeys : public QObject {
    public:
      FieldKeys(QWidget& field, std::function<void ()> cancel,
          std::function<void ()> release)
          : QObject(&field),
            m_field(&field),
            m_cancel(std::move(cancel)),
            m_release(std::move(release)) {
        field.installEventFilter(this);
      }

    protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if(event->type() != QEvent::KeyPress &&
            event->type() != QEvent::ShortcutOverride) {
          return QObject::eventFilter(watched, event);
        }
        auto key = static_cast<QKeyEvent*>(event)->key();
        if(key != Qt::Key_Escape && !is_entry(key)) {
          return QObject::eventFilter(watched, event);
        }
        if(event->type() == QEvent::ShortcutOverride) {
          event->accept();
          return true;
        }
        if(key == Qt::Key_Escape) {
          m_cancel();
        } else if(auto line = find_line(*m_field);
            line && !line->hasAcceptableInput()) {
          m_cancel();
        }
        m_field->clearFocus();
        m_release();
        return true;
      }

    private:
      QWidget* m_field;
      std::function<void ()> m_cancel;
      std::function<void ()> m_release;
  };
}

void Rellu::bind_field_keys(QWidget& field, std::function<void ()> cancel,
    std::function<void ()> release) {
  new FieldKeys(field, std::move(cancel), std::move(release));
}

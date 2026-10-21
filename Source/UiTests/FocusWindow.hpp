#ifndef RELLU_UI_TESTS_FOCUS_WINDOW_HPP
#define RELLU_UI_TESTS_FOCUS_WINDOW_HPP
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QString>
#include <QWidget>

namespace Rellu::Tests {

  /**
   * A shown, active window laying out a stand-in for the app bar's zoom
   * field, a stand-in for the canvas and an inspector, in the order of
   * Rellu's main window.
   */
  struct FocusWindow {

    /** The window. */
    QWidget m_frame;

    /** The stand-in for the zoom field. */
    QLineEdit* m_zoom;

    /** The stand-in for the canvas. */
    QWidget* m_canvas;

    /** The number of times the inspector released the keyboard focus. */
    int m_releases;

    /**
     * Constructs a FocusWindow, showing and activating it.
     * @param inspector The inspector, taken ownership of.
     */
    explicit FocusWindow(QWidget* inspector)
        : m_zoom(new QLineEdit()),
          m_canvas(new QWidget()),
          m_releases(0) {
      auto layout = new QHBoxLayout(&m_frame);
      layout->addWidget(m_zoom);
      m_canvas->setFocusPolicy(Qt::StrongFocus);
      layout->addWidget(m_canvas);
      layout->addWidget(inspector);
      m_frame.show();
      m_frame.activateWindow();
      QApplication::processEvents();
    }

    /** Gives the keyboard focus to the canvas, as Rellu's main window does. */
    void release() {
      ++m_releases;
      m_canvas->setFocus();
    }
  };

  /**
   * Sends a key press to the widget with the keyboard focus.
   * @param key The key.
   * @param text The text the key types.
   */
  inline void press(int key, const QString& text = QString()) {
    auto event = QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(QApplication::focusWidget(), &event);
    QApplication::processEvents();
  }

  /**
   * Types text into the widget with the keyboard focus, one key at a time.
   * @param text The text.
   */
  inline void type(const QString& text) {
    for(auto character : text) {
      press(character.unicode(), QString(character));
    }
  }
}

#endif

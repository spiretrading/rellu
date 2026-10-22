#ifndef RELLU_UI_XD_ALERT_HPP
#define RELLU_UI_XD_ALERT_HPP
#include <vector>
#include <QDialog>
#include <QPushButton>
#include <QString>

namespace Rellu {

  /** The content of an alert. */
  struct XdAlertContent {

    /** The message, in bold. */
    QString m_message;

    /** The explanation beneath the message, or empty for none. */
    QString m_explanation;

    /**
     * The text beneath the explanation, in Adobe XD's link markup, its links
     * shown as plain text, or empty for none.
     */
    QString m_link;

    /** The labels of the buttons, left to right. */
    std::vector<QString> m_buttons;

    /** The index of the button that Enter and Escape press. */
    int m_default = 0;
  };

  /**
   * Adobe XD's critical alert: a panel with a red warning sign over a dimmed
   * window, its message, explanation and buttons, shown modally over its
   * parent's window.
   */
  class XdAlert : public QDialog {
    public:

      /**
       * Constructs an XdAlert.
       * @param content The content.
       * @param parent The widget whose window the alert covers.
       */
      XdAlert(XdAlertContent content, QWidget* parent);

      /** Returns the content. */
      const XdAlertContent& get_content() const;

      /** Returns the buttons, left to right. */
      const std::vector<QPushButton*>& get_buttons() const;

      /**
       * Returns the index of the button pressed, or the default button's
       * index if none was.
       */
      int get_choice() const;

    protected:
      void paintEvent(QPaintEvent* event) override;
      void showEvent(QShowEvent* event) override;
      void keyPressEvent(QKeyEvent* event) override;

    private:
      XdAlertContent m_content;
      std::vector<QPushButton*> m_buttons;
      int m_choice;
  };

  /**
   * Shows an XdAlert and waits for a button to be pressed.
   * @param content The content.
   * @param parent The widget whose window the alert covers.
   * @return The index of the button pressed.
   */
  int show_xd_alert(XdAlertContent content, QWidget* parent);
}

#endif

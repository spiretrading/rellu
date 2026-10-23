#ifndef RELLU_UI_CAPTION_BUTTONS_HPP
#define RELLU_UI_CAPTION_BUTTONS_HPP
#include <vector>
#include <QAbstractButton>
#include <QString>
#include <QWidget>

namespace Rellu {

  /** Lists the buttons of a title bar a window draws itself. */
  enum class CaptionKind : unsigned char {

    /** Minimizes the window. */
    MINIMIZE,

    /** Maximizes or restores the window. */
    MAXIMIZE,

    /** Closes the window. */
    CLOSE
  };

  /** A button of a title bar a window draws itself, as Windows draws it. */
  class CaptionButton : public QAbstractButton {
    public:

      /** The width in logical pixels. */
      static constexpr auto WIDTH = 46;

      /** The height in logical pixels. */
      static constexpr auto HEIGHT = 32;

      /**
       * Constructs a CaptionButton.
       * @param kind The kind of button.
       * @param tip The tool tip, in English.
       */
      CaptionButton(CaptionKind kind, const QString& tip);

      /** Returns <code>true</code> iff the pointer is over the button. */
      bool is_hovered() const;

    protected:
      void enterEvent(QEnterEvent* event) override;
      void leaveEvent(QEvent* event) override;
      void showEvent(QShowEvent* event) override;
      void hideEvent(QHideEvent* event) override;
      void paintEvent(QPaintEvent* event) override;

    private:
      CaptionKind m_kind;
      bool m_is_hovered;

      void reset();
  };

  /**
   * Makes the minimize, maximize and close buttons of a window drawing its
   * own title bar, acting on the window.
   * @param window The window.
   * @param parent The widget holding the buttons.
   * @return The buttons, left to right.
   */
  std::vector<CaptionButton*> make_caption_buttons(
    QWidget& window, QWidget& parent);

  /**
   * Updates the tool tip of a maximize button to the window's state.
   * @param button The maximize button.
   * @param is_maximized Whether the window is maximized.
   */
  void update_maximize_tip(QAbstractButton& button, bool is_maximized);
}

#endif

#ifndef RELLU_UI_PANE_FRAME_HPP
#define RELLU_UI_PANE_FRAME_HPP
#include <QColor>
#include <QStackedWidget>

namespace Rellu {

  /**
   * The stack of Adobe XD's left panes, sized to a whole number of device
   * pixels: its content ends where its border begins, and the border, drawn
   * in the canvas's color, runs from BORDER_WIDTH logical pixels before the
   * pane's width to the widget's right edge, which lies on the next whole
   * logical pixel.
   */
  class PaneFrame : public QStackedWidget {
    public:

      /** The width of the border in logical pixels. */
      static constexpr auto BORDER_WIDTH = 2;

      /** The color of the pane. */
      static inline const auto BACKGROUND = QColor(0xF5, 0xF5, 0xF5);

      /** The color of the border. */
      static inline const auto BORDER = QColor(0xE4, 0xE4, 0xE4);

      /**
       * Constructs a PaneFrame.
       * @param parent The parent widget.
       */
      explicit PaneFrame(QWidget* parent = nullptr);

      /** Returns the pane's width in device pixels. */
      int get_device_width() const;

      /**
       * Sets the pane's width.
       * @param width The width in device pixels.
       */
      void set_device_width(int width);

    protected:
      void paintEvent(QPaintEvent* event) override;

    private:
      int m_device_width;

      int get_border_device_width() const;
  };
}

#endif

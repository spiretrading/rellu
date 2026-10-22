#ifndef RELLU_UI_PANE_RESIZER_HPP
#define RELLU_UI_PANE_RESIZER_HPP
#include <functional>
#include <QPointer>
#include <QWidget>
#include "Rellu/Ui/PaneFrame.hpp"

namespace Rellu {

  /**
   * A band along the inside of a pane's right edge, BAND_WIDTH logical
   * pixels wide rounded to device pixels and reaching from the pane's top
   * to the bottom of the resizer's parent, the pointer over each of whose
   * device pixels always shows the same cursor,
   * that resizes the pane as Adobe XD's left pane resizes: the pointer over
   * the band shows the horizontal resize cursor, and once a drag moves
   * DEAD_ZONE device pixels or more from the press, the pane follows the
   * pointer's moves from where it crossed, live, until released.
   */
  class PaneResizer : public QWidget {
    public:

      /** The width of the band in logical pixels. */
      static constexpr auto BAND_WIDTH = 4;

      /** The distance in device pixels a drag moves before resizing. */
      static constexpr auto DEAD_ZONE = 4.0;

      /**
       * The type of function receiving the width a drag asks for.
       * @param width The width in logical pixels.
       */
      using WidthHandler = std::function<void (double width)>;

      /** The type of function returning the pane's width. */
      using WidthProvider = std::function<double ()>;

      /**
       * Constructs a PaneResizer over a pane's right edge.
       * @param pane The pane resized.
       * @param parent The widget holding the resizer, the pane's parent or
       *        one of its ancestors.
       */
      PaneResizer(PaneFrame& pane, QWidget* parent);

      /**
       * Returns <code>true</code> iff a point lies on the band's device
       * pixels.
       * @param position The point in the resizer's logical pixels.
       */
      bool is_on_band(QPointF position) const;

      /**
       * Sets the function receiving the width a drag asks for.
       * @param handler The function.
       */
      void set_width_handler(WidthHandler handler);

      /**
       * Sets the function returning the pane's width when a drag starts, by
       * default the pane widget's width.
       * @param provider The function.
       */
      void set_width_provider(WidthProvider provider);

      /** Places the band over the pane's right edge, hidden with the pane. */
      void place();

    protected:
      bool event(QEvent* event) override;
      bool eventFilter(QObject* watched, QEvent* event) override;
      void mousePressEvent(QMouseEvent* event) override;
      void mouseMoveEvent(QMouseEvent* event) override;
      void mouseReleaseEvent(QMouseEvent* event) override;
      void leaveEvent(QEvent* event) override;

    private:
      QPointer<PaneFrame> m_pane;
      WidthHandler m_handler;
      WidthProvider m_provider;
      QObject* m_enter_filter;
      bool m_is_pressed;
      bool m_is_dragging;
      bool m_has_parent_cursor;
      double m_press;
      double m_origin;
      double m_width;

      QPoint get_pane_position() const;
      void update_cursor(QPointF position);
      void press(double x);
      void drag(double x);
      void set_parent_cursor(bool is_band);
      bool handle_parent_event(QEvent& event);
  };
}

#endif

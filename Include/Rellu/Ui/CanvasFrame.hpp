#ifndef RELLU_UI_CANVAS_FRAME_HPP
#define RELLU_UI_CANVAS_FRAME_HPP
#include <QPoint>
#include <QRect>
#include <QWidget>
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Rendering/Canvas.hpp"

namespace Rellu {

  /**
   * Returns the first position at or after a position that lies on a whole
   * device pixel, or the position itself if none lies nearby.
   * @param position The position in logical pixels.
   * @param ratio The number of device pixels per logical pixel.
   */
  int align_to_device_pixels(int position, double ratio);

  /**
   * Returns the region Adobe XD treats as the canvas for a frame: the frame
   * with its edges rounded to whole device pixels.
   * @param frame The frame in logical pixels of the window.
   * @param canvas The top left corner of the canvas in logical pixels of the
   *        window.
   * @param ratio The number of device pixels per logical pixel.
   * @return The region in logical pixels relative to the canvas.
   */
  Rect get_view_region(const QRect& frame, QPoint canvas, double ratio);

  /**
   * Holds a Canvas, placing its top left corner on a whole device pixel of
   * the window so that the window shows each of the canvas's pixels once,
   * and filling the space left beside it with the canvas's background. The
   * canvas's view region is the frame with its edges rounded to whole device
   * pixels, as Adobe XD places its canvas.
   */
  class CanvasFrame : public QWidget {
    public:

      /**
       * Constructs a CanvasFrame.
       * @param canvas The canvas placed, which becomes a child.
       * @param parent The parent widget.
       */
      explicit CanvasFrame(Canvas& canvas, QWidget* parent = nullptr);

      /** Returns the canvas placed. */
      Canvas& get_canvas() const;

    protected:
      bool event(QEvent* event) override;
      void paintEvent(QPaintEvent* event) override;

    private:
      Canvas* m_canvas;

      void place();
  };
}

#endif

#ifndef RELLU_UI_PIXEL_HIT_FILTER_HPP
#define RELLU_UI_PIXEL_HIT_FILTER_HPP
#include <optional>
#include <QObject>
#include <QPointF>

class QWidget;

namespace Rellu {

  /**
   * An application event filter delivering the pointer over any device pixel
   * of a widget to that widget, including the device pixels of its last
   * logical row and column at fractional scales.
   */
  class PixelHitFilter : public QObject {
    public:

      /**
       * Constructs a PixelHitFilter.
       * @param parent The filter's parent.
       */
      explicit PixelHitFilter(QObject* parent = nullptr);

      bool eventFilter(QObject* watched, QEvent* event) override;
  };

  /**
   * Returns the point a window's widget hit test needs to reach the widget
   * owning the device pixel under a pointer, or nothing if the point already
   * reaches it.
   * @param root The window's widget.
   * @param position The pointer's position in the root's logical pixels.
   */
  std::optional<QPointF> get_pixel_hit(const QWidget& root, QPointF position);
}

#endif

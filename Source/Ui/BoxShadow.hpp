#ifndef RELLU_UI_BOX_SHADOW_HPP
#define RELLU_UI_BOX_SHADOW_HPP
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace Rellu {

  /** An image of a box's soft shadow placed for a window holding the box. */
  struct BoxShadow {

    /**
     * The image in device pixels, transparent but for the shadow, its device
     * pixel ratio unset.
     */
    QImage m_image;

    /** The box within the image in device pixels. */
    QRect m_box;

    /** The window's top left corner in logical pixels. */
    QPoint m_position;

    /** The window's size in logical pixels. */
    QSize m_size;
  };

  /** Lists the shadows Adobe XD casts around its floating boxes. */
  enum class ShadowKind {

    /** The broad shadow under a tool tip. */
    TOOL_TIP,

    /** The tight shadow around a hover card. */
    CARD
  };

  /**
   * Makes the shadow Adobe XD's flyouts cast around a box, with the window
   * placement that puts the box on whole device pixels.
   * @param box The box in device pixels, relative to its screen's origin.
   * @param ratio The ratio of device pixels to logical pixels.
   * @param kind The kind of shadow.
   * @return The shadow, its window position relative to the screen's origin.
   */
  BoxShadow make_box_shadow(
    const QRect& box, double ratio, ShadowKind kind = ShadowKind::TOOL_TIP);
}

#endif

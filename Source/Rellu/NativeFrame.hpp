#ifndef RELLU_NATIVE_FRAME_HPP
#define RELLU_NATIVE_FRAME_HPP
#include <functional>
#include <QAbstractButton>
#include <QPoint>
#include <QRect>
#include <QWidget>

namespace Rellu {

  /**
   * Tests whether a point of a window drawing its own title bar lies on the
   * title bar's empty space, which moves the window.
   * @param position The point in the window's coordinates.
   */
  using CaptionTest = std::function<bool (QPoint position)>;

  /**
   * Returns <code>true</code> iff the platform lets a window draw its own
   * title bar while keeping the platform's resizing, moving, snapping and
   * window animations.
   */
  bool is_custom_frame_supported();

  /**
   * Makes a top-level window draw its own title bar over the whole window,
   * keeping the platform's resize borders, snapping and animations, if the
   * platform supports it.
   * @param window The window.
   */
  void enable_custom_frame(QWidget& window);

  /**
   * Handles the native events of a window drawing its own title bar,
   * limiting its client area to get_minimum_client_size as Adobe XD limits
   * its window.
   * @param window The window.
   * @param message The native message.
   * @param result The result of the message, set when it is handled.
   * @param is_caption The function testing whether a point lies on the
   *        title bar's empty space.
   * @param caption_height The height of the title bar.
   * @param maximize The title bar's maximize button, reported to the
   *        platform as the window's maximize button, or
   *        <code>nullptr</code>.
   * @return <code>true</code> iff the event was handled.
   */
  bool handle_frame_event(QWidget& window, void* message, qintptr* result,
    const CaptionTest& is_caption, int caption_height,
    QAbstractButton* maximize);

  /**
   * Returns a top-level window's bounds in screen device pixels while
   * neither minimized nor maximized, or an empty QRect if the platform has
   * none.
   * @param window The window.
   */
  QRect get_normal_bounds(const QWidget& window);

  /**
   * Sets a top-level window's bounds in screen device pixels while neither
   * minimized nor maximized, keeping its state.
   * @param window The window.
   * @param bounds The bounds in screen device pixels.
   */
  void set_normal_bounds(QWidget& window, const QRect& bounds);

  /**
   * Returns the area in screen device pixels not covered by the taskbar of
   * the screen nearest to a rectangle, or an empty QRect if the platform
   * has none.
   * @param bounds The rectangle in screen device pixels.
   */
  QRect get_work_area(const QRect& bounds);

  /**
   * Returns <code>true</code> iff a native message shows a window.
   * @param message The native message.
   */
  bool is_showing(void* message);

  /**
   * Lets another process bring its window to the front, as the running
   * instance does when given a file to open.
   */
  void allow_foreground_activation();
}

#endif

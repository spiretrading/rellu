#ifndef RELLU_NATIVE_FRAME_HPP
#define RELLU_NATIVE_FRAME_HPP
#include <functional>
#include <QPoint>
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
   * Handles the native events of a window drawing its own title bar.
   * @param window The window.
   * @param message The native message.
   * @param result The result of the message, set when it is handled.
   * @param is_caption The function testing whether a point lies on the
   *        title bar's empty space.
   * @param caption_height The height of the title bar.
   * @return <code>true</code> iff the event was handled.
   */
  bool handle_frame_event(QWidget& window, void* message, qintptr* result,
    const CaptionTest& is_caption, int caption_height);
}

#endif

#ifndef RELLU_TOOLS_TOOL_HPP
#define RELLU_TOOLS_TOOL_HPP
#include <Qt>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"
#include "Rellu/Tools/ToolContext.hpp"

namespace Rellu {

  /** A pointer event over the canvas. */
  struct PointerEvent {

    /** The position in the canvas's logical units. */
    Point m_position = Point(0, 0);

    /** The button pressed or released, or none for a move. */
    Qt::MouseButton m_button = Qt::NoButton;

    /** The buttons held. */
    Qt::MouseButtons m_buttons;

    /** The modifier keys held. */
    Qt::KeyboardModifiers m_modifiers;
  };

  /** A key event over the canvas. */
  struct KeyEvent {

    /** The key, a Qt::Key. */
    int m_key = 0;

    /** The modifier keys held. */
    Qt::KeyboardModifiers m_modifiers;

    /** Whether the event repeats a key held down. */
    bool m_is_repeat = false;
  };

  /** Lists the kinds of pointer shown over the canvas. */
  enum class CursorKind : unsigned char {

    /** The standard arrow. */
    ARROW,

    /** A double arrow resizing along an angle. */
    RESIZE,

    /** A curved arrow rotating about a corner at an angle. */
    ROTATE
  };

  /** The pointer a tool shows over the canvas. */
  struct ToolCursor {

    /** The kind of pointer. */
    CursorKind m_kind = CursorKind::ARROW;

    /**
     * The angle in radians, clockwise on screen from the positive x axis,
     * along which a RESIZE pointer points or toward which the corner of a
     * ROTATE pointer lies.
     */
    double m_angle = 0;

    bool operator ==(const ToolCursor&) const = default;
  };

  /**
   * Interprets pointer and key events over the canvas as edits, and draws
   * its state over the canvas. Every handler does nothing by default.
   */
  class Tool {
    public:
      virtual ~Tool() = default;

      /**
       * Handles the press of a pointer button.
       * @param context The context worked in.
       * @param event The event.
       */
      virtual void press(ToolContext& context, const PointerEvent& event);

      /**
       * Handles a pointer move, with or without buttons held.
       * @param context The context worked in.
       * @param event The event.
       */
      virtual void move(ToolContext& context, const PointerEvent& event);

      /**
       * Handles the release of a pointer button.
       * @param context The context worked in.
       * @param event The event.
       */
      virtual void release(ToolContext& context, const PointerEvent& event);

      /**
       * Handles the second press of a double click, following the press and
       * release of the first.
       * @param context The context worked in.
       * @param event The event.
       */
      virtual void double_click(
        ToolContext& context, const PointerEvent& event);

      /**
       * Handles the pointer leaving the canvas.
       * @param context The context worked in.
       */
      virtual void leave(ToolContext& context);

      /**
       * Handles a key press.
       * @param context The context worked in.
       * @param event The event.
       * @return <code>true</code> iff the event was used.
       */
      virtual bool key_press(ToolContext& context, const KeyEvent& event);

      /**
       * Handles a key release.
       * @param context The context worked in.
       * @param event The event.
       * @return <code>true</code> iff the event was used.
       */
      virtual bool key_release(ToolContext& context, const KeyEvent& event);

      /**
       * Abandons the gesture in progress, reverting what it changed.
       * @param context The context worked in.
       */
      virtual void cancel(ToolContext& context);

      /** Returns <code>true</code> iff a gesture is in progress. */
      virtual bool is_busy() const;

      /** Returns the pointer to show. */
      virtual ToolCursor get_cursor() const;

      /**
       * Draws the tool's state.
       * @param context The context worked in.
       * @param overlay The overlay to draw into.
       */
      virtual void draw(
        const ToolContext& context, CanvasOverlay& overlay) const;

    protected:

      /** Constructs a Tool. */
      Tool() = default;

    private:
      Tool(const Tool&) = delete;
      Tool& operator =(const Tool&) = delete;
  };

  inline void Tool::press(ToolContext& context, const PointerEvent& event) {}

  inline void Tool::move(ToolContext& context, const PointerEvent& event) {}

  inline void Tool::release(ToolContext& context, const PointerEvent& event) {}

  inline void Tool::double_click(
    ToolContext& context, const PointerEvent& event) {}

  inline void Tool::leave(ToolContext& context) {}

  inline bool Tool::key_press(ToolContext& context, const KeyEvent& event) {
    return false;
  }

  inline bool Tool::key_release(ToolContext& context, const KeyEvent& event) {
    return false;
  }

  inline void Tool::cancel(ToolContext& context) {}

  inline bool Tool::is_busy() const {
    return false;
  }

  inline ToolCursor Tool::get_cursor() const {
    return ToolCursor();
  }

  inline void Tool::draw(
    const ToolContext& context, CanvasOverlay& overlay) const {}
}

#endif

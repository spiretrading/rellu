#ifndef RELLU_UI_GRADIENT_HANDLES_HPP
#define RELLU_UI_GRADIENT_HANDLES_HPP
#include <optional>
#include <QPainter>
#include <Qt>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /** Where a gradient fill lies on the canvas. */
  struct GradientFrame {

    /** The bounding box of the filled node in its local space. */
    Rect m_bounds = Rect(0, 0, 1, 1);

    /** The transform from the node's local space to the view. */
    Matrix m_to_view;
  };

  /** A change the user makes with a gradient's handles. */
  struct GradientEdit {

    /** Whether the handles took the pointer event. */
    bool m_is_handled = false;

    /** The fill to apply, or none if it is unchanged. */
    std::optional<GradientFill> m_fill;

    /** The stop to select, or none to keep the selection. */
    std::optional<int> m_stop;
  };

  /**
   * Edits a gradient fill with handles over the canvas while its color
   * picker is open, as Adobe XD does. The stops lie along a track drawn as
   * a light line over a dark one: the line from the start point to the end
   * point of a linear gradient, the line from the center of a radial
   * gradient to the edge of its ellipse, or the ellipse an angular gradient
   * sweeps around. The stops at the ends of a linear or radial track move
   * its end points, the stops between slide along it, and pressing the
   * track adds a stop of the color the gradient has there. A radial
   * gradient also shows its ellipse with a handle at the corner of its
   * bounds resizing it, Shift keeping its proportions; dragging its edge
   * stop rotates and scales it, Shift turning it in steps of fifteen
   * degrees. An angular gradient has a center handle moving it and a
   * handle beyond the track rotating it. End points snap to the edges and
   * center of the node's bounds.
   */
  class GradientHandles {
    public:

      /** The radius in logical units of a stop's handle. */
      static constexpr auto HANDLE_RADIUS = 6.0;

      /** The distance in logical units within which a handle is pressed. */
      static constexpr auto HANDLE_TOLERANCE = 8.0;

      /**
       * The distance in logical units between an angular gradient's track
       * and its rotation handle.
       */
      static constexpr auto ROTATION_HANDLE_DISTANCE = 20.0;

      /**
       * The direction in degrees an angular gradient's rotation handle
       * starts at.
       */
      static constexpr auto DEFAULT_ROTATION_HANDLE_ANGLE = -90.0;

      /** The distance in logical units within which an end point snaps. */
      static constexpr auto SNAP_DISTANCE = 4.0;

      /**
       * The distance in units of the node's local space, divided by the
       * zoom, an arrow key nudges a stop.
       */
      static constexpr auto NUDGE = 1.0;

      /**
       * The distance in units of the node's local space, divided by the
       * zoom, an arrow key nudges a stop while Shift is held.
       */
      static constexpr auto LARGE_NUDGE = 10.0;

      /**
       * Tests whether a gradient fill has handles: an angular or linear one,
       * or a radial one centered on its focal point, in units of the
       * node's bounding box.
       * @param fill The fill.
       */
      static bool is_editable(const GradientFill& fill);

      /**
       * Handles a key pressed while the handles are shown. Tab and Shift+Tab
       * select the next and the previous stop, wrapping around. Delete and
       * Backspace remove the selected stop unless it lies at an end of the
       * track, selecting the stop before it. The arrow keys, with no
       * modifier but Shift, nudge the selected stop along the node's axes by
       * NUDGE, or LARGE_NUDGE with Shift: a stop at an end of the track
       * moves that end, a linear gradient's start or end point, a radial
       * gradient's
       * center or edge, or an angular gradient's rotation, its other stops
       * keeping their places, and any other stop takes the offset of the
       * point of the track nearest its nudged position, passing the stops
       * it reaches.
       * @param fill The fill.
       * @param bounds The bounding box of the filled node in its local space.
       * @param zoom The zoom of the view.
       * @param stop The index of the selected stop.
       * @param key The key pressed.
       * @param modifiers The modifier keys held.
       * @return The change made.
       */
      static GradientEdit press_key(const GradientFill& fill,
        const Rect& bounds, double zoom, int stop, int key,
        Qt::KeyboardModifiers modifiers);

      /** Constructs GradientHandles with no drag in progress. */
      GradientHandles();

      /** Ends any drag and restores the rotation handle's direction. */
      void reset();

      /**
       * Paints the handles.
       * @param painter The painter, in the view's logical units.
       * @param fill The fill.
       * @param frame Where the fill lies.
       * @param stop The index of the selected stop.
       */
      void paint(QPainter& painter, const GradientFill& fill,
        const GradientFrame& frame, int stop) const;

      /**
       * Handles a press.
       * @param fill The fill.
       * @param frame Where the fill lies.
       * @param position The pointer's position in the view.
       * @return The change made.
       */
      GradientEdit press(
        const GradientFill& fill, const GradientFrame& frame, Point position);

      /**
       * Handles a move while pressed.
       * @param fill The fill.
       * @param frame Where the fill lies.
       * @param position The pointer's position in the view.
       * @param modifiers The modifier keys held.
       * @return The change made.
       */
      GradientEdit move(const GradientFill& fill, const GradientFrame& frame,
        Point position, Qt::KeyboardModifiers modifiers);

      /** Handles the release, ending the drag. */
      void release();

    private:
      enum class Part : unsigned char {
        NONE,
        STOP,
        START,
        END,
        CENTER,
        ROTATION,
        RESIZE
      };
      Part m_part;
      int m_stop;
      double m_rotation;
      double m_angle;
      Point m_offset;
      double m_handle_angle;
  };
}

#endif

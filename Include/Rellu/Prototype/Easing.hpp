#ifndef RELLU_PROTOTYPE_EASING_HPP
#define RELLU_PROTOTYPE_EASING_HPP
#include "Rellu/Model/Prototype.hpp"

namespace Rellu {

  /**
   * Evaluates a cubic Bezier timing curve from (0, 0) to (1, 1), as CSS's
   * cubic-bezier does.
   * @param x1 The x coordinate of the first control point, in [0, 1].
   * @param y1 The y coordinate of the first control point.
   * @param x2 The x coordinate of the second control point, in [0, 1].
   * @param y2 The y coordinate of the second control point.
   * @param time The fraction of the duration elapsed, in [0, 1].
   * @return The fraction of the change applied.
   */
  double evaluate_bezier(
    double x1, double y1, double x2, double y2, double time);

  /**
   * Returns the progress of an animation along Adobe XD's timing curve.
   * @param easing The timing curve.
   * @param time The fraction of the duration elapsed, clamped to [0, 1].
   * @return The fraction of the change applied, 0 at the start and 1 at the
   *         end, which leaves [0, 1] in between for the curves that
   *         overshoot.
   */
  double ease(Easing easing, double time);
}

#endif

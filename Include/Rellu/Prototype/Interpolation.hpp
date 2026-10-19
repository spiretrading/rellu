#ifndef RELLU_PROTOTYPE_INTERPOLATION_HPP
#define RELLU_PROTOTYPE_INTERPOLATION_HPP
#include <optional>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /**
   * Interpolates linearly between two numbers.
   * @param start The value at progress 0.
   * @param end The value at progress 1.
   * @param progress The progress, which may leave [0, 1] to extrapolate.
   */
  double interpolate(double start, double end, double progress);

  /**
   * Interpolates between two straight alpha colors in sRGB, clamping the
   * components to [0, 1].
   */
  Color interpolate(const Color& start, const Color& end, double progress);

  /**
   * Interpolates between two transforms as Adobe XD's auto-animate moves,
   * turns and scales a layer: the translations linearly, and the linear
   * parts decomposed into a rotation, taken the shorter way round, scales
   * and a shear, each interpolated linearly.
   */
  Matrix interpolate(const Matrix& start, const Matrix& end, double progress);

  /**
   * Interpolates between two fills: solid colors, or gradients of the same
   * kind with as many stops.
   * @return The fill, or <code>std::nullopt</code> if the fills do not
   *         interpolate.
   */
  std::optional<Fill> interpolate(
    const Fill& start, const Fill& end, double progress);

  /**
   * Interpolates between two paths made of the same segments, point by
   * point, as Adobe XD morphs a path whose points correspond.
   * @return The path, or <code>std::nullopt</code> if the segments differ.
   */
  std::optional<Path> interpolate(
    const Path& start, const Path& end, double progress);

  /**
   * Interpolates between two styles: the fills, the strokes when both have
   * one, and the effects when they are of the same kinds.
   */
  Style interpolate(const Style& start, const Style& end, double progress);

  /**
   * Interpolates between two payloads of the same kind as Adobe XD's
   * auto-animate does: the size, corner radii and points of shapes of the
   * same geometry, paths made of the same segments, the style of shapes and
   * the background of artboards. The rest is taken from the end.
   * @param start The payload at progress 0.
   * @param end The payload at progress 1.
   * @param progress The progress.
   */
  Payload interpolate(
    const Payload& start, const Payload& end, double progress);

  /**
   * Interpolates between two headers: the transform and the opacity, the
   * rest being taken from the end.
   */
  NodeHeader interpolate(
    const NodeHeader& start, const NodeHeader& end, double progress);
}

#endif

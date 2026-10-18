#ifndef RELLU_RENDERING_DRAW_LIST_HPP
#define RELLU_RENDERING_DRAW_LIST_HPP
#include <optional>
#include <variant>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Rect.hpp"

namespace Rellu {

  /** Paints every covered pixel with a single color. */
  struct SolidPaint {

    /** The straight alpha sRGB color painted. */
    Color m_color;
  };

  /** The ways a region can be painted. */
  using Paint = std::variant<SolidPaint>;

  /** Fills the region enclosed by a path under the path's fill rule. */
  struct FillCommand {

    /** The outline of the region, every subpath treated as closed. */
    Path m_path;

    /** The transform from the path's coordinates to document coordinates. */
    Matrix m_transform;

    /** The paint applied to the region. */
    Paint m_paint;

    /**
     * The rect in document coordinates outside of which nothing is painted,
     * or <code>std::nullopt</code> to paint the whole region. A view that
     * rotates or skews clips to the bounds of the mapped rect.
     */
    std::optional<Rect> m_clip;
  };

  /** The operations a DrawList performs. */
  using DrawCommand = std::variant<FillCommand>;

  /**
   * A sequence of operations drawn in order, each composited over the ones
   * before it using source-over blending in sRGB space.
   */
  using DrawList = std::vector<DrawCommand>;
}

#endif

#ifndef RELLU_RENDERING_DRAW_LIST_HPP
#define RELLU_RENDERING_DRAW_LIST_HPP
#include <memory>
#include <optional>
#include <variant>
#include <vector>
#include <QImage>
#include "Rellu/Geometry/BlendMode.hpp"
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Gradient.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Stroke.hpp"

namespace Rellu {

  /** Paints every covered pixel with a single color. */
  struct SolidPaint {

    /** The straight alpha sRGB color painted. */
    Color m_color;
  };

  /**
   * Paints a gradient, interpolating between the straight alpha colors of
   * its stops and padding with the end colors beyond them.
   */
  struct GradientPaint {

    /** The geometry of the gradient in its own coordinates. */
    GradientGeometry m_geometry;

    /** The colors in increasing offset order. */
    std::vector<GradientStop> m_stops;

    /**
     * The transform from the gradient's coordinates to the coordinates of
     * the path it fills.
     */
    Matrix m_transform;
  };

  /** Paints an image, extending its edge pixels beyond its bounds. */
  struct ImagePaint {

    /** The image, which must remain unchanged while it is drawn. */
    std::shared_ptr<const QImage> m_image;

    /**
     * The transform from the image's pixel coordinates, spanning its width
     * and height, to the coordinates of the path it fills.
     */
    Matrix m_transform;

    /** The opacity multiplying the image's alpha. */
    double m_opacity = 1;
  };

  /** The ways a region can be painted. */
  using Paint = std::variant<SolidPaint, GradientPaint, ImagePaint>;

  /**
   * The line a stroke follows, letting a FillCommand drawing the stroke's
   * region draw it as Adobe XD does: as a hairline when the stroke is at
   * most a pixel wide, and otherwise antialiased the way XD treats the
   * outline it strokes the line into.
   */
  struct StrokeLine {

    /**
     * The non-null line, in the same coordinates as the FillCommand's path.
     */
    std::shared_ptr<const Path> m_path;

    /** The width of the stroke. */
    double m_width;

    /** The shape ending the line's open subpaths. */
    StrokeCap m_cap;
  };

  /** Fills the region enclosed by a path under the path's fill rule. */
  struct FillCommand {

    /**
     * The non-null outline of the region, every subpath treated as closed,
     * shared with whatever else draws it.
     */
    std::shared_ptr<const Path> m_path;

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

    /**
     * The line the path's region strokes, or <code>std::nullopt</code> if
     * the region is not a stroke drawn along a line.
     */
    std::optional<StrokeLine> m_stroke;
  };

  /** A shadow cast by the content of a layer. */
  struct Shadow {

    /** The displacement of the shadow in document coordinates. */
    Point m_offset;

    /**
     * The standard deviation of the Gaussian blur softening the shadow, in
     * document units.
     */
    double m_radius;

    /** The straight alpha sRGB color of the shadow. */
    Color m_color;
  };

  /** The region a layer's content is clipped to when it is composited. */
  struct LayerMask {

    /** The non-null outline of the region. */
    std::shared_ptr<const Path> m_path;

    /** The transform from the path's coordinates to document coordinates. */
    Matrix m_transform;
  };

  /**
   * Begins a layer: the commands up to the matching EndLayerCommand draw
   * into a transparent surface of their own, which is then composited onto
   * what lies beneath. The composite applies, in order, the inner shadows,
   * the drop shadows, the blur, the mask, the opacity and the blend mode.
   */
  struct BeginLayerCommand {

    /** The opacity multiplying the layer's composite. */
    double m_opacity = 1;

    /** How the layer's composite blends with what lies beneath it. */
    BlendMode m_blend_mode = BlendMode::NORMAL;

    /** The region the composite is clipped to, or none to leave it whole. */
    std::optional<LayerMask> m_mask;

    /**
     * The shadows cast inside the content's outline, drawn atop the content
     * in order.
     */
    std::vector<Shadow> m_inner_shadows;

    /** The shadows cast beneath the content, the first drawn lowest. */
    std::vector<Shadow> m_drop_shadows;

    /**
     * The standard deviation of the Gaussian blur applied to the layer once
     * its shadows are drawn, in document units.
     */
    double m_blur = 0;

    /**
     * The rect in document coordinates outside of which nothing is
     * composited, or <code>std::nullopt</code> to composite the whole layer.
     */
    std::optional<Rect> m_clip;
  };

  /** Ends the most recently begun layer, compositing it. */
  struct EndLayerCommand {};

  /**
   * Replaces what lies beneath a region with a Gaussian blur of itself, as
   * a background blur does.
   */
  struct BackdropBlurCommand {

    /**
     * The non-null outline of the region, every subpath treated as closed.
     */
    std::shared_ptr<const Path> m_path;

    /** The transform from the path's coordinates to document coordinates. */
    Matrix m_transform;

    /** The standard deviation of the Gaussian blur, in document units. */
    double m_radius;

    /**
     * The amount in the range [-1, 1] added to each color component of the
     * blurred pixels.
     */
    double m_brightness = 0;

    /**
     * The rect in document coordinates outside of which nothing is painted,
     * or <code>std::nullopt</code> to paint the whole region.
     */
    std::optional<Rect> m_clip;
  };

  /** The operations a DrawList performs. */
  using DrawCommand = std::variant<
    FillCommand, BeginLayerCommand, EndLayerCommand, BackdropBlurCommand>;

  /**
   * A sequence of operations drawn in order, each composited over the ones
   * before it using source-over blending in sRGB space unless a layer
   * specifies otherwise.
   */
  using DrawList = std::vector<DrawCommand>;

  /**
   * The commands of a drawing in the order they are drawn, held elsewhere,
   * which draw as a DrawList holding them in the same order does.
   */
  using DrawSequence = std::vector<const DrawCommand*>;

  /**
   * Returns the sequence of a draw list's commands.
   * @param list The draw list, which must outlive the sequence.
   */
  DrawSequence to_sequence(const DrawList& list);

  inline DrawSequence to_sequence(const DrawList& list) {
    auto sequence = DrawSequence();
    sequence.reserve(list.size());
    for(auto& command : list) {
      sequence.push_back(&command);
    }
    return sequence;
  }
}

#endif

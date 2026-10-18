#ifndef RELLU_RENDERING_RENDER_PLAN_HPP
#define RELLU_RENDERING_RENDER_PLAN_HPP
#include <array>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>
#include <QImage>
#include <QMatrix4x4>
#include <QRect>
#include <QSize>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"

namespace Rellu {

  /** The width in texels of a RenderPlan's gradient ramps. */
  inline constexpr auto GRADIENT_RAMP_WIDTH = 512;

  /** The width in texels of a RenderPlan's paint table. */
  inline constexpr auto PAINT_TABLE_WIDTH = 1024;

  /**
   * The number of paint table texels describing each paint, the last
   * holding the divisor of a projective paint.
   */
  inline constexpr auto PAINT_TEXELS = 5;

  /** Lists the kinds of paint a paint table entry describes. */
  enum class PaintKind : unsigned char {

    /** A color, which needs no paint table entry. */
    SOLID,

    /** A linear gradient. */
    LINEAR,

    /** A radial gradient with a focal point. */
    RADIAL,

    /** The premultiplied colors of a texture. */
    TEXTURE,

    /** The alpha of a texture, multiplying the strip's color. */
    TEXTURE_ALPHA
  };

  /** Lists the pipelines a RenderPlan draws with. */
  enum class PlanPipeline : unsigned char {

    /**
     * Composites strips of solid colors with premultiplied source-over
     * blending.
     */
    SOLID,

    /** Composites strips with premultiplied source-over blending. */
    OVER,

    /**
     * Replaces the target under strips with the blend of a texture over a
     * backdrop texture, or with a shadow texture masked by the backdrop's
     * alpha over the backdrop.
     */
    BLEND,

    /**
     * Replaces the target under strips with a Gaussian blur of a texture
     * along one axis.
     */
    BLUR
  };

  /** Lists the ways a pass prepares its target before drawing. */
  enum class PlanClear : unsigned char {

    /** Keeps the target's content. */
    KEEP,

    /** Clears the target to transparent. */
    CLEAR,

    /** Clears the target to the background color. */
    BACKGROUND
  };

  /** Identifies the texture a draw samples through texture paints. */
  struct PlanSource {

    /** The index of an image of the plan, or -1. */
    int m_image = -1;

    /** The index of a texture of the plan, or -1. */
    int m_texture = -1;

    bool operator ==(const PlanSource&) const = default;
  };

  /** An instanced draw of consecutive strips within a pass. */
  struct PlanDraw {

    /** The pipeline drawn with. */
    PlanPipeline m_pipeline;

    /**
     * The index of the first strip, counting the scene's strips followed by
     * the plan's.
     */
    std::uint32_t m_first;

    /** The number of strips. */
    std::uint32_t m_count;

    /** The texture sampled by texture paints. */
    PlanSource m_source;

    /** The index of the texture holding a blend's backdrop, or -1. */
    int m_backdrop = -1;
  };

  /** A render pass over one target. */
  struct PlanPass {

    /** The index of the texture drawn to, or -1 for the render target. */
    int m_target;

    /** How the target is prepared. */
    PlanClear m_clear;

    /** The index of the pass's uniform block. */
    int m_uniforms;

    /** The draws in order. */
    std::vector<PlanDraw> m_draws;
  };

  /** Copies a region of one texture to the origin of another. */
  struct PlanCopy {

    /** The index of the texture copied from. */
    int m_source;

    /** The region copied, in texels of the source. */
    QRect m_region;

    /** The index of the texture copied to. */
    int m_destination;
  };

  /** A step of a RenderPlan. */
  using PlanStep = std::variant<PlanPass, PlanCopy>;

  /**
   * The values of a pass's uniform block: a projection matrix followed by
   * five vectors of four floats, the first giving the sizes of the coverage
   * atlas and the paint table and the fourth ending with the height of
   * the gradient ramps, which the renderer fills in.
   */
  using PlanUniforms = std::array<float, 36>;

  /** Specifies the conventions of the graphics API a plan targets. */
  struct PlanOptions {

    /** The correction applied to projections for the graphics API. */
    QMatrix4x4 m_correction;

    /** Whether textures rendered to store their bottom row first. */
    bool m_is_y_up = false;
  };

  /**
   * The passes, textures and data needed to draw a rasterized DrawList.
   * A list without layers or backdrop blurs draws in one pass to the render
   * target.
   */
  struct RenderPlan {

    /** The steps in order. */
    std::vector<PlanStep> m_steps;

    /**
     * The size of each intermediate RGBA8 texture, which are cleared or
     * written before they are read.
     */
    std::vector<QSize> m_textures;

    /** The strips drawn in addition to the scene's. */
    std::vector<Strip> m_strips;

    /**
     * The RGBA32F paint table, PAINT_TEXELS texels for each paint in the
     * order of the scene's paints followed by the plan's own.
     */
    std::vector<std::array<float, 4>> m_paints;

    /**
     * The RGBA8 straight alpha gradient ramps, GRADIENT_RAMP_WIDTH texels
     * per row, packing red in the least significant byte.
     */
    std::vector<std::uint32_t> m_ramps;

    /** The images sampled. */
    std::vector<std::shared_ptr<const QImage>> m_images;

    /** The uniform blocks of the passes. */
    std::vector<PlanUniforms> m_uniforms;

    /**
     * The index of the texture a region is drawn to, whose top left as
     * rendered is the region's top left pixel, or -1 if the plan draws to
     * the render target.
     */
    int m_target = -1;

    /** Returns the number of rows of gradient ramps. */
    int get_ramp_height() const;
  };

  /**
   * Plans the drawing of a draw list.
   * @param list The draw list.
   * @param scene The draw list rasterized.
   * @param view The transform from document coordinates to pixels.
   * @param size The size in pixels of the render target.
   * @param options The conventions of the graphics API.
   * @param plan The plan receiving the result, reusing its storage.
   */
  void make_render_plan(const DrawList& list, const StripScene& scene,
    const Matrix& view, QSize size, const PlanOptions& options,
    RenderPlan& plan);

  /**
   * Plans the drawing of a draw sequence.
   * @param commands The draw sequence.
   * @param scene The draw sequence rasterized.
   * @param view The transform from document coordinates to pixels.
   * @param size The size in pixels of the render target.
   * @param options The conventions of the graphics API.
   * @param plan The plan receiving the result, reusing its storage.
   */
  void make_render_plan(const DrawSequence& commands, const StripScene& scene,
    const Matrix& view, QSize size, const PlanOptions& options,
    RenderPlan& plan);

  /**
   * Plans the drawing of a region of the pixels of a draw sequence into a
   * texture of the plan. Each pixel of the region is drawn the same whatever
   * the region, provided that the region contains every pixel that the
   * backdrop blurs and blended layers outside of other layers read when
   * drawing it.
   * @param commands The draw sequence.
   * @param scene The draw sequence rasterized for the region.
   * @param view The transform from document coordinates to pixels.
   * @param size The size in pixels of the area drawn, which starts at the
   *        origin and contains the region.
   * @param region The region drawn.
   * @param options The conventions of the graphics API.
   * @param plan The plan receiving the result, reusing its storage.
   */
  void make_render_plan(const DrawSequence& commands, const StripScene& scene,
    const Matrix& view, QSize size, const QRect& region,
    const PlanOptions& options, RenderPlan& plan);

  /**
   * Evaluates a gradient's color at an offset, interpolating straight alpha
   * components between stops and padding beyond them.
   * @param stops The stops in increasing offset order.
   * @param offset The offset along the gradient.
   * @return The straight alpha color, transparent if there are no stops.
   */
  Color evaluate_gradient(const std::vector<GradientStop>& stops,
    double offset);

  /**
   * Returns the offset of a point within a radial gradient, as SVG defines
   * it, moving a focal point outside of the circle onto it.
   * @param gradient The gradient's geometry.
   * @param point The point in the gradient's coordinates.
   * @return The offset, which is not clamped.
   */
  double get_radial_offset(const RadialGradient& gradient, Point point);

  /**
   * Returns the factor by which a Gaussian blur of a standard deviation in
   * pixels is downsampled, a power of two.
   * @param radius The standard deviation in pixels.
   */
  int get_blur_downsampling(double radius);
}

#endif

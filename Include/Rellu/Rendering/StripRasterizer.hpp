#ifndef RELLU_RENDERING_STRIP_RASTERIZER_HPP
#define RELLU_RENDERING_STRIP_RASTERIZER_HPP
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <QFloat16>
#include <QSize>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"

namespace Rellu {

  /** The height in pixels of the bands that strips are rasterized in. */
  inline constexpr auto STRIP_HEIGHT = 4;

  /** The width in texels of a StripScene's coverage atlas. */
  inline constexpr auto STRIP_ATLAS_WIDTH = 4096;

  /**
   * A rectangle of pixels composited with a premultiplied color scaled by
   * each pixel's coverage. A strip is at most STRIP_HEIGHT pixels tall and
   * takes its coverage from consecutive texels of one row of the atlas, each
   * texel holding a column of the strip with its top pixel in the red
   * channel. A span has full coverage and any height.
   */
  struct Strip {

    /** The left edge in pixels. */
    float m_x;

    /** The top edge in pixels. */
    float m_y;

    /** The width in pixels. */
    float m_width;

    /** The height in pixels. */
    float m_height;

    /** The premultiplied color. */
    std::array<qfloat16, 4> m_color;

    /**
     * The atlas column of the strip's leftmost texel, or a negative value
     * for a span.
     */
    float m_column;

    /** The atlas row holding the strip's texels. */
    float m_row;
  };

  /** A draw list rasterized into strips, ready to upload. */
  struct StripScene {

    /** The strips in drawing order. */
    std::vector<Strip> m_strips;

    /**
     * The RGBA8 coverage atlas, STRIP_ATLAS_WIDTH texels per row, each
     * texel packing the coverage of a strip column from its top pixel in
     * the least significant byte.
     */
    std::vector<std::uint32_t> m_atlas;

    /** Returns the number of rows in the atlas. */
    int get_atlas_height() const;
  };

  /**
   * Rasterizes draw lists into strips on the CPU, computing the exact area
   * each path covers in every pixel along its edges.
   */
  class StripRasterizer {
    public:

      /** The largest distance in pixels between a curve and its lines. */
      static constexpr auto TOLERANCE = 1.0 / 32;

      /** Constructs a StripRasterizer using every hardware thread. */
      StripRasterizer();

      /**
       * Constructs a StripRasterizer.
       * @param thread_count The largest number of threads to rasterize with.
       */
      explicit StripRasterizer(int thread_count);

      ~StripRasterizer();

      /**
       * Rasterizes a draw list, replacing a scene's contents and reusing its
       * storage.
       * @param list The draw list to rasterize.
       * @param view The transform from document coordinates to pixels.
       * @param size The size in pixels of the region rasterized, which
       *        starts at the origin.
       * @param scene The scene receiving the strips.
       */
      void rasterize(const DrawList& list, const Matrix& view, QSize size,
        StripScene& scene);

    private:
      struct Chunk;
      int m_thread_count;
      std::vector<std::unique_ptr<Chunk>> m_chunks;

      StripRasterizer(const StripRasterizer&) = delete;
      StripRasterizer& operator =(const StripRasterizer&) = delete;
  };
}

#endif

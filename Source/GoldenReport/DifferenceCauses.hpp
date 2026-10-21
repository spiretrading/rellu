#ifndef RELLU_GOLDEN_REPORT_DIFFERENCE_CAUSES_HPP
#define RELLU_GOLDEN_REPORT_DIFFERENCE_CAUSES_HPP
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <QImage>
#include <QSize>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Rendering/DrawList.hpp"

namespace Rellu {

  /** Lists the likely causes of a pixel differing from its reference. */
  enum class DifferenceCause : unsigned char {

    /** The pixel is covered by the pixel aligned coverage of text. */
    TEXT,

    /** A visible path or mask covers the pixel partially. */
    EDGE,

    /** The pixel lies in the footprint of a shadow or a blur. */
    BLUR_SHADOW,

    /** The pixel is covered by a gradient fill. */
    GRADIENT,

    /** The pixel is covered by an image fill. */
    IMAGE,

    /** The pixel is covered fully by solid fills composited together. */
    BLENDING,

    /** No command draws the pixel. */
    UNCOVERED
  };

  /** The number of DifferenceCauses. */
  inline constexpr auto DIFFERENCE_CAUSE_COUNT = 7;

  /** Returns the name of a DifferenceCause. */
  std::string_view to_name(DifferenceCause cause);

  /** Totals the pixels differing from their references by cause. */
  struct DifferenceTally {

    /** The number of pixels differing at all. */
    std::array<std::int64_t, DIFFERENCE_CAUSE_COUNT> m_pixels = {};

    /** The number of pixels differing by more than 2. */
    std::array<std::int64_t, DIFFERENCE_CAUSE_COUNT> m_over_2 = {};

    /** The number of pixels differing by more than 16. */
    std::array<std::int64_t, DIFFERENCE_CAUSE_COUNT> m_over_16 = {};

    /** The sum of the largest channel difference of each pixel. */
    std::array<std::int64_t, DIFFERENCE_CAUSE_COUNT> m_difference = {};

    /** Adds another tally to this one. */
    DifferenceTally& operator +=(const DifferenceTally& tally);
  };

  /**
   * Returns the likely cause of a difference at each pixel of a draw list's
   * rendering, from what the commands visible at the pixel draw there.
   * @param list The draw list.
   * @param view The transform from document coordinates to pixels.
   * @param size The size in pixels of the rendering.
   * @return The cause of each pixel in rows from the top.
   */
  std::vector<DifferenceCause> classify_differences(
    const DrawList& list, const Matrix& view, QSize size);

  /**
   * Totals the pixels of a rendering differing from its reference by cause.
   * @param causes The cause of each pixel of the rendering.
   * @param actual The rendering.
   * @param expected The reference, of the same size as the rendering.
   */
  DifferenceTally tally_differences(const std::vector<DifferenceCause>& causes,
    const QImage& actual, const QImage& expected);
}

#endif

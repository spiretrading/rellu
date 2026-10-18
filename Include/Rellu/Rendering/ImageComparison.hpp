#ifndef RELLU_RENDERING_IMAGE_COMPARISON_HPP
#define RELLU_RENDERING_IMAGE_COMPARISON_HPP
#include <QImage>

namespace Rellu {

  /** Specifies how much two images may differ while still matching. */
  struct ImageTolerance {

    /**
     * The largest difference, in units of 1/255, that any channel of a pixel
     * may have while the pixel still matches.
     */
    int m_difference;

    /** The largest fraction of mismatched pixels for the images to match. */
    double m_fraction;

    /**
     * Constructs an ImageTolerance with a difference of 16 and a fraction of
     * 0.005.
     */
    ImageTolerance() noexcept;

    /**
     * Constructs an ImageTolerance.
     * @param difference The largest per channel difference of a matching
     *        pixel.
     * @param fraction The largest fraction of mismatched pixels.
     */
    ImageTolerance(int difference, double fraction) noexcept;
  };

  /**
   * Stores the result of comparing an image against an expected image. Pixels
   * are compared with premultiplied alpha. Images of different sizes never
   * match and are compared as if both were padded with transparent pixels to
   * the size that contains both.
   */
  struct ImageComparison {

    /** Whether the images match under the tolerance. */
    bool m_is_match;

    /** The number of pixels compared. */
    int m_pixels;

    /** The number of pixels with a channel beyond the tolerance. */
    int m_mismatches;

    /** The fraction of compared pixels that mismatch. */
    double m_fraction;

    /** The largest difference of any channel of any pixel. */
    int m_maximum_difference;

    /** The mean difference over every channel of every pixel. */
    double m_mean_difference;

    /**
     * An opaque image of the compared area highlighting mismatched pixels in
     * red over a faded grayscale of the expected image.
     */
    QImage m_highlight;
  };

  /**
   * Compares an image against an expected image.
   * @param actual The image to compare.
   * @param expected The image expected.
   * @param tolerance How much the images may differ while still matching.
   * @return The result of the comparison.
   */
  ImageComparison compare_images(const QImage& actual, const QImage& expected,
    const ImageTolerance& tolerance = ImageTolerance());
}

#endif

#ifndef RELLU_IO_EXPORT_FORMAT_HPP
#define RELLU_IO_EXPORT_FORMAT_HPP
#include <string_view>

namespace Rellu {

  /** Lists the file formats Adobe XD exports assets in. */
  enum class ExportFormat : unsigned char {

    /** Portable Network Graphics. */
    PNG,

    /** Scalable Vector Graphics. */
    SVG,

    /** Portable Document Format. */
    PDF,

    /** JPEG. */
    JPG
  };

  /** Lists the sets of sizes a bitmap export can write at once. */
  enum class ExportPreset : unsigned char {

    /** A single size, the export's scale. */
    NONE,

    /** The 1x and 2x sizes of the web. */
    WEB,

    /** The 1x, 2x and 3x sizes of iOS. */
    IOS,

    /** The ldpi to xxxhdpi densities of Android. */
    ANDROID
  };

  /** Lists how an SVG export styles its elements. */
  enum class SvgStyling : unsigned char {

    /** Each element carries its style as presentation attributes. */
    PRESENTATION_ATTRIBUTES,

    /** The styles are classes of a style element. */
    INTERNAL_CSS
  };

  /** Lists how an SVG export saves the images it draws. */
  enum class SvgImages : unsigned char {

    /** The images are embedded as data URLs. */
    EMBED,

    /** The images are written beside the SVG file and linked. */
    LINK
  };

  /** The options of Adobe XD's Export Assets dialog. */
  struct ExportOptions {

    /** The file format written. */
    ExportFormat m_format = ExportFormat::PNG;

    /** The scale of a bitmap export written at a single size. */
    double m_scale = 1;

    /** The set of sizes a bitmap export writes, NONE for m_scale alone. */
    ExportPreset m_preset = ExportPreset::NONE;

    /** The quality of a JPG export, in percent. */
    int m_jpg_quality = 100;

    /** How an SVG export styles its elements. */
    SvgStyling m_svg_styling = SvgStyling::PRESENTATION_ATTRIBUTES;

    /** How an SVG export saves the images it draws. */
    SvgImages m_svg_images = SvgImages::EMBED;

    /** Whether an SVG export is minified. */
    bool m_is_svg_minified = false;

    /** Whether an SVG export outlines its strokes. */
    bool m_is_svg_outlined = false;

    /** Whether a PDF export writes all of its assets into a single file. */
    bool m_is_pdf_single = true;

    bool operator ==(const ExportOptions&) const = default;
  };

  /**
   * Returns the file extension of a format, without its dot.
   * @param format The format.
   */
  std::string_view get_extension(ExportFormat format);

  /**
   * Tests whether a format is a vector format, written at a single size.
   * @param format The format.
   */
  bool is_vector(ExportFormat format);
}

#endif

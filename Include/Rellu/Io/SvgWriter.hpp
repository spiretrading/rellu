// Derived from Adobe's svgObjectModelGenerator (svgWriter), Copyright (c)
// 2014, 2015 Adobe Systems Incorporated, licensed under the Apache License 2.0
// (see Licenses/svgObjectModelGenerator-LICENSE.txt); modified (ported to C++)
// for Rellu.
#ifndef RELLU_IO_SVG_WRITER_HPP
#define RELLU_IO_SVG_WRITER_HPP
#include <string>
#include "Rellu/Io/XdValue.hpp"

namespace Rellu {

  /** Lists how Adobe XD's SVG writer styles the elements it writes. */
  enum class SvgWriterStyling : unsigned char {

    /** Classes of a style element. */
    CLASS,

    /** A style attribute on each element. */
    STYLE,

    /** Presentation attributes on each element. */
    ATTRIBUTE
  };

  /** The configuration of Adobe XD's SVG writer. */
  struct SvgWriterOptions {

    /** The number of decimal places numbers are rounded to. */
    int m_precision = 3;

    /** How the elements are styled. */
    SvgWriterStyling m_styling = SvgWriterStyling::CLASS;

    /** Whether the output is minified. */
    bool m_is_minified = false;

    /** The preserveAspectRatio of the root element. */
    std::string m_preserve_aspect_ratio = "xMidYMid";

    /** Whether lines end in a carriage return and a line feed. */
    bool m_has_carriage_returns = false;

    /**
     * Whether a drop shadow of an element painting nothing is written as a
     * shadow alone.
     */
    bool m_has_drop_shadow_only = false;
  };

  /**
   * Writes the SVG markup of an Adobe XD document of the kind its SVG export
   * serializes, as Adobe XD's SVG writer does. Throws std::runtime_error if
   * the writer fails, as it does on a transform that can not be decomposed.
   * @param document The document's JSON.
   * @param options The configuration.
   * @return The UTF-8 markup.
   */
  std::string write_svg(XdValue document, const SvgWriterOptions& options);
}

#endif

#ifndef RELLU_IO_SVG_EXPORT_HPP
#define RELLU_IO_SVG_EXPORT_HPP
#include <filesystem>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Io/ExportFormat.hpp"
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Io/XdValue.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** The functions an SVG export lays out and measures text with. */
  struct SvgTextServices {

    /**
     * The layout of text without a stored layout, or empty to write such
     * text without a layout.
     */
    XdTextLayout m_layout;

    /** The function measuring text. */
    TextMeasurer m_measurer = estimate_text_bounds;
  };

  /** An image an SVG export links to rather than embeds. */
  struct SvgLinkedImage {

    /** The name of the image's file, relative to the SVG file. */
    std::string m_name;

    /** The image. */
    ImageId m_image;
  };

  /** The markup of an SVG export and the images it links to. */
  struct SvgExport {

    /** The UTF-8 markup. */
    std::string m_markup;

    /** The images linked to, in the order they are first linked. */
    std::vector<SvgLinkedImage> m_images;
  };

  /**
   * Generates the Adobe XD JSON its SVG export serializes for nodes: the
   * nodes positioned within the union of their drawn bounds.
   * @param document The document holding the nodes.
   * @param nodes The nodes exported.
   * @param options The export's options.
   * @param name The name of the SVG file without its extension, which names
   *        the linked images.
   * @param services The functions laying out and measuring text.
   * @param linked Receives the images linked to, or null.
   * @return The JSON.
   */
  XdValue make_svg_document(const Document& document,
    const std::vector<NodeId>& nodes, const ExportOptions& options,
    std::string_view name, const SvgTextServices& services = {},
    std::vector<SvgLinkedImage>* linked = nullptr);

  /**
   * Makes the SVG markup Adobe XD exports for nodes.
   * @param document The document holding the nodes.
   * @param nodes The nodes exported.
   * @param options The export's options.
   * @param name The name of the SVG file without its extension, which names
   *        the linked images.
   * @param services The functions laying out and measuring text.
   * @return The markup and the images it links to.
   */
  SvgExport make_svg_export(const Document& document,
    const std::vector<NodeId>& nodes, const ExportOptions& options,
    std::string_view name, const SvgTextServices& services = {});

  /**
   * Makes the SVG markup Adobe XD's Copy SVG Code puts on the clipboard for
   * nodes.
   * @param document The document holding the nodes.
   * @param nodes The nodes copied.
   * @param services The functions laying out and measuring text.
   * @return The UTF-8 markup.
   */
  std::string make_svg_code(const Document& document,
    const std::vector<NodeId>& nodes, const SvgTextServices& services = {});

  /**
   * Writes a node as an SVG file as Adobe XD exports it. Throws an exception
   * derived from std::exception if the file can not be written.
   * @param document The document holding the node.
   * @param node The node exported.
   * @param path The path of the file written.
   * @param options The export's options.
   * @param services The functions laying out and measuring text.
   */
  void write_svg_export(const Document& document, const NodeId& node,
    const std::filesystem::path& path, const ExportOptions& options,
    const SvgTextServices& services = {});
}

#endif

#ifndef RELLU_IO_SVG_EXPORT_HPP
#define RELLU_IO_SVG_EXPORT_HPP
#include <filesystem>
#include "Rellu/Io/ExportFormat.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Writes a node as an SVG file as Adobe XD exports it. Throws an exception
   * derived from std::exception if the file can not be written.
   * @param document The document holding the node.
   * @param node The node exported.
   * @param path The path of the file written.
   * @param options The export's options.
   */
  void write_svg_export(const Document& document, const NodeId& node,
    const std::filesystem::path& path, const ExportOptions& options);
}

#endif

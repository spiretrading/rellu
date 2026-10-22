#ifndef RELLU_IO_PDF_EXPORT_HPP
#define RELLU_IO_PDF_EXPORT_HPP
#include <filesystem>
#include <vector>
#include <QByteArray>
#include "Rellu/Io/ExportFormat.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Orders the nodes exported to a single PDF file as Adobe XD orders its
   * pages.
   * @param document The document holding the nodes.
   * @param nodes The nodes exported.
   * @return The <i>nodes</i> in page order.
   */
  std::vector<NodeId> sort_pdf_pages(
    const Document& document, std::vector<NodeId> nodes);

  /**
   * Writes nodes as the pages of a PDF file as Adobe XD exports them, in the
   * order given.
   * @param document The document holding the nodes.
   * @param nodes The nodes, one per page.
   * @return The bytes of the PDF file.
   */
  QByteArray write_pdf(
    const Document& document, const std::vector<NodeId>& nodes);

  /**
   * Exports nodes to a PDF file as Adobe XD does, one page per node. Throws
   * std::runtime_error on failure.
   * @param document The document holding the nodes.
   * @param nodes The nodes exported.
   * @param path The path of the file written.
   * @param options The export's options.
   */
  void write_pdf_export(const Document& document,
    const std::vector<NodeId>& nodes, const std::filesystem::path& path,
    const ExportOptions& options);
}

#endif

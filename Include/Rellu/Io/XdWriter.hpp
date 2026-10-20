#ifndef RELLU_IO_XD_WRITER_HPP
#define RELLU_IO_XD_WRITER_HPP
#include <filesystem>
#include <string>
#include <QByteArray>
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Model/Document.hpp"

namespace Rellu {

  /** Specifies how an Adobe XD package is written. */
  struct XdWriteOptions {

    /**
     * The layout of text without a stored layout, or empty to write such
     * text without a layout.
     */
    XdTextLayout m_layout;

    /** The document name recorded in a document without a manifest. */
    std::string m_name = "Untitled";

    /**
     * The id Adobe XD gives the document, or empty to keep the recorded
     * one.
     */
    std::string m_id;

    /**
     * The id of the document the document derives from, or empty to keep
     * the recorded one.
     */
    std::string m_parent;
  };

  /**
   * Writes a document as an Adobe XD package: every entry is generated from
   * the model and the opaque data the import kept, so that a document read
   * from a package and left unchanged writes the same JSON, and the entries
   * the model does not represent are written as they were read. Every
   * failure throws XdError.
   * @param document The document to write.
   * @param options The options.
   * @return The bytes of the package.
   */
  QByteArray write_xd(
    const Document& document, const XdWriteOptions& options = {});

  /**
   * Writes a document as an Adobe XD file, replacing the file only once the
   * new one is complete.
   * @param document The document to write.
   * @param path The path of the .xd file.
   * @param options The options.
   */
  void write_xd(const Document& document, const std::filesystem::path& path,
    const XdWriteOptions& options = {});

  /**
   * Writes nodes with their descendants as the JSON array of Adobe XD
   * nodes they are saved as, for exchanges such as the clipboard.
   * @param document The document holding the nodes.
   * @param nodes The nodes.
   * @param options The options, of which the text layout applies.
   * @return The JSON text of the array.
   */
  std::string write_xd_nodes(const Document& document,
    const std::vector<NodeId>& nodes, const XdWriteOptions& options = {});

  /**
   * Returns the components in the order their masters are written: those
   * listed first, then the rest by id.
   * @param document The document.
   * @param order The components to write first, in order.
   */
  std::vector<ComponentId> order_symbols(
    const Document& document, const std::vector<NodeId>& order);
}

#endif

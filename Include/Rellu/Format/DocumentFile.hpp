#ifndef RELLU_FORMAT_DOCUMENT_FILE_HPP
#define RELLU_FORMAT_DOCUMENT_FILE_HPP
#include <cstdint>
#include <filesystem>
#include <istream>
#include <ostream>
#include "Rellu/Format/FormatError.hpp"
#include "Rellu/Model/Document.hpp"

namespace Rellu {

  /** The version of the Rellu document format written by save. */
  inline constexpr auto FORMAT_VERSION = std::uint32_t(1);

  /** The conventional extension of a Rellu document file. */
  inline constexpr auto FORMAT_EXTENSION = ".rellu";

  /** Specifies how a document is saved. */
  struct SaveOptions {

    /**
     * The deflate compression level of the document's structure, from 0 for
     * no compression to 9 for the smallest file.
     */
    int m_compression_level = 6;

    /** The largest number of threads used, or 0 for one per processor. */
    int m_thread_count = 0;
  };

  /**
   * Writes a document to a stream, throwing FormatError on failure.
   * @param document The document to write.
   * @param out The stream receiving the document.
   * @param options How the document is saved.
   */
  void save(const Document& document, std::ostream& out,
    const SaveOptions& options = SaveOptions());

  /**
   * Writes a document to a file, replacing any existing file only once the
   * document is completely written, throwing FormatError on failure.
   * @param document The document to write.
   * @param path The path of the file.
   * @param options How the document is saved.
   */
  void save(const Document& document, const std::filesystem::path& path,
    const SaveOptions& options = SaveOptions());

  /**
   * Reads a document from a stream into an empty document in a single
   * transaction, then discards the document's undo history. A failure
   * throws FormatError and leaves the document unchanged; a document that
   * is not empty throws std::invalid_argument.
   * @param in The stream holding the document.
   * @param document The empty document receiving the contents.
   */
  void load(std::istream& in, Document& document);

  /**
   * Reads a document from a file into an empty document in a single
   * transaction, then discards the document's undo history. A failure
   * throws FormatError and leaves the document unchanged; a document that
   * is not empty throws std::invalid_argument.
   * @param path The path of the file.
   * @param document The empty document receiving the contents.
   */
  void load(const std::filesystem::path& path, Document& document);
}

#endif

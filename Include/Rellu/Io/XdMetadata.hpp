#ifndef RELLU_IO_XD_METADATA_HPP
#define RELLU_IO_XD_METADATA_HPP
#include <string>
#include <string_view>
#include <QDateTime>

namespace Rellu {

  /** The application version the manifest of a saved package records. */
  constexpr auto XD_APPLICATION_VERSION = std::string_view("61.0.12.1");

  /** The document version the manifest of a saved package records. */
  constexpr auto XD_DOCUMENT_VERSION = std::string_view("2.33");

  /**
   * Returns a time as the XMP metadata of an Adobe XD package records it.
   * @param time The time, in the time zone it is recorded in.
   */
  std::string to_xmp_date(const QDateTime& time);

  /**
   * Returns the XMP metadata Adobe XD records when it saves a document.
   * @param metadata The XMP metadata of the file the save replaces, or empty
   *        for a save to a new file.
   * @param name The name of the document.
   * @param composite The id of the composite holding the document.
   * @param instance The id of the instance the save makes.
   * @param time The time of the save, in the time zone it is recorded in.
   * @return The XMP metadata of the saved file, the given metadata unchanged
   *         if it holds properties Adobe XD does not write.
   */
  std::string save_xd_metadata(std::string_view metadata,
    std::string_view name, std::string_view composite,
    std::string_view instance, const QDateTime& time);
}

#endif

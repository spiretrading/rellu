#ifndef RELLU_IO_PDF_FILE_HPP
#define RELLU_IO_PDF_FILE_HPP
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <QByteArray>
#include "Rellu/Geometry/Path.hpp"

namespace Rellu {

  /**
   * Formats a number as Adobe XD's PDF writer does.
   * @param value The number.
   */
  std::string to_pdf_number(double value);

  /**
   * Formats a color component or opacity with at most four decimals.
   * @param value The component in the range [0, 1].
   */
  std::string to_pdf_color(double value);

  /**
   * Formats a color component of an 8-bit color with at most three decimals.
   * @param value The component in the range [0, 1], rounded to 8 bits.
   */
  std::string to_pdf_byte_color(double value);

  /**
   * Formats a reference to an indirect object.
   * @param id The object's number.
   */
  std::string to_pdf_reference(int id);

  /**
   * Formats a name, escaping the characters PDF names can not hold.
   * @param name The name without its leading slash.
   */
  std::string to_pdf_name(std::string_view name);

  /**
   * Formats a 16-bit value as four uppercase hexadecimal digits.
   * @param value The value.
   */
  std::string to_pdf_hex(std::uint16_t value);

  /**
   * Finds the rectangle a path draws as Adobe XD's PDF writer writes it.
   * @param path The path.
   * @return The rectangle, or none if the path is written otherwise.
   */
  std::optional<Rect> find_pdf_rect(const Path& path);

  /**
   * Formats the operators constructing a path as Adobe XD's PDF writer does.
   * @param path The path.
   * @param tolerance The largest distance between an arc and the curves
   *        approximating it.
   * @param is_clip Whether the path clips rather than paints.
   */
  std::string to_pdf_path(
    const Path& path, double tolerance = 0.25, bool is_clip = false);

  /**
   * Returns the operator painting a path's interior under its fill rule.
   * @param path The path.
   */
  std::string_view get_pdf_fill(const Path& path);

  /** A PDF dictionary whose entries are written in insertion order. */
  class PdfDictionary {
    public:

      /** Constructs an empty PdfDictionary. */
      PdfDictionary() = default;

      /**
       * Constructs a PdfDictionary holding a /Type entry.
       * @param type The type's name.
       */
      explicit PdfDictionary(std::string_view type);

      /**
       * Appends an entry.
       * @param key The key without its leading slash.
       * @param value The formatted value.
       */
      void add(std::string_view key, std::string value);

      /** Returns <code>true</code> iff the dictionary has no entries. */
      bool is_empty() const;

      /** Returns the dictionary formatted. */
      std::string to_string() const;

    private:
      std::vector<std::pair<std::string, std::string>> m_entries;
  };

  /**
   * Formats an array.
   * @param values The formatted values.
   */
  std::string to_pdf_array(const std::vector<std::string>& values);

  /**
   * Compresses data with zlib's default compression.
   * @param data The data.
   */
  std::string deflate(std::string_view data);

  /** Writes the objects of a PDF file in the order they are written. */
  class PdfFile {
    public:

      /** Constructs a PdfFile holding only its header. */
      PdfFile();

      /** Reserves the next object number. */
      int reserve();

      /**
       * Writes an object.
       * @param id The object's reserved number.
       * @param object The formatted object.
       */
      void write(int id, std::string_view object);

      /**
       * Writes a stream, compressing it as Adobe XD's PDF writer does.
       * @param id The object's reserved number.
       * @param dictionary The stream's dictionary without its filter and
       *        length.
       * @param data The stream's data.
       */
      void write_stream(int id, PdfDictionary dictionary, std::string data);

      /**
       * Writes a stream as it is.
       * @param id The object's reserved number.
       * @param dictionary The stream's dictionary without its length.
       * @param data The stream's data.
       */
      void write_raw_stream(
        int id, PdfDictionary dictionary, std::string_view data);

      /**
       * Writes the cross-reference table and trailer.
       * @param root The number of the catalog.
       * @param info The number of the document information dictionary.
       * @return The bytes of the file.
       */
      QByteArray finish(int root, int info);

    private:
      std::string m_data;
      std::vector<std::size_t> m_offsets;
  };
}

#endif

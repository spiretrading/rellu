#ifndef RELLU_IO_BITMAP_EXPORT_HPP
#define RELLU_IO_BITMAP_EXPORT_HPP
#include <filesystem>
#include <QByteArray>
#include <QImage>

namespace Rellu {

  /**
   * Encodes an opaque image as a JPG as Adobe XD encodes it. Throws
   * std::runtime_error if the encoding fails.
   * @param image The image.
   * @param quality The quality in percent.
   * @return The JPG's bytes.
   */
  QByteArray encode_jpg(const QImage& image, int quality);

  /**
   * Encodes an image as a PNG as Adobe XD encodes it: 8-bit RGBA keeping the
   * image's transparency. Throws std::runtime_error if the encoding fails.
   * @param image The image.
   * @return The PNG's bytes.
   */
  QByteArray encode_png(const QImage& image);

  /**
   * Writes an image as a PNG file as Adobe XD exports it: 8-bit RGBA keeping
   * the image's transparency. Throws std::runtime_error if the file can not
   * be written.
   * @param image The image.
   * @param path The path of the file written.
   */
  void write_png_export(const QImage& image, const std::filesystem::path& path);

  /**
   * Writes an image as a JPG file as Adobe XD exports it: composited over
   * white. Throws std::runtime_error if the file can not be written.
   * @param image The image.
   * @param path The path of the file written.
   * @param quality The quality in percent.
   */
  void write_jpg_export(const QImage& image, const std::filesystem::path& path,
    int quality);
}

#endif

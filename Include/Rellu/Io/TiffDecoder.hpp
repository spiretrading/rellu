#ifndef RELLU_IO_TIFF_DECODER_HPP
#define RELLU_IO_TIFF_DECODER_HPP
#include <QByteArray>
#include <QImage>

namespace Rellu {

  /**
   * Tests whether bytes begin with the header of a TIFF file, in either byte
   * order.
   * @param bytes The bytes.
   */
  bool is_tiff(const QByteArray& bytes);

  /**
   * Decodes the first image of a baseline TIFF file: little or big endian,
   * in strips or tiles, uncompressed or compressed with LZW, PackBits or
   * Deflate, with or without horizontal differencing, holding 8 or 16-bit
   * greyscale, RGB or RGBA samples with associated or unassociated alpha,
   * palette colors or bilevel pixels.
   * @param bytes The file's bytes.
   * @return The image, or a null image if the file is not such a TIFF.
   */
  QImage decode_tiff(const QByteArray& bytes);
}

#endif

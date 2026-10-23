#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImageWriter>
#include <QPainter>
#include <QString>
#include <QtJpeg/jpeglib.h>
#include "Rellu/Io/BitmapExport.hpp"

using namespace Rellu;

namespace {
  constexpr auto PNG_SIGNATURE_SIZE = 8;

  std::uint32_t compute_crc(const char* data, qsizetype size) {
    static const auto TABLE = [] {
      auto table = std::array<std::uint32_t, 256>();
      for(auto i = std::uint32_t(0); i != 256; ++i) {
        auto value = i;
        for(auto bit = 0; bit != 8; ++bit) {
          if(value & 1) {
            value = 0xEDB88320u ^ (value >> 1);
          } else {
            value >>= 1;
          }
        }
        table[i] = value;
      }
      return table;
    }();
    auto crc = 0xFFFFFFFFu;
    for(auto i = qsizetype(0); i != size; ++i) {
      crc = TABLE[(crc ^ static_cast<std::uint8_t>(data[i])) & 0xFF] ^
        (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
  }

  void append_number(QByteArray& bytes, std::uint32_t value) {
    bytes.append(static_cast<char>(value >> 24));
    bytes.append(static_cast<char>(value >> 16));
    bytes.append(static_cast<char>(value >> 8));
    bytes.append(static_cast<char>(value));
  }

  std::uint32_t read_number(const QByteArray& bytes, qsizetype position) {
    auto byte = [&] (qsizetype offset) {
      return static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(bytes[position + offset]));
    };
    return (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
  }

  void append_chunk(
      QByteArray& bytes, const QByteArray& type, const QByteArray& data) {
    append_number(bytes, static_cast<std::uint32_t>(data.size()));
    auto body = type + data;
    bytes.append(body);
    append_number(bytes, compute_crc(body.constData(), body.size()));
  }

  QByteArray restructure_png(const QByteArray& png) {
    auto result = png.left(PNG_SIGNATURE_SIZE);
    auto position = qsizetype(PNG_SIGNATURE_SIZE);
    while(position + 12 <= png.size()) {
      auto length = static_cast<qsizetype>(read_number(png, position));
      auto type = png.mid(position + 4, 4);
      auto end = position + 12 + length;
      if(end > png.size()) {
        throw std::runtime_error("The PNG encoding is malformed.");
      }
      if(type != "pHYs") {
        result.append(png.mid(position, end - position));
      }
      if(type == "IHDR") {
        append_chunk(result, "sBIT", QByteArray(4, char(8)));
      }
      position = end;
    }
    return result;
  }

  void write_file(const QByteArray& bytes, const std::filesystem::path& path) {
    auto file = QFile(QString::fromStdU16String(path.u16string()));
    if(!file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size()) {
      throw std::runtime_error(file.errorString().toStdString());
    }
  }

  struct JpegError {
    jpeg_error_mgr m_manager;
    std::jmp_buf m_jump;
  };

  void exit_on_error(j_common_ptr info) {
    std::longjmp(reinterpret_cast<JpegError*>(info->err)->m_jump, 1);
  }

  bool compress(const QImage& image, int quality, unsigned char** buffer,
      unsigned long* size) {
    auto info = jpeg_compress_struct();
    auto error = JpegError();
    info.err = jpeg_std_error(&error.m_manager);
    error.m_manager.error_exit = exit_on_error;
    if(setjmp(error.m_jump)) {
      jpeg_destroy_compress(&info);
      return false;
    }
    jpeg_create_compress(&info);
    jpeg_mem_dest(&info, buffer, size);
    info.image_width = static_cast<JDIMENSION>(image.width());
    info.image_height = static_cast<JDIMENSION>(image.height());
    info.input_components = 3;
    info.in_color_space = JCS_RGB;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, quality, TRUE);
    info.optimize_coding = TRUE;
    jpeg_start_compress(&info, TRUE);
    while(info.next_scanline < info.image_height) {
      auto row = const_cast<JSAMPROW>(reinterpret_cast<const JSAMPLE*>(
        image.constScanLine(static_cast<int>(info.next_scanline))));
      jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);
    return true;
  }

  QByteArray encode(const QImage& image, const char* format, int quality) {
    auto bytes = QByteArray();
    auto buffer = QBuffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    auto writer = QImageWriter(&buffer, format);
    writer.setQuality(quality);
    if(!writer.write(image)) {
      throw std::runtime_error(writer.errorString().toStdString());
    }
    return bytes;
  }
}

QByteArray Rellu::encode_jpg(const QImage& image, int quality) {
  auto source = image.convertToFormat(QImage::Format_RGB888);
  auto buffer = static_cast<unsigned char*>(nullptr);
  auto size = 0ul;
  auto is_compressed = compress(source, quality, &buffer, &size);
  auto bytes = QByteArray();
  if(is_compressed) {
    bytes = QByteArray(reinterpret_cast<const char*>(buffer),
      static_cast<qsizetype>(size));
  }
  std::free(buffer);
  if(!is_compressed) {
    throw std::runtime_error("The JPG encoding failed.");
  }
  return bytes;
}

void Rellu::write_png_export(
    const QImage& image, const std::filesystem::path& path) {
  if(image.isNull()) {
    throw std::runtime_error("The image is empty.");
  }
  write_file(restructure_png(encode(
    image.convertToFormat(QImage::Format_RGBA8888), "png", -1)), path);
}

void Rellu::write_jpg_export(
    const QImage& image, const std::filesystem::path& path, int quality) {
  if(image.isNull()) {
    throw std::runtime_error("The image is empty.");
  }
  auto opaque = QImage(image.size(), QImage::Format_RGB32);
  opaque.fill(QColor(255, 255, 255));
  auto painter = QPainter(&opaque);
  painter.drawImage(0, 0, image);
  painter.end();
  write_file(encode_jpg(opaque, std::clamp(quality, 0, 100)), path);
}

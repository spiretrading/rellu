#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
#include <QtZlib/zlib.h>
#include "Rellu/Io/TiffDecoder.hpp"

using namespace Rellu;

namespace {
  using Bytes = std::vector<std::uint8_t>;
  using Values = std::vector<std::uint32_t>;

  constexpr auto MAXIMUM_PIXELS = std::uint64_t(1) << 28;
  constexpr auto IMAGE_WIDTH = std::uint16_t(256);
  constexpr auto IMAGE_LENGTH = std::uint16_t(257);
  constexpr auto BITS_PER_SAMPLE = std::uint16_t(258);
  constexpr auto COMPRESSION = std::uint16_t(259);
  constexpr auto PHOTOMETRIC = std::uint16_t(262);
  constexpr auto FILL_ORDER = std::uint16_t(266);
  constexpr auto STRIP_OFFSETS = std::uint16_t(273);
  constexpr auto SAMPLES_PER_PIXEL = std::uint16_t(277);
  constexpr auto ROWS_PER_STRIP = std::uint16_t(278);
  constexpr auto STRIP_BYTE_COUNTS = std::uint16_t(279);
  constexpr auto PLANAR_CONFIGURATION = std::uint16_t(284);
  constexpr auto PREDICTOR = std::uint16_t(317);
  constexpr auto COLOR_MAP = std::uint16_t(320);
  constexpr auto TILE_WIDTH = std::uint16_t(322);
  constexpr auto TILE_LENGTH = std::uint16_t(323);
  constexpr auto TILE_OFFSETS = std::uint16_t(324);
  constexpr auto TILE_BYTE_COUNTS = std::uint16_t(325);
  constexpr auto EXTRA_SAMPLES = std::uint16_t(338);
  constexpr auto SAMPLE_FORMAT = std::uint16_t(339);
  constexpr auto NO_COMPRESSION = std::uint32_t(1);
  constexpr auto LZW = std::uint32_t(5);
  constexpr auto DEFLATE = std::uint32_t(8);
  constexpr auto ADOBE_DEFLATE = std::uint32_t(32946);
  constexpr auto PACK_BITS = std::uint32_t(32773);
  constexpr auto WHITE_IS_ZERO = std::uint32_t(0);
  constexpr auto BLACK_IS_ZERO = std::uint32_t(1);
  constexpr auto RGB = std::uint32_t(2);
  constexpr auto PALETTE = std::uint32_t(3);
  constexpr auto ASSOCIATED_ALPHA = std::uint32_t(1);
  constexpr auto UNASSOCIATED_ALPHA = std::uint32_t(2);

  class Reader {
    public:
      Reader(const QByteArray& bytes, bool is_big_endian) noexcept
        : m_data(reinterpret_cast<const std::uint8_t*>(bytes.constData())),
          m_size(static_cast<std::uint64_t>(bytes.size())),
          m_is_big_endian(is_big_endian) {}

      bool is_big_endian() const {
        return m_is_big_endian;
      }

      bool has(std::uint64_t offset, std::uint64_t size) const {
        return offset <= m_size && size <= m_size - offset;
      }

      std::uint64_t get_size() const {
        return m_size;
      }

      const std::uint8_t* get(std::uint64_t offset) const {
        return m_data + offset;
      }

      std::uint16_t read16(std::uint64_t offset) const {
        auto data = m_data + offset;
        if(m_is_big_endian) {
          return static_cast<std::uint16_t>((data[0] << 8) | data[1]);
        }
        return static_cast<std::uint16_t>((data[1] << 8) | data[0]);
      }

      std::uint32_t read32(std::uint64_t offset) const {
        auto high = read16(offset);
        auto low = read16(offset + 2);
        if(!m_is_big_endian) {
          std::swap(high, low);
        }
        return (std::uint32_t(high) << 16) | low;
      }

    private:
      const std::uint8_t* m_data;
      std::uint64_t m_size;
      bool m_is_big_endian;
  };

  struct Entry {
    std::uint16_t m_tag;
    std::uint16_t m_type;
    std::uint32_t m_count;
    std::uint64_t m_offset;
  };

  class Directory {
    public:
      Directory(const Reader& reader, std::vector<Entry> entries) noexcept
        : m_reader(&reader),
          m_entries(std::move(entries)) {}

      std::optional<Values> find(std::uint16_t tag) const {
        auto entry = std::ranges::find(m_entries, tag, &Entry::m_tag);
        if(entry == m_entries.end()) {
          return std::nullopt;
        }
        auto width = std::uint64_t(0);
        if(entry->m_type == 1 || entry->m_type == 7) {
          width = 1;
        } else if(entry->m_type == 3) {
          width = 2;
        } else if(entry->m_type == 4) {
          width = 4;
        } else {
          return std::nullopt;
        }
        auto size = width * entry->m_count;
        auto offset = entry->m_offset;
        if(size > 4) {
          offset = m_reader->read32(entry->m_offset);
        }
        if(!m_reader->has(offset, size)) {
          return std::nullopt;
        }
        auto values = Values();
        values.reserve(entry->m_count);
        for(auto i = std::uint64_t(0); i != entry->m_count; ++i) {
          if(width == 1) {
            values.push_back(*m_reader->get(offset + i));
          } else if(width == 2) {
            values.push_back(m_reader->read16(offset + 2 * i));
          } else {
            values.push_back(m_reader->read32(offset + 4 * i));
          }
        }
        return values;
      }

      std::optional<std::uint32_t> get(
          std::uint16_t tag, std::optional<std::uint32_t> fallback) const {
        if(auto values = find(tag); values && !values->empty()) {
          return values->front();
        } else if(contains(tag)) {
          return std::nullopt;
        }
        return fallback;
      }

      bool contains(std::uint16_t tag) const {
        return std::ranges::contains(m_entries, tag, &Entry::m_tag);
      }

    private:
      const Reader* m_reader;
      std::vector<Entry> m_entries;
  };

  std::optional<Directory> read_directory(const Reader& reader) {
    auto offset = std::uint64_t(reader.read32(4));
    if(!reader.has(offset, 2)) {
      return std::nullopt;
    }
    auto count = reader.read16(offset);
    if(!reader.has(offset + 2, std::uint64_t(12) * count)) {
      return std::nullopt;
    }
    auto entries = std::vector<Entry>();
    for(auto i = std::uint64_t(0); i != count; ++i) {
      auto position = offset + 2 + 12 * i;
      entries.push_back(Entry(reader.read16(position),
        reader.read16(position + 2), reader.read32(position + 4),
        position + 8));
    }
    return Directory(reader, std::move(entries));
  }

  bool unpack_bits(const std::uint8_t* data, std::size_t size,
      std::size_t limit, Bytes& out) {
    auto i = std::size_t(0);
    while(i < size && out.size() < limit) {
      auto header = static_cast<std::int8_t>(data[i]);
      ++i;
      if(header >= 0) {
        auto length = std::size_t(header) + 1;
        if(length > size - i) {
          return false;
        }
        out.insert(out.end(), data + i, data + i + length);
        i += length;
      } else if(header != -128) {
        if(i == size) {
          return false;
        }
        out.insert(out.end(), std::size_t(1 - header), data[i]);
        ++i;
      }
    }
    return true;
  }

  bool decode_lzw(const std::uint8_t* data, std::size_t size,
      std::size_t limit, Bytes& out) {
    constexpr auto CLEAR = 256;
    constexpr auto END = 257;
    constexpr auto TABLE_SIZE = 4096;
    auto prefix = std::vector<std::uint16_t>(TABLE_SIZE);
    auto suffix = std::vector<std::uint8_t>(TABLE_SIZE);
    auto length = std::vector<std::uint16_t>(TABLE_SIZE);
    for(auto i = 0; i != 256; ++i) {
      suffix[i] = static_cast<std::uint8_t>(i);
      length[i] = 1;
    }
    auto bit = std::uint64_t(0);
    auto total = std::uint64_t(size) * 8;
    auto width = 9;
    auto next = 258;
    auto previous = -1;
    auto read = [&] {
      auto code = 0;
      for(auto i = 0; i != width; ++i) {
        auto byte = data[bit >> 3];
        code = (code << 1) | ((byte >> (7 - (bit & 7))) & 1);
        ++bit;
      }
      return code;
    };
    auto emit = [&] (int code) {
      auto start = out.size();
      out.resize(start + length[code]);
      for(auto i = std::size_t(length[code]); i != 0; --i) {
        out[start + i - 1] = suffix[code];
        code = prefix[code];
      }
      return out[start];
    };
    while(bit + width <= total) {
      auto code = read();
      if(code == END) {
        break;
      } else if(code == CLEAR) {
        width = 9;
        next = 258;
        previous = -1;
        continue;
      }
      if(previous == -1) {
        if(code >= 256) {
          return false;
        }
        emit(code);
      } else if(code < next) {
        auto first = emit(code);
        if(next < TABLE_SIZE) {
          prefix[next] = static_cast<std::uint16_t>(previous);
          suffix[next] = first;
          length[next] = static_cast<std::uint16_t>(length[previous] + 1);
          ++next;
        }
      } else if(code == next && next < TABLE_SIZE) {
        auto first = emit(previous);
        out.push_back(first);
        prefix[next] = static_cast<std::uint16_t>(previous);
        suffix[next] = first;
        length[next] = static_cast<std::uint16_t>(length[previous] + 1);
        ++next;
      } else {
        return false;
      }
      previous = code;
      if(next + 1 >= (1 << width) && width < 12) {
        ++width;
      }
      if(out.size() >= limit) {
        break;
      }
    }
    return true;
  }

  bool inflate_data(const std::uint8_t* data, std::size_t size,
      std::size_t limit, Bytes& out) {
    auto stream = z_stream();
    if(inflateInit(&stream) != Z_OK) {
      return false;
    }
    out.resize(limit);
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    auto result = inflate(&stream, Z_FINISH);
    auto produced = out.size() - stream.avail_out;
    inflateEnd(&stream);
    out.resize(produced);
    return result == Z_STREAM_END || result == Z_OK || result == Z_BUF_ERROR;
  }

  std::optional<Bytes> decompress(std::uint32_t compression,
      const std::uint8_t* data, std::size_t size, std::size_t expected) {
    auto out = Bytes();
    out.reserve(expected);
    auto is_decoded = false;
    if(compression == NO_COMPRESSION) {
      out.assign(data, data + std::min(size, expected));
      is_decoded = true;
    } else if(compression == PACK_BITS) {
      is_decoded = unpack_bits(data, size, expected, out);
    } else if(compression == LZW) {
      is_decoded = decode_lzw(data, size, expected, out);
    } else if(compression == DEFLATE || compression == ADOBE_DEFLATE) {
      is_decoded = inflate_data(data, size, expected, out);
    }
    if(!is_decoded) {
      return std::nullopt;
    }
    out.resize(expected);
    return out;
  }

  void undo_differencing(Bytes& chunk, std::uint64_t width,
      std::uint64_t rows, std::uint64_t row_bytes, std::uint32_t samples,
      std::uint32_t bits, bool is_big_endian) {
    for(auto row = std::uint64_t(0); row != rows; ++row) {
      auto data = chunk.data() + row * row_bytes;
      if(bits == 8) {
        for(auto i = std::uint64_t(samples); i < width * samples; ++i) {
          data[i] = static_cast<std::uint8_t>(data[i] + data[i - samples]);
        }
        continue;
      }
      auto load = [&] (std::uint64_t i) {
        auto byte = data + 2 * i;
        if(is_big_endian) {
          return static_cast<std::uint16_t>((byte[0] << 8) | byte[1]);
        }
        return static_cast<std::uint16_t>((byte[1] << 8) | byte[0]);
      };
      for(auto i = std::uint64_t(samples); i < width * samples; ++i) {
        auto value = static_cast<std::uint16_t>(load(i) + load(i - samples));
        auto byte = data + 2 * i;
        if(is_big_endian) {
          byte[0] = static_cast<std::uint8_t>(value >> 8);
          byte[1] = static_cast<std::uint8_t>(value);
        } else {
          byte[0] = static_cast<std::uint8_t>(value);
          byte[1] = static_cast<std::uint8_t>(value >> 8);
        }
      }
    }
  }

  std::uint32_t read_sample(const std::uint8_t* row, std::uint64_t index,
      std::uint32_t bits, bool is_big_endian) {
    if(bits == 8) {
      return row[index];
    } else if(bits == 16) {
      auto byte = row + 2 * index;
      if(is_big_endian) {
        return (std::uint32_t(byte[0]) << 8) | byte[1];
      }
      return (std::uint32_t(byte[1]) << 8) | byte[0];
    }
    auto position = index * bits;
    auto byte = row[position >> 3];
    auto shift = 8 - bits - (position & 7);
    return (byte >> shift) & ((1u << bits) - 1);
  }

  int to_byte(std::uint32_t value, std::uint32_t bits) {
    if(bits == 8) {
      return static_cast<int>(value);
    } else if(bits == 16) {
      return static_cast<int>((value * 255 + 32767) / 65535);
    }
    return static_cast<int>(value * 255 / ((1u << bits) - 1));
  }
}

bool Rellu::is_tiff(const QByteArray& bytes) {
  return bytes.startsWith(QByteArray("II*\0", 4)) ||
    bytes.startsWith(QByteArray("MM\0*", 4));
}

QImage Rellu::decode_tiff(const QByteArray& bytes) {
  if(!is_tiff(bytes) || bytes.size() < 8) {
    return QImage();
  }
  auto reader = Reader(bytes, bytes[0] == 'M');
  auto directory = read_directory(reader);
  if(!directory) {
    return QImage();
  }
  auto width = directory->get(IMAGE_WIDTH, std::nullopt);
  auto height = directory->get(IMAGE_LENGTH, std::nullopt);
  auto photometric = directory->get(PHOTOMETRIC, std::nullopt);
  auto compression = directory->get(COMPRESSION, NO_COMPRESSION);
  auto samples = directory->get(SAMPLES_PER_PIXEL, 1);
  auto predictor = directory->get(PREDICTOR, 1);
  if(!width || !height || !photometric || !compression || !samples ||
      !predictor || *width == 0 || *height == 0 || *samples == 0 ||
      std::uint64_t(*width) * *height > MAXIMUM_PIXELS ||
      directory->get(PLANAR_CONFIGURATION, 1) != 1u ||
      directory->get(FILL_ORDER, 1) != 1u) {
    return QImage();
  }
  auto bits = std::uint32_t(1);
  if(auto values = directory->find(BITS_PER_SAMPLE)) {
    if(values->empty() || std::ranges::any_of(*values,
        [&] (auto value) { return value != values->front(); })) {
      return QImage();
    }
    bits = values->front();
  } else if(directory->contains(BITS_PER_SAMPLE)) {
    return QImage();
  }
  if(auto formats = directory->find(SAMPLE_FORMAT)) {
    if(std::ranges::any_of(*formats, [] (auto value) {
        return value != 1; })) {
      return QImage();
    }
  }
  auto base = std::uint32_t(1);
  if(*photometric == RGB) {
    base = 3;
    if(bits != 8 && bits != 16) {
      return QImage();
    }
  } else if(*photometric == PALETTE) {
    if(bits != 1 && bits != 2 && bits != 4 && bits != 8) {
      return QImage();
    }
  } else if(*photometric == WHITE_IS_ZERO || *photometric == BLACK_IS_ZERO) {
    if(bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) {
      return QImage();
    }
  } else {
    return QImage();
  }
  if(*samples < base || (*samples > 1 && bits < 8)) {
    return QImage();
  }
  auto alpha = std::uint32_t(0);
  if(*samples > base) {
    if(auto extra = directory->find(EXTRA_SAMPLES); extra && !extra->empty()) {
      alpha = extra->front();
    }
  }
  auto colors = Values();
  if(*photometric == PALETTE) {
    auto map = directory->find(COLOR_MAP);
    if(!map || map->size() != 3 * (std::size_t(1) << bits)) {
      return QImage();
    }
    colors = std::move(*map);
  }
  if((*predictor != 1 && *predictor != 2) ||
      (*predictor == 2 && bits != 8 && bits != 16)) {
    return QImage();
  }
  auto row_size = [&] (std::uint64_t pixels) {
    return (pixels * *samples * bits + 7) / 8;
  };
  auto row_bytes = row_size(*width);
  auto raw = Bytes(row_bytes * *height);
  auto decode_chunk = [&] (std::uint32_t offset, std::uint32_t count,
      std::uint64_t pixels, std::uint64_t rows) -> std::optional<Bytes> {
    if(!reader.has(offset, 0)) {
      return std::nullopt;
    }
    auto size = std::min<std::uint64_t>(count, reader.get_size() - offset);
    auto chunk_row_bytes = row_size(pixels);
    auto chunk = decompress(*compression, reader.get(offset),
      static_cast<std::size_t>(size),
      static_cast<std::size_t>(chunk_row_bytes * rows));
    if(chunk && *predictor == 2) {
      undo_differencing(*chunk, pixels, rows, chunk_row_bytes, *samples,
        bits, reader.is_big_endian());
    }
    return chunk;
  };
  if(directory->contains(TILE_WIDTH)) {
    auto tile_width = directory->get(TILE_WIDTH, std::nullopt);
    auto tile_length = directory->get(TILE_LENGTH, std::nullopt);
    auto offsets = directory->find(TILE_OFFSETS);
    auto counts = directory->find(TILE_BYTE_COUNTS);
    if(!tile_width || !tile_length || !offsets || !counts ||
        *tile_width == 0 || *tile_length == 0 ||
        (std::uint64_t(*tile_width) * *samples * bits) % 8 != 0) {
      return QImage();
    }
    auto across = (*width + *tile_width - 1) / *tile_width;
    auto down = (*height + *tile_length - 1) / *tile_length;
    if(offsets->size() < std::uint64_t(across) * down ||
        counts->size() < offsets->size()) {
      return QImage();
    }
    auto tile_row_bytes = row_size(*tile_width);
    for(auto i = std::uint64_t(0); i != std::uint64_t(across) * down; ++i) {
      auto chunk = decode_chunk(
        (*offsets)[i], (*counts)[i], *tile_width, *tile_length);
      if(!chunk) {
        return QImage();
      }
      auto left = (i % across) * tile_row_bytes;
      auto top = (i / across) * *tile_length;
      auto length = std::min(tile_row_bytes, row_bytes - left);
      for(auto row = std::uint64_t(0);
          row != *tile_length && top + row < *height; ++row) {
        std::memcpy(raw.data() + (top + row) * row_bytes + left,
          chunk->data() + row * tile_row_bytes, length);
      }
    }
  } else {
    auto rows = std::max<std::uint32_t>(1,
      directory->get(ROWS_PER_STRIP, *height).value_or(*height));
    auto offsets = directory->find(STRIP_OFFSETS);
    auto counts = directory->find(STRIP_BYTE_COUNTS);
    auto strips = (std::uint64_t(*height) + rows - 1) / rows;
    if(!offsets || offsets->size() < strips) {
      return QImage();
    }
    if(!counts) {
      if(*compression != NO_COMPRESSION) {
        return QImage();
      }
      counts =
        Values(offsets->size(), std::numeric_limits<std::uint32_t>::max());
    } else if(counts->size() < strips) {
      return QImage();
    }
    for(auto i = std::uint64_t(0); i != strips; ++i) {
      auto top = i * rows;
      auto strip_rows = std::min<std::uint64_t>(rows, *height - top);
      auto chunk =
        decode_chunk((*offsets)[i], (*counts)[i], *width, strip_rows);
      if(!chunk) {
        return QImage();
      }
      std::memcpy(raw.data() + top * row_bytes, chunk->data(), chunk->size());
    }
  }
  auto is_associated = alpha == ASSOCIATED_ALPHA;
  auto has_alpha = is_associated || alpha == UNASSOCIATED_ALPHA;
  auto format = QImage::Format_RGB32;
  if(is_associated) {
    format = QImage::Format_ARGB32_Premultiplied;
  } else if(has_alpha) {
    format = QImage::Format_ARGB32;
  }
  auto image =
    QImage(static_cast<int>(*width), static_cast<int>(*height), format);
  if(image.isNull()) {
    return QImage();
  }
  auto is_big_endian = reader.is_big_endian();
  for(auto y = std::uint64_t(0); y != *height; ++y) {
    auto row = raw.data() + y * row_bytes;
    auto line = reinterpret_cast<QRgb*>(image.scanLine(static_cast<int>(y)));
    for(auto x = std::uint64_t(0); x != *width; ++x) {
      auto index = x * *samples;
      auto sample = [&] (std::uint64_t i) {
        return to_byte(read_sample(row, index + i, bits, is_big_endian), bits);
      };
      auto red = 0;
      auto green = 0;
      auto blue = 0;
      if(*photometric == RGB) {
        red = sample(0);
        green = sample(1);
        blue = sample(2);
      } else if(*photometric == PALETTE) {
        auto entry = read_sample(row, index, bits, is_big_endian);
        auto size = colors.size() / 3;
        red = to_byte(colors[entry], 16);
        green = to_byte(colors[size + entry], 16);
        blue = to_byte(colors[2 * size + entry], 16);
      } else {
        red = sample(0);
        if(*photometric == WHITE_IS_ZERO) {
          red = 255 - red;
        }
        green = red;
        blue = red;
      }
      auto opacity = has_alpha ? sample(base) : 255;
      if(is_associated) {
        red = std::min(red, opacity);
        green = std::min(green, opacity);
        blue = std::min(blue, opacity);
      }
      line[x] = qRgba(red, green, blue, opacity);
    }
  }
  return image;
}

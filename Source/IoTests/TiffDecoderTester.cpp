#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>
#include <doctest/doctest.h>
#include <QByteArray>
#include <QImage>
#include "Rellu/Io/TiffDecoder.hpp"

using namespace Rellu;

namespace {
  using Values = std::vector<std::uint32_t>;

  constexpr auto SHORT = std::uint16_t(3);
  constexpr auto LONG = std::uint16_t(4);

  struct Field {
    std::uint16_t m_tag;
    std::uint16_t m_type;
    Values m_values;
  };

  struct Tiff {
    bool m_is_big_endian = false;
    bool m_is_tiled = false;
    std::vector<Field> m_fields;
    std::vector<QByteArray> m_chunks;
  };

  void append(QByteArray& out, std::uint32_t value, int size, bool is_big) {
    for(auto i = 0; i != size; ++i) {
      auto shift = is_big ? 8 * (size - 1 - i) : 8 * i;
      out.append(static_cast<char>((value >> shift) & 0xFF));
    }
  }

  void set32(QByteArray& out, qsizetype at, std::uint32_t value, bool is_big) {
    auto bytes = QByteArray();
    append(bytes, value, 4, is_big);
    out.replace(at, 4, bytes);
  }

  QByteArray write_tiff(Tiff tiff) {
    auto is_big = tiff.m_is_big_endian;
    auto out = is_big ? QByteArray("MM\0*", 4) : QByteArray("II*\0", 4);
    append(out, 0, 4, is_big);
    auto offsets = Values();
    auto counts = Values();
    for(auto& chunk : tiff.m_chunks) {
      offsets.push_back(static_cast<std::uint32_t>(out.size()));
      counts.push_back(static_cast<std::uint32_t>(chunk.size()));
      out.append(chunk);
      if(out.size() % 2) {
        out.append('\0');
      }
    }
    tiff.m_fields.push_back(
      Field(tiff.m_is_tiled ? 324 : 273, LONG, std::move(offsets)));
    tiff.m_fields.push_back(
      Field(tiff.m_is_tiled ? 325 : 279, LONG, std::move(counts)));
    std::ranges::sort(tiff.m_fields, {}, &Field::m_tag);
    auto directory = out.size();
    set32(out, 4, static_cast<std::uint32_t>(directory), is_big);
    auto count = static_cast<qsizetype>(tiff.m_fields.size());
    auto extra = directory + 2 + 12 * count + 4;
    auto tail = QByteArray();
    append(out, static_cast<std::uint32_t>(count), 2, is_big);
    for(auto& field : tiff.m_fields) {
      auto size = field.m_type == SHORT ? 2 : 4;
      append(out, field.m_tag, 2, is_big);
      append(out, field.m_type, 2, is_big);
      append(out, static_cast<std::uint32_t>(field.m_values.size()), 4,
        is_big);
      auto data = QByteArray();
      for(auto value : field.m_values) {
        append(data, value, size, is_big);
      }
      if(data.size() <= 4) {
        data.append(QByteArray(4 - data.size(), '\0'));
        out.append(data);
      } else {
        append(out, static_cast<std::uint32_t>(extra + tail.size()), 4,
          is_big);
        tail.append(data);
      }
    }
    append(out, 0, 4, is_big);
    out.append(tail);
    return out;
  }

  QByteArray encode_lzw(const QByteArray& data) {
    auto out = QByteArray();
    auto buffer = std::uint32_t(0);
    auto pending = 0;
    auto width = 9;
    auto put = [&] (int code) {
      buffer = (buffer << width) | static_cast<std::uint32_t>(code);
      pending += width;
      while(pending >= 8) {
        pending -= 8;
        out.append(static_cast<char>((buffer >> pending) & 0xFF));
      }
    };
    auto table = std::map<QByteArray, int>();
    auto next = 258;
    auto reset = [&] {
      table.clear();
      for(auto i = 0; i != 256; ++i) {
        table[QByteArray(1, static_cast<char>(i))] = i;
      }
      next = 258;
    };
    auto advance = [&] {
      ++next;
      if(next == 4094) {
        put(256);
        reset();
        width = 9;
      } else if(next == (1 << width)) {
        ++width;
      }
    };
    reset();
    put(256);
    auto current = QByteArray();
    for(auto byte : data) {
      auto extended = current + byte;
      if(table.contains(extended)) {
        current = extended;
        continue;
      }
      put(table[current]);
      table[extended] = next;
      advance();
      current = QByteArray(1, byte);
    }
    if(!current.isEmpty()) {
      put(table[current]);
      advance();
    }
    put(257);
    if(pending > 0) {
      out.append(static_cast<char>((buffer << (8 - pending)) & 0xFF));
    }
    return out;
  }

  QByteArray encode_pack_bits(const QByteArray& data) {
    auto out = QByteArray();
    auto i = qsizetype(0);
    while(i < data.size()) {
      auto run = qsizetype(1);
      while(i + run < data.size() && run < 128 && data[i + run] == data[i]) {
        ++run;
      }
      if(run >= 3) {
        out.append(static_cast<char>(1 - run));
        out.append(data[i]);
        i += run;
        continue;
      }
      auto length = std::min<qsizetype>(128, data.size() - i);
      out.append(static_cast<char>(length - 1));
      out.append(data.mid(i, length));
      i += length;
    }
    return out;
  }

  QByteArray encode_deflate(const QByteArray& data) {
    return qCompress(data).mid(4);
  }

  QByteArray predict(QByteArray data, int width, int samples, int bits,
      bool is_big) {
    auto row = width * samples * bits / 8;
    for(auto start = qsizetype(0); start < data.size(); start += row) {
      if(bits == 8) {
        for(auto i = row - 1; i >= samples; --i) {
          data[start + i] =
            static_cast<char>(data[start + i] - data[start + i - samples]);
        }
        continue;
      }
      auto load = [&] (qsizetype i) {
        auto high = static_cast<std::uint8_t>(data[start + 2 * i]);
        auto low = static_cast<std::uint8_t>(data[start + 2 * i + 1]);
        if(!is_big) {
          std::swap(high, low);
        }
        return static_cast<std::uint16_t>((high << 8) | low);
      };
      for(auto i = width * samples - 1; i >= samples; --i) {
        auto value = static_cast<std::uint16_t>(load(i) - load(i - samples));
        auto bytes = QByteArray();
        append(bytes, value, 2, is_big);
        data.replace(start + 2 * i, 2, bytes);
      }
    }
    return data;
  }

  QByteArray make_samples(int count, std::uint32_t seed) {
    auto data = QByteArray();
    for(auto i = 0; i != count; ++i) {
      seed = seed * 1103515245 + 12345;
      data.append(static_cast<char>((seed >> 16) & 0xFF));
    }
    return data;
  }

  QByteArray compress(std::uint32_t compression, const QByteArray& data) {
    if(compression == 5) {
      return encode_lzw(data);
    } else if(compression == 32773) {
      return encode_pack_bits(data);
    } else if(compression == 8 || compression == 32946) {
      return encode_deflate(data);
    }
    return data;
  }

  std::vector<Field> make_fields(int width, int height, int bits,
      int samples, std::uint32_t photometric, std::uint32_t compression) {
    return {Field(256, LONG, {std::uint32_t(width)}),
      Field(257, LONG, {std::uint32_t(height)}),
      Field(258, SHORT, Values(samples, std::uint32_t(bits))),
      Field(259, SHORT, {compression}),
      Field(262, SHORT, {photometric}),
      Field(277, SHORT, {std::uint32_t(samples)})};
  }

  QByteArray make_strips(const QByteArray& data, int width, int height,
      int bits, int samples, std::uint32_t photometric,
      std::uint32_t compression, bool is_predicted, int rows, bool is_big,
      std::vector<Field> extra = {}) {
    auto tiff = Tiff();
    tiff.m_is_big_endian = is_big;
    tiff.m_fields =
      make_fields(width, height, bits, samples, photometric, compression);
    tiff.m_fields.push_back(Field(278, LONG, {std::uint32_t(rows)}));
    if(is_predicted) {
      tiff.m_fields.push_back(Field(317, SHORT, {2}));
    }
    tiff.m_fields.insert(tiff.m_fields.end(), extra.begin(), extra.end());
    auto row = (width * samples * bits + 7) / 8;
    for(auto top = 0; top < height; top += rows) {
      auto strip = data.mid(top * row, std::min(rows, height - top) * row);
      if(is_predicted) {
        strip = predict(strip, width, samples, bits, is_big);
      }
      tiff.m_chunks.push_back(compress(compression, strip));
    }
    return write_tiff(std::move(tiff));
  }

  int to_byte(int value) {
    return (value * 255 + 32767) / 65535;
  }
}

TEST_SUITE("TiffDecoder") {
  TEST_CASE("is_tiff") {
    REQUIRE(is_tiff(QByteArray("II*\0", 4)));
    REQUIRE(is_tiff(QByteArray("MM\0*", 4)));
    REQUIRE(!is_tiff(QByteArray("MM*\0", 4)));
    REQUIRE(!is_tiff(QByteArray("II")));
    REQUIRE(decode_tiff(QByteArray("II*\0\xFF\xFF\xFF\xFF", 8)).isNull());
    REQUIRE(decode_tiff(QByteArray("not a tiff")).isNull());
  }

  TEST_CASE("rgb_compressions") {
    auto width = 37;
    auto height = 23;
    auto data = make_samples(width * height * 3, 7);
    for(auto is_big : {false, true}) {
      for(auto compression : {1u, 5u, 32773u, 8u, 32946u}) {
        for(auto is_predicted : {false, true}) {
          auto image = decode_tiff(make_strips(data, width, height, 8, 3, 2,
            compression, is_predicted, 10, is_big));
          REQUIRE(image.size() == QSize(width, height));
          REQUIRE(image.format() == QImage::Format_RGB32);
          for(auto y = 0; y != height; ++y) {
            for(auto x = 0; x != width; ++x) {
              auto i = 3 * (y * width + x);
              REQUIRE(image.pixel(x, y) ==
                qRgb(static_cast<std::uint8_t>(data[i]),
                  static_cast<std::uint8_t>(data[i + 1]),
                  static_cast<std::uint8_t>(data[i + 2])));
            }
          }
        }
      }
    }
  }

  TEST_CASE("lzw_code_widths") {
    auto width = 128;
    auto height = 96;
    auto data = make_samples(width * height, 3);
    for(auto i = qsizetype(0); i < data.size(); i += 5) {
      data[i] = 'x';
    }
    auto image = decode_tiff(
      make_strips(data, width, height, 8, 1, 1, 5, false, height, false));
    REQUIRE(image.size() == QSize(width, height));
    for(auto y = 0; y != height; ++y) {
      for(auto x = 0; x != width; ++x) {
        auto value = static_cast<std::uint8_t>(data[y * width + x]);
        REQUIRE(image.pixel(x, y) == qRgb(value, value, value));
      }
    }
  }

  TEST_CASE("windows_lzw") {
    auto image = decode_tiff(QByteArray::fromHex(
      "49492a003e0300008000205020640831021240861021e40891022a408c10236408f102"
      "4240921024e40951025a409810266409b10272000280003850180e0a0602a141207028"
      "3011850782a0a12062142a0e028601f850d8460a1e09a144215028902d85138620a2a0"
      "d2145a1c028c03b851987e0a3610a14948a0000e1403038280c0a850241c0a06046140"
      "f0a828241885054380a0c07e141b118283c2685084540a120b61427188285434850b47"
      "00a180ee14331f8286c028512986854780c150f0303f1e090a83c181a4783c3f0f0909"
      "3bb2a078605e8f0d8d21e1e1ce3c423f07890088f138261e2a0763c5a14078c0328f19"
      "87a1e3611e3c94940001c2803070500c150a02438140608ce72a0a04862140a8e02818"
      "1f85036060a0781a141085028240d828138440a05414828168600a0601b8281987c0a0"
      "6c0282825232000118c80608c32030348c81211832060578c81e1a4320907a8c82a030"
      "320c01d8c8360bc320f0408c84214832120638c84e1d432150068c85a06032180298c8"
      "660ec321b04c8c894a2e150f0060a83c0303f0f0121503c060690f01e1f83c090130f0"
      "f10f0300f43c0d8520f03c19c3c1087c0f0480443c138260f0540ec3c168500f060194"
      "3c1987a0f06c03c3c2524600018918060c4460304991812188460607a9181e04c46090"
      "2b9182a110460c05c918361d4460f00d91842098461203e9184e15c461506f9185a020"
      "4618020918660e4461b051918949400007050018381400c15050048701401803050078"
      "2814024110500a860140301f0500d8184f83414042140501206c1404e0205015024140"
      "5a100501805c140661e0501b01414094a421f968060fc5a0305e968121f9210765a078"
      "3d168241725a0a87c1683006e5a0d83b1683c16a5a1087a168480665a1383916854162"
      "5a1687840e17968660dc5a1b056968949900008c64018461900c1a4640480c190180bc"
      "6407852190241d4640a818190300ec640d85e1903c00464108241904811c641386a190"
      "54034646266418053190661d8641b019190949b80009a6e0184d1b80c1ce6e0481a1b8"
      "181026e078671b8240366e0a8341b83016a6e0d8011b83c09e6e1084e1b8481d26e138"
      "1b1b8541066e168681b86003a6e198351b86c16e6e1c8808000f00fe00040001000000"
      "0000000000010400010000001400000001010400010000000c00000002010300030000"
      "00f8030000030103000100000005000000060103000100000002000000110104000100"
      "00000800000015010300010000000300000016010400010000000c0000001701040001"
      "000000350300001a01050001000000fe0300001b01050001000000060400001c010300"
      "01000000010000002801030001000000020000003d0103000100000001000000000000"
      "0008000800080000770100e803000000770100e8030000"));
    REQUIRE(image.size() == QSize(20, 12));
    for(auto y = 0; y != 12; ++y) {
      for(auto x = 0; x != 20; ++x) {
        REQUIRE(image.pixel(x, y) ==
          qRgb((x * y * 7) % 256, (y * 20) % 256, (x * 12) % 256));
      }
    }
  }

  TEST_CASE("tiles") {
    auto width = 40;
    auto height = 20;
    auto tile = 16;
    auto data = make_samples(width * height * 4, 11);
    auto tiff = Tiff();
    tiff.m_is_tiled = true;
    tiff.m_fields = make_fields(width, height, 8, 4, 2, 5);
    tiff.m_fields.push_back(Field(322, SHORT, {std::uint32_t(tile)}));
    tiff.m_fields.push_back(Field(323, SHORT, {std::uint32_t(tile)}));
    tiff.m_fields.push_back(Field(338, SHORT, {2}));
    for(auto top = 0; top < height; top += tile) {
      for(auto left = 0; left < width; left += tile) {
        auto chunk = QByteArray(tile * tile * 4, '\0');
        for(auto y = 0; y != tile && top + y < height; ++y) {
          for(auto x = 0; x != tile && left + x < width; ++x) {
            for(auto s = 0; s != 4; ++s) {
              chunk[(y * tile + x) * 4 + s] =
                data[((top + y) * width + left + x) * 4 + s];
            }
          }
        }
        tiff.m_chunks.push_back(encode_lzw(chunk));
      }
    }
    auto image = decode_tiff(write_tiff(std::move(tiff)));
    REQUIRE(image.size() == QSize(width, height));
    REQUIRE(image.format() == QImage::Format_ARGB32);
    for(auto y = 0; y != height; ++y) {
      for(auto x = 0; x != width; ++x) {
        auto i = 4 * (y * width + x);
        REQUIRE(image.pixel(x, y) ==
          qRgba(static_cast<std::uint8_t>(data[i]),
            static_cast<std::uint8_t>(data[i + 1]),
            static_cast<std::uint8_t>(data[i + 2]),
            static_cast<std::uint8_t>(data[i + 3])));
      }
    }
  }

  TEST_CASE("sixteen_bits") {
    auto width = 9;
    auto height = 5;
    for(auto is_big : {false, true}) {
      auto grey = make_samples(width * height * 4, 5);
      auto image = decode_tiff(make_strips(grey, width, height, 16, 2, 1, 8,
        true, 2, is_big, {Field(338, SHORT, {2})}));
      REQUIRE(image.size() == QSize(width, height));
      auto load = [&] (const QByteArray& data, int i) {
        auto high = static_cast<std::uint8_t>(data[2 * i]);
        auto low = static_cast<std::uint8_t>(data[2 * i + 1]);
        if(!is_big) {
          std::swap(high, low);
        }
        return (high << 8) | low;
      };
      for(auto i = 0; i != width * height; ++i) {
        auto value = to_byte(load(grey, 2 * i));
        REQUIRE(image.pixel(i % width, i / width) ==
          qRgba(value, value, value, to_byte(load(grey, 2 * i + 1))));
      }
      auto color = make_samples(width * height * 6, 9);
      auto rgb = decode_tiff(make_strips(
        color, width, height, 16, 3, 2, 1, false, height, is_big));
      for(auto i = 0; i != width * height; ++i) {
        REQUIRE(rgb.pixel(i % width, i / width) ==
          qRgb(to_byte(load(color, 3 * i)), to_byte(load(color, 3 * i + 1)),
            to_byte(load(color, 3 * i + 2))));
      }
    }
  }

  TEST_CASE("associated_alpha") {
    auto data = QByteArray::fromHex("4020108000000000ffffffff");
    auto image = decode_tiff(make_strips(data, 3, 1, 8, 4, 2, 1, false, 1,
      false, {Field(338, SHORT, {1})}));
    REQUIRE(image.format() == QImage::Format_ARGB32_Premultiplied);
    auto line = reinterpret_cast<const QRgb*>(image.constScanLine(0));
    REQUIRE(line[0] == qRgba(0x40, 0x20, 0x10, 0x80));
    REQUIRE(line[1] == qRgba(0, 0, 0, 0));
    REQUIRE(line[2] == qRgba(0xFF, 0xFF, 0xFF, 0xFF));
  }

  TEST_CASE("palette") {
    auto colors = Values(3 * 16, 0);
    colors[1] = 0xFFFF;
    colors[16 + 2] = 0x8080;
    colors[32 + 15] = 0x1234;
    auto data = QByteArray::fromHex("12f0");
    auto image = decode_tiff(make_strips(data, 4, 1, 4, 1, 3, 32773, false,
      1, true, {Field(320, SHORT, colors)}));
    REQUIRE(image.size() == QSize(4, 1));
    REQUIRE(image.pixel(0, 0) == qRgb(255, 0, 0));
    REQUIRE(image.pixel(1, 0) == qRgb(0, 128, 0));
    REQUIRE(image.pixel(2, 0) == qRgb(0, 0, to_byte(0x1234)));
    REQUIRE(image.pixel(3, 0) == qRgb(0, 0, 0));
  }

  TEST_CASE("bilevel") {
    auto data = QByteArray::fromHex("a5c07f00");
    for(auto photometric : {0u, 1u}) {
      auto image = decode_tiff(
        make_strips(data, 10, 2, 1, 1, photometric, 1, false, 2, false));
      REQUIRE(image.size() == QSize(10, 2));
      auto bits = std::vector<int>({1, 0, 1, 0, 0, 1, 0, 1, 1, 1,
        0, 1, 1, 1, 1, 1, 1, 1, 0, 0});
      for(auto i = 0; i != 20; ++i) {
        auto value = (bits[i] == 1) == (photometric == 1) ? 255 : 0;
        REQUIRE(image.pixel(i % 10, i / 10) == qRgb(value, value, value));
      }
    }
  }

  TEST_CASE("unsupported") {
    auto data = make_samples(4 * 4 * 3, 1);
    auto make = [&] (std::vector<Field> extra, std::uint32_t compression,
        std::uint32_t photometric, int samples) {
      return decode_tiff(make_strips(data, 4, 4, 8, samples, photometric,
        compression, false, 4, false, std::move(extra)));
    };
    REQUIRE(!make({}, 1, 2, 3).isNull());
    REQUIRE(make({Field(284, SHORT, {2})}, 1, 2, 3).isNull());
    REQUIRE(make({}, 7, 2, 3).isNull());
    REQUIRE(make({}, 1, 5, 3).isNull());
    REQUIRE(make({Field(339, SHORT, {3, 3, 3})}, 1, 2, 3).isNull());
    REQUIRE(make({Field(317, SHORT, {3})}, 1, 2, 3).isNull());
    REQUIRE(make({}, 1, 3, 1).isNull());
  }
}

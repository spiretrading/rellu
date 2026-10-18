#ifndef RELLU_FORMAT_BYTE_READER_HPP
#define RELLU_FORMAT_BYTE_READER_HPP
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include "Rellu/Format/FormatError.hpp"
#include "ByteWriter.hpp"

namespace Rellu::Details {

  /**
   * Reads little-endian binary values from a buffer, throwing FormatError
   * whenever a value would extend past the buffer's end or is malformed.
   */
  class ByteReader {
    public:

      /**
       * Constructs a ByteReader.
       * @param data The bytes to read.
       * @param context The name of what the bytes hold, used in errors.
       */
      ByteReader(std::span<const std::byte> data,
        std::string_view context) noexcept;

      /** Returns the number of bytes left to read. */
      std::size_t get_remaining() const;

      std::uint8_t read_byte();

      /** Reads a byte that must be 0 or 1. */
      bool read_bool();

      std::uint16_t read_u16();
      std::uint32_t read_u32();
      std::uint64_t read_u64();

      /** Reads an unsigned integer in LEB128 form. */
      std::uint64_t read_varint();

      /** Reads a signed integer in zigzag LEB128 form. */
      std::int64_t read_signed();

      /** Reads a signed integer that must fit in an int. */
      int read_int();

      /** Reads a number written by ByteWriter::write_number. */
      double read_number();

      /** Reads the raw bits of a double. */
      double read_double();

      /**
       * Reads bytes as they are.
       * @param size The number of bytes.
       * @return The bytes, valid as long as the buffer.
       */
      std::span<const std::byte> read_bytes(std::size_t size);

      /** Reads a string written by ByteWriter::write_string. */
      std::string read_string();

      /**
       * Reads the number of elements of a sequence.
       * @param size The smallest number of bytes an element occupies.
       * @return The number of elements, which fit in the bytes left.
       */
      std::size_t read_count(std::size_t size);

      /**
       * Reads an enumerator stored as a byte.
       * @param last The largest valid enumerator.
       */
      template<typename E>
      E read_enum(E last);

      /** Throws FormatError unless every byte was read. */
      void expect_end() const;

      /**
       * Throws FormatError describing a malformed value.
       * @param message The description of the problem.
       */
      [[noreturn]] void fail(std::string_view message) const;

    private:
      const std::byte* m_position;
      const std::byte* m_end;
      std::string_view m_context;

      void require(std::size_t size) const;
  };

  inline ByteReader::ByteReader(
    std::span<const std::byte> data, std::string_view context) noexcept
    : m_position(data.data()),
      m_end(data.data() + data.size()),
      m_context(context) {}

  inline std::size_t ByteReader::get_remaining() const {
    return static_cast<std::size_t>(m_end - m_position);
  }

  inline std::uint8_t ByteReader::read_byte() {
    require(1);
    return static_cast<std::uint8_t>(*m_position++);
  }

  inline bool ByteReader::read_bool() {
    auto value = read_byte();
    if(value > 1) {
      fail("invalid boolean");
    }
    return value == 1;
  }

  inline std::uint16_t ByteReader::read_u16() {
    require(2);
    auto value = std::uint16_t(0);
    for(auto i = 0; i != 2; ++i) {
      value |= static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(m_position[i]) << (8 * i));
    }
    m_position += 2;
    return value;
  }

  inline std::uint32_t ByteReader::read_u32() {
    require(4);
    auto value = std::uint32_t(0);
    for(auto i = 0; i != 4; ++i) {
      value |= static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(m_position[i])) << (8 * i);
    }
    m_position += 4;
    return value;
  }

  inline std::uint64_t ByteReader::read_u64() {
    require(8);
    auto value = std::uint64_t(0);
    for(auto i = 0; i != 8; ++i) {
      value |= static_cast<std::uint64_t>(
        static_cast<std::uint8_t>(m_position[i])) << (8 * i);
    }
    m_position += 8;
    return value;
  }

  inline std::uint64_t ByteReader::read_varint() {
    auto value = std::uint64_t(0);
    for(auto shift = 0; shift < 64; shift += 7) {
      if(m_position == m_end) {
        fail("unexpected end of data");
      }
      auto byte = static_cast<std::uint8_t>(*m_position++);
      if(shift == 63 && byte > 1) {
        fail("integer overflow");
      }
      value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
      if(!(byte & 0x80)) {
        return value;
      }
    }
    fail("integer overflow");
  }

  inline std::int64_t ByteReader::read_signed() {
    auto value = read_varint();
    return static_cast<std::int64_t>(value >> 1) ^
      -static_cast<std::int64_t>(value & 1);
  }

  inline int ByteReader::read_int() {
    auto value = read_signed();
    if(value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
      fail("integer out of range");
    }
    return static_cast<int>(value);
  }

  inline double ByteReader::read_number() {
    auto value = read_varint();
    auto tag = value & 7;
    if(tag == RAW_NUMBER_TAG) {
      if(value != RAW_NUMBER_TAG) {
        fail("invalid number");
      }
      return read_double();
    } else if(tag >= std::size(NUMBER_SCALES)) {
      fail("invalid number");
    }
    auto zigzag = value >> 3;
    auto integer = static_cast<std::int64_t>(zigzag >> 1) ^
      -static_cast<std::int64_t>(zigzag & 1);
    if(integer > MAXIMUM_SCALED || integer < -MAXIMUM_SCALED) {
      fail("invalid number");
    }
    return static_cast<double>(integer) / NUMBER_SCALES[tag];
  }

  inline double ByteReader::read_double() {
    return std::bit_cast<double>(read_u64());
  }

  inline std::span<const std::byte> ByteReader::read_bytes(std::size_t size) {
    require(size);
    auto bytes = std::span<const std::byte>(m_position, size);
    m_position += size;
    return bytes;
  }

  inline std::string ByteReader::read_string() {
    auto size = read_count(1);
    auto bytes = read_bytes(size);
    return std::string(reinterpret_cast<const char*>(bytes.data()), size);
  }

  inline std::size_t ByteReader::read_count(std::size_t size) {
    auto count = read_varint();
    if(count > get_remaining() / std::max<std::size_t>(size, 1)) {
      fail("count exceeds the data");
    }
    return static_cast<std::size_t>(count);
  }

  template<typename E>
  E ByteReader::read_enum(E last) {
    auto value = read_byte();
    if(value > static_cast<std::uint8_t>(last)) {
      fail("invalid enumerator");
    }
    return static_cast<E>(value);
  }

  inline void ByteReader::expect_end() const {
    if(m_position != m_end) {
      fail("unexpected trailing data");
    }
  }

  inline void ByteReader::fail(std::string_view message) const {
    throw FormatError(
      "Corrupt " + std::string(m_context) + ": " + std::string(message) + ".");
  }

  inline void ByteReader::require(std::size_t size) const {
    if(size > get_remaining()) {
      fail("unexpected end of data");
    }
  }
}

#endif

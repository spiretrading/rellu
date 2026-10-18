#ifndef RELLU_FORMAT_BYTE_WRITER_HPP
#define RELLU_FORMAT_BYTE_WRITER_HPP
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace Rellu::Details {

  /** The divisors of the scaled integer forms of a number, by tag. */
  inline constexpr double NUMBER_SCALES[] = {1, 10, 100, 1000, 10000, 255};

  /** The tag of a number stored as its raw IEEE 754 bits. */
  inline constexpr auto RAW_NUMBER_TAG = std::uint64_t(7);

  /** The largest magnitude of a scaled integer form of a number. */
  inline constexpr auto MAXIMUM_SCALED = std::int64_t(1) << 53;

  /** Appends little-endian binary values to a growing buffer. */
  class ByteWriter {
    public:

      /** Returns the bytes written. */
      const std::vector<std::byte>& get_data() const;

      /** Returns the number of bytes written. */
      std::size_t get_size() const;

      /** Returns the bytes written, leaving the writer empty. */
      std::vector<std::byte> release();

      void write_byte(std::uint8_t value);
      void write_bool(bool value);
      void write_u16(std::uint16_t value);
      void write_u32(std::uint32_t value);
      void write_u64(std::uint64_t value);

      /**
       * Writes an unsigned integer in LEB128 form.
       * @param value The value to write.
       */
      void write_varint(std::uint64_t value);

      /**
       * Writes a signed integer in zigzag LEB128 form.
       * @param value The value to write.
       */
      void write_signed(std::int64_t value);

      /**
       * Writes a double exactly, as a scaled integer when one reproduces its
       * bits and as its raw bits otherwise.
       * @param value The value to write.
       */
      void write_number(double value);

      /**
       * Writes the raw bits of a double.
       * @param value The value to write.
       */
      void write_double(double value);

      /**
       * Writes bytes as they are.
       * @param data The bytes to write.
       * @param size The number of bytes.
       */
      void write_bytes(const void* data, std::size_t size);

      /**
       * Writes a string as its length followed by its bytes.
       * @param value The string to write.
       */
      void write_string(std::string_view value);

    private:
      std::vector<std::byte> m_data;
  };

  inline const std::vector<std::byte>& ByteWriter::get_data() const {
    return m_data;
  }

  inline std::size_t ByteWriter::get_size() const {
    return m_data.size();
  }

  inline std::vector<std::byte> ByteWriter::release() {
    return std::move(m_data);
  }

  inline void ByteWriter::write_byte(std::uint8_t value) {
    m_data.push_back(static_cast<std::byte>(value));
  }

  inline void ByteWriter::write_bool(bool value) {
    write_byte(value ? 1 : 0);
  }

  inline void ByteWriter::write_u16(std::uint16_t value) {
    auto bytes = std::array<std::byte, 2>();
    for(auto i = std::size_t(0); i != bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(value >> (8 * i));
    }
    write_bytes(bytes.data(), bytes.size());
  }

  inline void ByteWriter::write_u32(std::uint32_t value) {
    auto bytes = std::array<std::byte, 4>();
    for(auto i = std::size_t(0); i != bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(value >> (8 * i));
    }
    write_bytes(bytes.data(), bytes.size());
  }

  inline void ByteWriter::write_u64(std::uint64_t value) {
    auto bytes = std::array<std::byte, 8>();
    for(auto i = std::size_t(0); i != bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(value >> (8 * i));
    }
    write_bytes(bytes.data(), bytes.size());
  }

  inline void ByteWriter::write_varint(std::uint64_t value) {
    auto bytes = std::array<std::byte, 10>();
    auto size = std::size_t(0);
    while(value >= 0x80) {
      bytes[size++] = static_cast<std::byte>((value & 0x7F) | 0x80);
      value >>= 7;
    }
    bytes[size++] = static_cast<std::byte>(value);
    write_bytes(bytes.data(), size);
  }

  inline void ByteWriter::write_signed(std::int64_t value) {
    write_varint((static_cast<std::uint64_t>(value) << 1) ^
      static_cast<std::uint64_t>(value >> 63));
  }

  inline void ByteWriter::write_number(double value) {
    auto bits = std::bit_cast<std::uint64_t>(value);
    if(std::isfinite(value)) {
      for(auto tag = std::size_t(0); tag != std::size(NUMBER_SCALES); ++tag) {
        auto scaled = std::round(value * NUMBER_SCALES[tag]);
        if(std::abs(scaled) > static_cast<double>(MAXIMUM_SCALED)) {
          continue;
        }
        auto integer = static_cast<std::int64_t>(scaled);
        if(std::bit_cast<std::uint64_t>(static_cast<double>(integer) /
            NUMBER_SCALES[tag]) == bits) {
          auto zigzag = (static_cast<std::uint64_t>(integer) << 1) ^
            static_cast<std::uint64_t>(integer >> 63);
          write_varint((zigzag << 3) | tag);
          return;
        }
      }
    }
    write_byte(static_cast<std::uint8_t>(RAW_NUMBER_TAG));
    write_double(value);
  }

  inline void ByteWriter::write_double(double value) {
    write_u64(std::bit_cast<std::uint64_t>(value));
  }

  inline void ByteWriter::write_bytes(const void* data, std::size_t size) {
    auto bytes = static_cast<const std::byte*>(data);
    m_data.insert(m_data.end(), bytes, bytes + size);
  }

  inline void ByteWriter::write_string(std::string_view value) {
    write_varint(value.size());
    write_bytes(value.data(), value.size());
  }
}

#endif

#ifndef RELLU_FORMAT_CONTAINER_HPP
#define RELLU_FORMAT_CONTAINER_HPP
#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace Rellu::Details {

  /** The four characters naming a kind of chunk. */
  using ChunkType = std::array<char, 4>;

  /** The chunk describing the document's counts. */
  inline constexpr auto INFO_CHUNK = ChunkType({'I', 'N', 'F', 'O'});

  /** The chunk holding the identifier table. */
  inline constexpr auto IDS_CHUNK = ChunkType({'I', 'D', 'S', ' '});

  /** A chunk holding a contiguous run of nodes in pre-order. */
  inline constexpr auto NODE_CHUNK = ChunkType({'N', 'O', 'D', 'E'});

  /** The chunk holding the resources apart from image bytes. */
  inline constexpr auto RESOURCES_CHUNK = ChunkType({'R', 'S', 'R', 'C'});

  /** The chunk holding the prototype. */
  inline constexpr auto PROTOTYPE_CHUNK = ChunkType({'P', 'R', 'O', 'T'});

  /** A chunk holding the encoded bytes of one image. */
  inline constexpr auto IMAGE_CHUNK = ChunkType({'I', 'M', 'A', 'G'});

  /** The chunk ending the file. */
  inline constexpr auto END_CHUNK = ChunkType({'E', 'N', 'D', ' '});

  /** The chunk flag marking deflate compressed data. */
  inline constexpr auto COMPRESSED_FLAG = std::uint32_t(1);

  /** The size of a chunk's header in bytes. */
  inline constexpr auto CHUNK_HEADER_SIZE = std::size_t(28);

  /** Describes a chunk. */
  struct ChunkHeader {

    /** The kind of chunk. */
    ChunkType m_type = ChunkType();

    /** The flags describing how the data is stored. */
    std::uint32_t m_flags = 0;

    /** The number of bytes stored. */
    std::uint64_t m_stored_size = 0;

    /** The number of bytes once decompressed. */
    std::uint64_t m_size = 0;

    /** The CRC-32 of the header's other fields and the stored bytes. */
    std::uint32_t m_checksum = 0;
  };

  /** A chunk ready to be written. */
  struct OutputChunk {

    /** The chunk's header. */
    ChunkHeader m_header;

    /** The bytes owned by the chunk, if any. */
    std::vector<std::byte> m_storage;

    /** The bytes stored, either the storage or bytes owned elsewhere. */
    std::span<const std::byte> m_data;
  };

  /** A chunk read from a file. */
  struct InputChunk {

    /** The chunk's header. */
    ChunkHeader m_header;

    /** The bytes stored. */
    std::vector<std::byte> m_data;
  };

  /**
   * Tests whether a reader must understand a kind of chunk, which is the
   * case when its name starts with an upper case letter.
   * @param type The kind of chunk.
   */
  bool is_critical(const ChunkType& type);

  /**
   * Returns the printable name of a kind of chunk.
   * @param type The kind of chunk.
   */
  std::string to_string(const ChunkType& type);

  /**
   * Makes a chunk owning its data, compressing it when that makes it
   * smaller.
   * @param type The kind of chunk.
   * @param data The chunk's data.
   * @param level The deflate compression level, 0 to store the data as is.
   */
  OutputChunk pack(ChunkType type, std::vector<std::byte> data, int level);

  /**
   * Makes a chunk storing data owned elsewhere as it is.
   * @param type The kind of chunk.
   * @param data The chunk's data, which must outlive the chunk.
   */
  OutputChunk pack_view(ChunkType type, std::span<const std::byte> data);

  /**
   * Verifies a chunk's checksum and returns its decompressed data, throwing
   * FormatError if it is corrupt.
   * @param chunk The chunk, whose data is consumed.
   */
  std::vector<std::byte> unpack(InputChunk& chunk);

  /**
   * Writes the file header.
   * @param out The stream to write to.
   * @param version The format version.
   */
  void write_file_header(std::ostream& out, std::uint32_t version);

  /**
   * Reads the file header, throwing FormatError if it is not that of a
   * Rellu document.
   * @param in The stream to read from.
   * @return The format version.
   */
  std::uint32_t read_file_header(std::istream& in);

  /**
   * Writes a chunk.
   * @param out The stream to write to.
   * @param chunk The chunk to write.
   */
  void write_chunk(std::ostream& out, const OutputChunk& chunk);

  /**
   * Reads a chunk, throwing FormatError if the file ends within it.
   * @param in The stream to read from.
   * @return The chunk.
   */
  InputChunk read_chunk(std::istream& in);
}

#endif

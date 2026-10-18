#include <algorithm>
#include <optional>
#include <QtZlib/zlib.h>
#include "Rellu/Format/FormatError.hpp"
#include "ByteReader.hpp"
#include "ByteWriter.hpp"
#include "Container.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  constexpr auto SIGNATURE = std::array<std::uint8_t, 8>(
    {0x89, 'R', 'E', 'L', 'L', 'U', 0x0D, 0x0A});
  constexpr auto FILE_HEADER_SIZE = std::size_t(16);
  constexpr auto MAXIMUM_CHUNK_SIZE = std::uint64_t(1) << 40;
  constexpr auto MAXIMUM_DEFLATE_RATIO = std::uint64_t(1032);
  constexpr auto READ_SIZE = std::size_t(1) << 26;
  constexpr auto STEP_SIZE = std::size_t(1) << 30;
  constexpr auto MAXIMUM_COMPRESSED_SIZE = std::size_t(1) << 31;

  std::array<std::byte, CHUNK_HEADER_SIZE> encode_header(
      const ChunkHeader& header) {
    auto writer = ByteWriter();
    writer.write_bytes(header.m_type.data(), header.m_type.size());
    writer.write_u32(header.m_flags);
    writer.write_u64(header.m_stored_size);
    writer.write_u64(header.m_size);
    writer.write_u32(header.m_checksum);
    auto bytes = std::array<std::byte, CHUNK_HEADER_SIZE>();
    std::copy(writer.get_data().begin(), writer.get_data().end(),
      bytes.begin());
    return bytes;
  }

  std::uint32_t compute_checksum(
      const ChunkHeader& header, std::span<const std::byte> data) {
    auto bytes = encode_header(header);
    auto checksum = crc32_z(0, reinterpret_cast<const Bytef*>(bytes.data()),
      CHUNK_HEADER_SIZE - 4);
    auto position = data.data();
    auto remaining = data.size();
    while(remaining != 0) {
      auto step = std::min(remaining, STEP_SIZE);
      checksum = crc32_z(
        checksum, reinterpret_cast<const Bytef*>(position), step);
      position += step;
      remaining -= step;
    }
    return static_cast<std::uint32_t>(checksum);
  }

  std::optional<std::vector<std::byte>> compress(
      std::span<const std::byte> data, int level) {
    if(data.size() > MAXIMUM_COMPRESSED_SIZE) {
      return std::nullopt;
    }
    auto stream = z_stream();
    if(deflateInit2(&stream, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
      throw FormatError("Cannot initialize compression.");
    }
    auto output = std::vector<std::byte>(
      deflateBound(&stream, static_cast<uLong>(data.size())));
    stream.next_in =
      const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    auto status = deflate(&stream, Z_FINISH);
    output.resize(static_cast<std::size_t>(stream.total_out));
    deflateEnd(&stream);
    if(status != Z_STREAM_END) {
      throw FormatError("Compression failed.");
    }
    return output;
  }

  std::vector<std::byte> decompress(std::span<const std::byte> data,
      std::uint64_t size, const ChunkType& type) {
    auto error = FormatError(
      "Corrupt " + to_string(type) + " chunk: invalid compressed data.");
    if(size > MAXIMUM_COMPRESSED_SIZE ||
        data.size() > MAXIMUM_COMPRESSED_SIZE) {
      throw error;
    }
    auto stream = z_stream();
    if(inflateInit2(&stream, -15) != Z_OK) {
      throw FormatError("Cannot initialize decompression.");
    }
    auto output = std::vector<std::byte>(static_cast<std::size_t>(size));
    auto sink = std::byte();
    stream.next_in =
      const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    if(output.empty()) {
      stream.next_out = reinterpret_cast<Bytef*>(&sink);
      stream.avail_out = 1;
    } else {
      stream.next_out = reinterpret_cast<Bytef*>(output.data());
      stream.avail_out = static_cast<uInt>(output.size());
    }
    auto status = inflate(&stream, Z_FINISH);
    auto is_complete = status == Z_STREAM_END &&
      stream.total_out == output.size() && stream.avail_in == 0;
    inflateEnd(&stream);
    if(!is_complete) {
      throw error;
    }
    return output;
  }

  void read_exactly(std::istream& in, std::byte* data, std::size_t size) {
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    if(static_cast<std::size_t>(in.gcount()) != size) {
      throw FormatError("Truncated file.");
    }
  }
}

bool Rellu::Details::is_critical(const ChunkType& type) {
  return type[0] >= 'A' && type[0] <= 'Z';
}

std::string Rellu::Details::to_string(const ChunkType& type) {
  auto name = std::string();
  for(auto character : type) {
    if(character >= 0x20 && character < 0x7F) {
      name += character;
    } else {
      name += '?';
    }
  }
  while(!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  return name;
}

OutputChunk Rellu::Details::pack(
    ChunkType type, std::vector<std::byte> data, int level) {
  auto chunk = OutputChunk();
  chunk.m_header.m_type = type;
  chunk.m_header.m_size = data.size();
  if(level > 0 && !data.empty()) {
    auto compressed = compress(data, std::min(level, 9));
    if(compressed && compressed->size() < data.size()) {
      chunk.m_header.m_flags = COMPRESSED_FLAG;
      data = std::move(*compressed);
    }
  }
  chunk.m_storage = std::move(data);
  chunk.m_data = chunk.m_storage;
  chunk.m_header.m_stored_size = chunk.m_data.size();
  chunk.m_header.m_checksum = compute_checksum(chunk.m_header, chunk.m_data);
  return chunk;
}

OutputChunk Rellu::Details::pack_view(
    ChunkType type, std::span<const std::byte> data) {
  auto chunk = OutputChunk();
  chunk.m_header.m_type = type;
  chunk.m_header.m_size = data.size();
  chunk.m_header.m_stored_size = data.size();
  chunk.m_data = data;
  chunk.m_header.m_checksum = compute_checksum(chunk.m_header, data);
  return chunk;
}

std::vector<std::byte> Rellu::Details::unpack(InputChunk& chunk) {
  auto& header = chunk.m_header;
  if(compute_checksum(header, chunk.m_data) != header.m_checksum) {
    throw FormatError(
      "Corrupt " + to_string(header.m_type) + " chunk: checksum mismatch.");
  }
  if(header.m_flags & COMPRESSED_FLAG) {
    auto data = decompress(chunk.m_data, header.m_size, header.m_type);
    chunk.m_data = std::vector<std::byte>();
    return data;
  }
  return std::move(chunk.m_data);
}

void Rellu::Details::write_file_header(
    std::ostream& out, std::uint32_t version) {
  auto writer = ByteWriter();
  writer.write_bytes(SIGNATURE.data(), SIGNATURE.size());
  writer.write_u32(version);
  writer.write_u32(0);
  out.write(reinterpret_cast<const char*>(writer.get_data().data()),
    static_cast<std::streamsize>(writer.get_size()));
}

std::uint32_t Rellu::Details::read_file_header(std::istream& in) {
  auto bytes = std::array<std::byte, FILE_HEADER_SIZE>();
  in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  auto size = static_cast<std::size_t>(in.gcount());
  auto is_signed = size >= SIGNATURE.size() && std::equal(SIGNATURE.begin(),
    SIGNATURE.end(), bytes.begin(),
    [] (const auto& expected, const auto& actual) {
      return static_cast<std::byte>(expected) == actual;
    });
  if(!is_signed) {
    throw FormatError("Not a Rellu document.");
  }
  if(size != bytes.size()) {
    throw FormatError("Truncated file.");
  }
  auto reader = ByteReader(
    std::span(bytes).subspan(SIGNATURE.size()), "file header");
  return reader.read_u32();
}

void Rellu::Details::write_chunk(std::ostream& out, const OutputChunk& chunk) {
  auto header = encode_header(chunk.m_header);
  out.write(reinterpret_cast<const char*>(header.data()), header.size());
  auto position = chunk.m_data.data();
  auto remaining = chunk.m_data.size();
  while(remaining != 0) {
    auto step = std::min(remaining, STEP_SIZE);
    out.write(reinterpret_cast<const char*>(position),
      static_cast<std::streamsize>(step));
    position += step;
    remaining -= step;
  }
}

InputChunk Rellu::Details::read_chunk(std::istream& in) {
  auto bytes = std::array<std::byte, CHUNK_HEADER_SIZE>();
  read_exactly(in, bytes.data(), bytes.size());
  auto reader = ByteReader(bytes, "chunk header");
  auto chunk = InputChunk();
  auto& header = chunk.m_header;
  for(auto& character : header.m_type) {
    character = static_cast<char>(reader.read_byte());
  }
  header.m_flags = reader.read_u32();
  header.m_stored_size = reader.read_u64();
  header.m_size = reader.read_u64();
  header.m_checksum = reader.read_u32();
  auto name = to_string(header.m_type);
  if(header.m_stored_size > MAXIMUM_CHUNK_SIZE ||
      header.m_size > MAXIMUM_CHUNK_SIZE) {
    throw FormatError("Corrupt " + name + " chunk: size out of range.");
  }
  if(header.m_flags & COMPRESSED_FLAG) {
    if(header.m_size > MAXIMUM_DEFLATE_RATIO * header.m_stored_size + 64) {
      throw FormatError("Corrupt " + name + " chunk: size out of range.");
    }
  } else if(header.m_size != header.m_stored_size) {
    throw FormatError("Corrupt " + name + " chunk: size mismatch.");
  }
  auto remaining = static_cast<std::size_t>(header.m_stored_size);
  while(remaining != 0) {
    auto step = std::min(remaining, READ_SIZE);
    auto offset = chunk.m_data.size();
    chunk.m_data.resize(offset + step);
    read_exactly(in, chunk.m_data.data() + offset, step);
    remaining -= step;
  }
  return chunk;
}

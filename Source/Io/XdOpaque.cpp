#include <array>
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPatch.hpp"

using namespace Rellu;

namespace {
  constexpr auto NODE_VERSION = '\1';
  constexpr auto PACKAGE_VERSION = '\1';
  constexpr auto HAS_CACHE = std::uint64_t(1);
  constexpr auto HAS_PATCH = std::uint64_t(2);
  constexpr auto HAS_ORPHANS = std::uint64_t(4);
  constexpr auto HAS_PART = std::uint64_t(8);

  enum class Section : unsigned char {
    END,
    ENTRIES,
    PARTS,
    MANIFEST,
    RESOURCES,
    PASTEBOARD,
    INTERACTIONS,
    NO_INTERACTIONS,
    IMAGE_NAMES,
    SYMBOLS
  };

  void encode_id(const NodeId& id, std::string& out) {
    for(auto byte : id.get_bytes()) {
      out += static_cast<char>(byte);
    }
  }

  NodeId decode_id(std::string_view& data) {
    auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
    if(data.size() < NodeId::SIZE) {
      data = std::string_view();
      return NodeId();
    }
    for(auto i = std::size_t(0); i != NodeId::SIZE; ++i) {
      bytes[i] = static_cast<std::uint8_t>(data[i]);
    }
    data.remove_prefix(NodeId::SIZE);
    return NodeId(bytes);
  }
}

std::string Rellu::encode_node_data(const XdNodeData& data) {
  auto flags = std::uint64_t(0);
  if(data.m_has_cache) {
    flags |= HAS_CACHE;
  }
  if(!data.m_patch.empty()) {
    flags |= HAS_PATCH;
  }
  if(!data.m_orphans.empty()) {
    flags |= HAS_ORPHANS;
  }
  if(!data.m_part.empty()) {
    flags |= HAS_PART;
  }
  if((flags & ~HAS_CACHE) == 0) {
    return std::string();
  }
  auto out = std::string();
  out += NODE_VERSION;
  encode_varint(flags, out);
  if(data.m_has_cache) {
    encode_fixed(data.m_cache_hash, out);
  }
  if(!data.m_patch.empty()) {
    encode_string(data.m_patch, out);
  }
  if(!data.m_orphans.empty()) {
    encode_varint(data.m_orphans.size(), out);
    for(auto& orphan : data.m_orphans) {
      encode_id(orphan.first, out);
      encode_string(orphan.second, out);
    }
  }
  if(!data.m_part.empty()) {
    encode_string(data.m_part, out);
  }
  return out;
}

XdNodeData Rellu::decode_node_data(std::string_view bytes) {
  auto data = XdNodeData();
  if(bytes.empty() || bytes.front() != NODE_VERSION) {
    return data;
  }
  bytes.remove_prefix(1);
  auto flags = decode_varint(bytes);
  if(flags & HAS_CACHE) {
    data.m_has_cache = true;
    data.m_cache_hash = decode_fixed(bytes);
  }
  if(flags & HAS_PATCH) {
    data.m_patch = decode_string(bytes);
  }
  if(flags & HAS_ORPHANS) {
    auto count = decode_varint(bytes);
    for(auto i = std::uint64_t(0); i != count && !bytes.empty(); ++i) {
      auto anchor = decode_id(bytes);
      data.m_orphans.emplace_back(anchor, decode_string(bytes));
    }
  }
  if(flags & HAS_PART) {
    data.m_part = decode_string(bytes);
  }
  return data;
}

std::string Rellu::encode_package_data(const XdPackageData& data) {
  auto out = std::string();
  out += PACKAGE_VERSION;
  out += static_cast<char>(Section::ENTRIES);
  encode_varint(data.m_entries.size(), out);
  for(auto& entry : data.m_entries) {
    encode_string(entry, out);
  }
  out += static_cast<char>(Section::PARTS);
  encode_varint(data.m_parts.size(), out);
  for(auto& part : data.m_parts) {
    encode_string(part.first, out);
    encode_string(part.second, out);
  }
  if(!data.m_manifest.empty()) {
    out += static_cast<char>(Section::MANIFEST);
    encode_string(data.m_manifest, out);
  }
  for(auto& section : {std::pair(Section::RESOURCES, &data.m_resources),
      std::pair(Section::PASTEBOARD, &data.m_pasteboard),
      std::pair(Section::INTERACTIONS, &data.m_interactions)}) {
    if(!section.second->empty()) {
      out += static_cast<char>(section.first);
      encode_string(*section.second, out);
    }
  }
  if(!data.m_has_interactions) {
    out += static_cast<char>(Section::NO_INTERACTIONS);
  }
  if(!data.m_image_names.empty()) {
    out += static_cast<char>(Section::IMAGE_NAMES);
    encode_varint(data.m_image_names.size(), out);
    for(auto& name : data.m_image_names) {
      encode_id(name.first, out);
      encode_string(name.second, out);
    }
  }
  if(!data.m_symbols.empty()) {
    out += static_cast<char>(Section::SYMBOLS);
    encode_varint(data.m_symbols.size(), out);
    for(auto& symbol : data.m_symbols) {
      encode_id(symbol, out);
    }
  }
  out += static_cast<char>(Section::END);
  return out;
}

XdPackageData Rellu::decode_package_data(std::string_view bytes) {
  auto data = XdPackageData();
  if(bytes.empty() || bytes.front() != PACKAGE_VERSION) {
    return data;
  }
  bytes.remove_prefix(1);
  while(!bytes.empty()) {
    auto section = static_cast<Section>(bytes.front());
    bytes.remove_prefix(1);
    switch(section) {
      case Section::ENTRIES:
        {
          auto count = decode_varint(bytes);
          for(auto i = std::uint64_t(0); i != count && !bytes.empty(); ++i) {
            data.m_entries.emplace_back(decode_string(bytes));
          }
        }
        break;
      case Section::PARTS:
        {
          auto count = decode_varint(bytes);
          for(auto i = std::uint64_t(0); i != count && !bytes.empty(); ++i) {
            auto name = std::string(decode_string(bytes));
            data.m_parts.emplace_back(
              std::move(name), std::string(decode_string(bytes)));
          }
        }
        break;
      case Section::MANIFEST:
        data.m_manifest = decode_string(bytes);
        break;
      case Section::RESOURCES:
        data.m_resources = decode_string(bytes);
        break;
      case Section::PASTEBOARD:
        data.m_pasteboard = decode_string(bytes);
        break;
      case Section::INTERACTIONS:
        data.m_interactions = decode_string(bytes);
        break;
      case Section::NO_INTERACTIONS:
        data.m_has_interactions = false;
        break;
      case Section::IMAGE_NAMES:
        {
          auto count = decode_varint(bytes);
          for(auto i = std::uint64_t(0); i != count && !bytes.empty(); ++i) {
            auto id = decode_id(bytes);
            data.m_image_names.emplace_back(
              id, std::string(decode_string(bytes)));
          }
        }
        break;
      case Section::SYMBOLS:
        {
          auto count = decode_varint(bytes);
          for(auto i = std::uint64_t(0); i != count && !bytes.empty(); ++i) {
            data.m_symbols.push_back(decode_id(bytes));
          }
        }
        break;
      default:
        return data;
    }
  }
  return data;
}

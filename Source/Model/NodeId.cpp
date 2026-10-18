#include <random>
#include "Rellu/Model/NodeId.hpp"

using namespace Rellu;

namespace {
  int get_digit(char character) {
    if(character >= '0' && character <= '9') {
      return character - '0';
    } else if(character >= 'a' && character <= 'f') {
      return character - 'a' + 10;
    } else if(character >= 'A' && character <= 'F') {
      return character - 'A' + 10;
    }
    return -1;
  }

  bool is_hyphen_position(std::size_t index) {
    return index == 8 || index == 13 || index == 18 || index == 23;
  }

  std::mt19937_64& get_generator() {
    thread_local auto generator = [] {
      auto device = std::random_device();
      auto seed = std::seed_seq(
        {device(), device(), device(), device(), device(), device()});
      return std::mt19937_64(seed);
    }();
    return generator;
  }
}

NodeId Rellu::generate_node_id() {
  auto& generator = get_generator();
  auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
  for(auto i = std::size_t(0); i != NodeId::SIZE; i += 8) {
    auto value = generator();
    for(auto j = std::size_t(0); j != 8; ++j) {
      bytes[i + j] = static_cast<std::uint8_t>(value >> (8 * j));
    }
  }
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
  return NodeId(bytes);
}

std::optional<NodeId> Rellu::parse_node_id(std::string_view source) {
  auto is_canonical = source.size() == 36;
  if(!is_canonical && source.size() != 32) {
    return std::nullopt;
  }
  auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
  auto count = std::size_t(0);
  for(auto i = std::size_t(0); i != source.size(); ++i) {
    if(is_canonical && is_hyphen_position(i)) {
      if(source[i] != '-') {
        return std::nullopt;
      }
      continue;
    }
    auto digit = get_digit(source[i]);
    if(digit < 0) {
      return std::nullopt;
    }
    auto& byte = bytes[count / 2];
    byte = static_cast<std::uint8_t>((byte << 4) | digit);
    ++count;
  }
  return NodeId(bytes);
}

std::string Rellu::to_string(const NodeId& id) {
  static constexpr auto DIGITS = std::string_view("0123456789abcdef");
  auto result = std::string();
  result.reserve(36);
  auto& bytes = id.get_bytes();
  for(auto i = std::size_t(0); i != NodeId::SIZE; ++i) {
    if(i == 4 || i == 6 || i == 8 || i == 10) {
      result.push_back('-');
    }
    result.push_back(DIGITS[bytes[i] >> 4]);
    result.push_back(DIGITS[bytes[i] & 0x0F]);
  }
  return result;
}

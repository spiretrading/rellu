#ifndef RELLU_MODEL_NODE_ID_HPP
#define RELLU_MODEL_NODE_ID_HPP
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace Rellu {

  /** A 128-bit universally unique identifier. A default NodeId is nil. */
  class NodeId {
    public:

      /** The number of bytes in a NodeId. */
      static constexpr auto SIZE = std::size_t(16);

      /** Constructs the nil NodeId, whose bytes are all zero. */
      constexpr NodeId() noexcept;

      /**
       * Constructs a NodeId from its bytes in canonical text order.
       * @param bytes The bytes making up the identifier.
       */
      constexpr explicit NodeId(
        const std::array<std::uint8_t, SIZE>& bytes) noexcept;

      /** Returns the bytes in canonical text order. */
      constexpr const std::array<std::uint8_t, SIZE>& get_bytes() const;

      /** Returns <code>true</code> iff every byte is zero. */
      constexpr bool is_nil() const;

      auto operator <=>(const NodeId&) const = default;

    private:
      std::array<std::uint8_t, SIZE> m_bytes;
  };

  /** Identifies a component, as Adobe XD's <code>symbolId</code>. */
  using ComponentId = NodeId;

  /** Identifies a state of a component. */
  using StateId = NodeId;

  /** Identifies a prototype interaction. */
  using InteractionId = NodeId;

  /** Identifies an image resource, as Adobe XD's resource uid. */
  using ImageId = NodeId;

  /** Identifies a color swatch. */
  using SwatchId = NodeId;

  /** Identifies a character style. */
  using CharacterStyleId = NodeId;

  /** Returns a freshly generated random (version 4) NodeId. */
  NodeId generate_node_id();

  /**
   * Parses a NodeId from text, ignoring case.
   * @param source Either the canonical form of 32 hexadecimal digits in
   *        groups of 8, 4, 4, 4 and 12 separated by hyphens, or the same 32
   *        digits without hyphens.
   * @return The parsed NodeId, or <code>std::nullopt</code> if the
   *         <i>source</i> is malformed.
   */
  std::optional<NodeId> parse_node_id(std::string_view source);

  /**
   * Formats a NodeId in canonical form with lowercase digits.
   * @param id The identifier to format.
   */
  std::string to_string(const NodeId& id);

  inline std::ostream& operator <<(std::ostream& out, const NodeId& id) {
    return out << to_string(id);
  }

  constexpr NodeId::NodeId() noexcept
    : m_bytes() {}

  constexpr NodeId::NodeId(
    const std::array<std::uint8_t, SIZE>& bytes) noexcept
    : m_bytes(bytes) {}

  constexpr const std::array<std::uint8_t, NodeId::SIZE>&
      NodeId::get_bytes() const {
    return m_bytes;
  }

  constexpr bool NodeId::is_nil() const {
    for(auto byte : m_bytes) {
      if(byte != 0) {
        return false;
      }
    }
    return true;
  }
}

template<>
struct std::hash<Rellu::NodeId> {
  std::size_t operator ()(const Rellu::NodeId& id) const noexcept {
    auto& bytes = id.get_bytes();
    auto high = std::uint64_t(0);
    auto low = std::uint64_t(0);
    for(auto i = std::size_t(0); i != 8; ++i) {
      high = (high << 8) | bytes[i];
      low = (low << 8) | bytes[i + 8];
    }
    auto hash = high ^ (low * 0x9E3779B97F4A7C15ULL);
    hash ^= hash >> 32;
    return static_cast<std::size_t>(hash);
  }
};

#endif

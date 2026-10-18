#ifndef RELLU_FORMAT_CODEC_HPP
#define RELLU_FORMAT_CODEC_HPP
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Prototype.hpp"
#include "Rellu/Model/Resources.hpp"
#include "IdIndex.hpp"

namespace Rellu::Details {

  /** A node decoded from a file, apart from its links to other nodes. */
  struct NodeRecord {

    /** The node's header. */
    NodeHeader m_header;

    /** The node's payload. */
    Payload m_payload;

    /** The number of children following the node in pre-order. */
    std::size_t m_child_count = 0;
  };

  /** A contiguous run of nodes in pre-order decoded from a file. */
  struct NodeBlock {

    /** The pre-order position of the first node. */
    std::size_t m_first = 0;

    /** The nodes. */
    std::vector<NodeRecord> m_records;
  };

  /** Describes an image whose bytes are stored in their own chunk. */
  struct ImageEntry {

    /** The image's identifier. */
    ImageId m_id;

    /** The media type of the encoding. */
    std::string m_media_type;

    /** The number of encoded bytes. */
    std::uint64_t m_size = 0;
  };

  /** The resources decoded from a file, apart from the image bytes. */
  struct ResourceRecord {

    /** The components in identifier order. */
    std::vector<Component> m_components;

    /** The color swatches in library order. */
    std::vector<ColorSwatch> m_swatches;

    /** The character styles in library order. */
    std::vector<CharacterStyle> m_character_styles;

    /** The images in the order their chunks are stored. */
    std::vector<ImageEntry> m_images;
  };

  /**
   * Encodes a run of nodes in pre-order, each followed in the run or a later
   * one by its descendants.
   * @param nodes The nodes to encode.
   * @param first The pre-order position of the first node.
   * @param ids The position of each identifier in the identifier table.
   * @return The encoded block.
   */
  std::vector<std::byte> encode_nodes(
    std::span<const Node* const> nodes, std::size_t first, const IdIndex& ids);

  /**
   * Decodes a run of nodes.
   * @param data The encoded block.
   * @param ids The identifier table, whose first entries are the nodes' ids
   *        in pre-order.
   * @param node_count The number of nodes in the file.
   * @param version The format version of the file.
   * @return The decoded nodes.
   */
  NodeBlock decode_nodes(std::span<const std::byte> data,
    std::span<const NodeId> ids, std::size_t node_count,
    std::uint32_t version);

  /**
   * Encodes the components, swatches, character styles and image entries.
   * @param resources The resources to encode.
   * @param ids The position of each identifier in the identifier table.
   * @return The encoded resources.
   */
  std::vector<std::byte> encode_resources(
    const Resources& resources, const IdIndex& ids);

  /**
   * Decodes the resources.
   * @param data The encoded resources.
   * @param ids The identifier table.
   * @param version The format version of the file.
   * @return The decoded resources.
   */
  ResourceRecord decode_resources(std::span<const std::byte> data,
    std::span<const NodeId> ids, std::uint32_t version);

  /**
   * Encodes the prototype.
   * @param prototype The prototype to encode.
   * @param ids The position of each identifier in the identifier table.
   * @return The encoded prototype.
   */
  std::vector<std::byte> encode_prototype(
    const Prototype& prototype, const IdIndex& ids);

  /**
   * Decodes the prototype.
   * @param data The encoded prototype.
   * @param ids The identifier table.
   * @param version The format version of the file.
   * @return The decoded prototype.
   */
  Prototype decode_prototype(std::span<const std::byte> data,
    std::span<const NodeId> ids, std::uint32_t version);
}

#endif

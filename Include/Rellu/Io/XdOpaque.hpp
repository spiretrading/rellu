#ifndef RELLU_IO_XD_OPAQUE_HPP
#define RELLU_IO_XD_OPAQUE_HPP
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * What an Adobe XD file holds for a node beyond the model, kept as the
   * node's opaque data: the patch turning the JSON the writer generates for
   * the node into the JSON it was read from.
   */
  struct XdNodeData {

    /**
     * Whether the patch restores cached members, such as visualBounds, that
     * apply only while the node is unchanged.
     */
    bool m_has_cache = false;

    /** The hash of the members the cached members derive from. */
    std::uint64_t m_cache_hash = 0;

    /** The patch of the node's JSON, empty if there is none. */
    std::string_view m_patch;

    /**
     * The children that could not be imported, each with the id of the
     * imported child it precedes, nil to follow every child, and its
     * encoded JSON.
     */
    std::vector<std::pair<NodeId, std::string_view>> m_orphans;

    /**
     * The patch of the root of the entry holding an artboard, empty if
     * there is none.
     */
    std::string_view m_part;
  };

  /**
   * Encodes the data of a node.
   * @param data The data.
   * @return The encoding, empty if the data is empty.
   */
  std::string encode_node_data(const XdNodeData& data);

  /**
   * Decodes the data of a node.
   * @param bytes The encoding, which the result refers to.
   * @return The data, empty if the encoding is empty or malformed.
   */
  XdNodeData decode_node_data(std::string_view bytes);

  /**
   * What an Adobe XD package holds beyond the model, kept as the document's
   * opaque data.
   */
  struct XdPackageData {

    /** The names of the package's entries, in archive order. */
    std::vector<std::string> m_entries;

    /**
     * The entries kept as they are, such as previews and renditions, by
     * name.
     */
    std::vector<std::pair<std::string, std::string>> m_parts;

    /** The JSON text of the manifest as read, empty if there is none. */
    std::string m_manifest;

    /** The patch of the root of the shared resources entry. */
    std::string m_resources;

    /** The patch of the root of the pasteboard entry. */
    std::string m_pasteboard;

    /** The patch of the prototype interactions entry. */
    std::string m_interactions;

    /** Whether the package has a prototype interactions entry. */
    bool m_has_interactions = true;

    /**
     * The entry names of images not named resources/ followed by their
     * uid, by image id.
     */
    std::vector<std::pair<NodeId, std::string>> m_image_names;

    /** The components in the order their masters are written. */
    std::vector<NodeId> m_symbols;
  };

  /**
   * Encodes the data of a package.
   * @param data The data.
   */
  std::string encode_package_data(const XdPackageData& data);

  /**
   * Decodes the data of a package.
   * @param bytes The encoding.
   * @return The data, empty if the encoding is empty or malformed.
   */
  XdPackageData decode_package_data(std::string_view bytes);
}

#endif

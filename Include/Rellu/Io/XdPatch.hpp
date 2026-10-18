#ifndef RELLU_IO_XD_PATCH_HPP
#define RELLU_IO_XD_PATCH_HPP
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <QJsonValue>
#include "Rellu/Io/XdValue.hpp"

namespace Rellu {

  /**
   * The paths, from the root of a value, of the arrays a patch leaves to the
   * writer, such as the children of a node, each written from the model.
   */
  struct XdStructure {

    /** The paths, each a sequence of object keys. */
    std::vector<std::vector<std::string_view>> m_paths;

    /** The structure of a node: its children and component states. */
    static const XdStructure& get_node();

    /** The structure of the root of an artboard or pasteboard entry. */
    static const XdStructure& get_part();

    /** The structure of the root of the shared resources entry. */
    static const XdStructure& get_resources();

    /** The structure of a value without structural arrays. */
    static const XdStructure& get_none();
  };

  /**
   * Counts the entries of patches by kind and normalized path, such as
   * add &lt;shape&gt;.meta.ux.hasCustomName, measuring how much of an Adobe
   * XD file the writer does not reproduce on its own.
   */
  using XdPatchCounts = std::map<std::string, std::size_t, std::less<>>;

  /** The result of comparing an original value with a generated one. */
  struct XdDifference {

    /** The encoded patch, empty if the values are equal. */
    std::string m_patch;

    /**
     * Whether the patch adds a member that is a cache of other members, such
     * as visualBounds, which is restored only while its node is unchanged.
     */
    bool m_has_cache = false;
  };

  /**
   * Computes the patch turning a generated value into an original one.
   * Members only the original has are kept, members only the generated
   * value has are removed and differing values are replaced, the last two
   * guarded by a hash of the generated value so that they apply only while
   * the writer still generates the same value. Arrays of objects with ids
   * are matched by id, other arrays by index.
   * @param original The value read from the file.
   * @param generated The value generated from the model for the same file.
   * @param structure The structural arrays to leave out.
   * @param root The normalized path of the value, such as &lt;shape&gt;.
   * @param counts The counts to add the patch's entries to, or
   *        <code>nullptr</code>.
   * @return The difference.
   */
  XdDifference make_patch(const QJsonValue& original, const XdValue& generated,
    const XdStructure& structure, std::string_view root,
    XdPatchCounts* counts);

  /**
   * Applies a patch to a generated value.
   * @param patch The encoded patch.
   * @param value The generated value to patch.
   * @param is_cache_valid Whether members that are caches are restored.
   */
  void apply_patch(std::string_view patch, XdValue& value, bool is_cache_valid);

  /**
   * Encodes a JSON value compactly.
   * @param value The value to encode.
   * @param out The string the encoding is appended to.
   */
  void encode_value(const QJsonValue& value, std::string& out);

  /**
   * Decodes a value encoded by encode_value.
   * @param data The encoding, advanced past the value.
   * @return The value, null if the encoding is malformed.
   */
  XdValue decode_value(std::string_view& data);

  /**
   * Appends an unsigned integer in LEB128 form.
   * @param value The integer.
   * @param out The string to append to.
   */
  void encode_varint(std::uint64_t value, std::string& out);

  /**
   * Reads an unsigned integer in LEB128 form.
   * @param data The encoding, advanced past the integer.
   * @return The integer, 0 if the encoding is malformed.
   */
  std::uint64_t decode_varint(std::string_view& data);

  /**
   * Appends a string preceded by its length.
   * @param text The string.
   * @param out The string to append to.
   */
  void encode_string(std::string_view text, std::string& out);

  /**
   * Reads a string preceded by its length.
   * @param data The encoding, advanced past the string.
   * @return The string, empty if the encoding is malformed.
   */
  std::string_view decode_string(std::string_view& data);

  /**
   * Appends a 64-bit integer in little-endian order.
   * @param value The integer.
   * @param out The string to append to.
   */
  void encode_fixed(std::uint64_t value, std::string& out);

  /**
   * Reads a 64-bit integer in little-endian order.
   * @param data The encoding, advanced past the integer.
   * @return The integer, 0 if the encoding is malformed.
   */
  std::uint64_t decode_fixed(std::string_view& data);

  /**
   * Tests whether a member is a cache of other members of its node.
   * @param key The member's name.
   */
  bool is_cache_key(std::string_view key);
}

#endif

#ifndef RELLU_MODEL_CHANGE_SET_HPP
#define RELLU_MODEL_CHANGE_SET_HPP
#include <cstdint>
#include <vector>
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** Flags the parts of a node that a change modified. */
  enum class ChangeMask : std::uint32_t {

    /** Nothing. */
    NONE = 0,

    /** The header's name. */
    NAME = 1 << 0,

    /** The header's visibility. */
    VISIBILITY = 1 << 1,

    /** The header's lock. */
    LOCK = 1 << 2,

    /**
     * The header's transform or 3D transform, or a reference's transform
     * override.
     */
    TRANSFORM = 1 << 3,

    /** The header's opacity. */
    OPACITY = 1 << 4,

    /** The header's blend mode. */
    BLEND_MODE = 1 << 5,

    /** The header's isolation. */
    ISOLATION = 1 << 6,

    /** The header's constraints, aspect lock or fixed position. */
    CONSTRAINTS = 1 << 7,

    /** The header's export mark. */
    EXPORT = 1 << 8,

    /** A shape's geometry, a text's frame or an artboard's frame. */
    GEOMETRY = 1 << 9,

    /** A shape's or text's style, or an artboard's background. */
    STYLE = 1 << 10,

    /** A text's content, character and paragraph styles or layout. */
    TEXT = 1 << 11,

    /** The children or their order. */
    CHILDREN = 1 << 12,

    /** A group's roles. */
    ROLES = 1 << 13,

    /** An artboard's viewport, grid or guides. */
    ARTBOARD = 1 << 14,

    /** A reference's source or whether it has its own children. */
    REFERENCE = 1 << 15
  };

  constexpr ChangeMask operator |(ChangeMask left, ChangeMask right) {
    return static_cast<ChangeMask>(
      static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
  }

  constexpr ChangeMask operator &(ChangeMask left, ChangeMask right) {
    return static_cast<ChangeMask>(
      static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
  }

  constexpr ChangeMask& operator |=(ChangeMask& left, ChangeMask right) {
    left = left | right;
    return left;
  }

  /**
   * Tests whether a mask has any of a set of flags.
   * @param mask The mask to test.
   * @param flags The flags to look for.
   * @return <code>true</code> iff the <i>mask</i> and <i>flags</i> share a
   *         flag.
   */
  constexpr bool is_set(ChangeMask mask, ChangeMask flags) {
    return (mask & flags) != ChangeMask::NONE;
  }

  /** The modification of a node that remains in the document. */
  struct NodeChange {

    /** The node modified. */
    NodeId m_id;

    /** The parts modified. */
    ChangeMask m_mask;

    bool operator ==(const NodeChange&) const = default;
  };

  /** Describes everything a committed transaction, undo or redo changed. */
  struct ChangeSet {

    /** The nodes added, each listed before its descendants. */
    std::vector<NodeId> m_inserted;

    /** The nodes removed, each listed before its descendants. */
    std::vector<NodeId> m_removed;

    /** The nodes reparented or reordered. */
    std::vector<NodeId> m_moved;

    /** The nodes modified, other than by insertion or removal. */
    std::vector<NodeChange> m_modified;

    /** The components added, removed or modified. */
    std::vector<ComponentId> m_components;

    /** The images added or removed. */
    std::vector<ImageId> m_images;

    /** The color swatches added, removed or modified. */
    std::vector<SwatchId> m_swatches;

    /** The character styles added, removed or modified. */
    std::vector<CharacterStyleId> m_character_styles;

    /** The interactions added, removed or modified. */
    std::vector<InteractionId> m_interactions;

    /** Whether the home artboard or the flows changed. */
    bool m_is_flow_changed = false;

    /** Whether the groups of assets changed. */
    bool m_is_asset_group_changed = false;

    /** Returns <code>true</code> iff nothing changed. */
    bool is_empty() const;

    bool operator ==(const ChangeSet&) const = default;
  };

  inline bool ChangeSet::is_empty() const {
    return m_inserted.empty() && m_removed.empty() && m_moved.empty() &&
      m_modified.empty() && m_components.empty() && m_images.empty() &&
      m_swatches.empty() && m_character_styles.empty() &&
      m_interactions.empty() && !m_is_flow_changed &&
      !m_is_asset_group_changed;
  }
}

#endif

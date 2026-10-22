#ifndef RELLU_COMMANDS_BOUNDS_HPP
#define RELLU_COMMANDS_BOUNDS_HPP
#include <functional>
#include <optional>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /** Lists the extents a bounds measurement covers. */
  enum class BoundsType : unsigned char {

    /** The outline of the geometry. */
    GEOMETRY,

    /** The outline together with the area its stroke paints. */
    VISUAL,

    /**
     * The area painted, including strokes, shadows and blurs, as Adobe XD
     * measures a node's draw bounds.
     */
    DRAW
  };

  /** Measures the content of a text payload in its node's local space. */
  using TextMeasurer = std::function<Rect (const Text& text)>;

  /**
   * Estimates the bounds of a text payload's content from its frame, font
   * sizes and line count, without laying it out.
   * @param text The text payload.
   * @return The estimated bounds in the node's local space.
   */
  Rect estimate_text_bounds(const Text& text);

  /**
   * Returns the transform rendered from a node's local space to its
   * parent's: a reference's resolved transform, an artboard's translation to
   * its frame, otherwise the header's transform. Throws std::out_of_range if
   * there is no such node.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  Matrix get_transform(const Document& document, const NodeId& id);

  /**
   * Returns the transform from a node's local space to document space.
   * @param document The document holding the node.
   * @param id The node's id, or nil for the canvas.
   */
  Matrix get_world_transform(const Document& document, const NodeId& id);

  /**
   * Returns the transform from the local space of a node's parent to
   * document space, the identity for a node without a parent.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  Matrix get_parent_transform(const Document& document, const NodeId& id);

  /**
   * Measures a node's content, including its visible descendants.
   * @param document The document holding the node.
   * @param id The node's id.
   * @param matrix The transform from the node's local space to the space
   *        measured in.
   * @param type The extents measured.
   * @param measurer The function measuring text.
   * @return The bounds in the measured space, or <code>std::nullopt</code>
   *         if the node draws nothing.
   */
  std::optional<Rect> get_bounds(const Document& document, const NodeId& id,
    const Matrix& matrix, BoundsType type = BoundsType::GEOMETRY,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Measures a node's content in its local space.
   * @param document The document holding the node.
   * @param id The node's id.
   * @param type The extents measured.
   * @param measurer The function measuring text.
   * @return The bounds, or <code>std::nullopt</code> if the node draws
   *         nothing.
   */
  std::optional<Rect> get_local_bounds(const Document& document,
    const NodeId& id, BoundsType type = BoundsType::GEOMETRY,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Measures a node's content in document space.
   * @param document The document holding the node.
   * @param id The node's id.
   * @param type The extents measured.
   * @param measurer The function measuring text.
   * @return The bounds, or <code>std::nullopt</code> if the node draws
   *         nothing.
   */
  std::optional<Rect> get_world_bounds(const Document& document,
    const NodeId& id, BoundsType type = BoundsType::GEOMETRY,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Measures the union of several nodes' content in document space.
   * @param document The document holding the nodes.
   * @param nodes The nodes' ids.
   * @param type The extents measured.
   * @param measurer The function measuring text.
   * @return The bounds, or <code>std::nullopt</code> if none of the nodes
   *         draws anything.
   */
  std::optional<Rect> get_world_bounds(const Document& document,
    const std::vector<NodeId>& nodes, BoundsType type = BoundsType::GEOMETRY,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Makes the transform mapping one rect onto another, keeping the scale of
   * an axis along which the source rect has no extent.
   * @param from The rect mapped from.
   * @param to The rect mapped to.
   */
  Matrix make_rect_mapping(const Rect& from, const Rect& to);
}

#endif

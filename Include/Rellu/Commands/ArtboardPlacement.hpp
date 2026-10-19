#ifndef RELLU_COMMANDS_ARTBOARD_PLACEMENT_HPP
#define RELLU_COMMANDS_ARTBOARD_PLACEMENT_HPP
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** The horizontal space Adobe XD leaves between artboards it places. */
  constexpr auto ARTBOARD_SPACING = 70.0;

  /** The vertical space Adobe XD leaves between artboards it places. */
  constexpr auto ARTBOARD_ROW_SPACING = 90.0;

  /**
   * Names an artboard as Adobe XD does: a base name followed by an en dash
   * and the lowest number no node on the canvas is named with.
   * @param document The document the artboard is added to.
   * @param name The name the artboard is given.
   * @param is_kept Whether a <i>name</i> no node on the canvas has is kept.
   */
  std::string make_artboard_name(
    const Document& document, std::string_view name, bool is_kept);

  /**
   * Finds where Adobe XD places new artboards.
   * @param document The document the artboards are added to.
   * @param selection The selected nodes.
   * @param view The region of the document in view.
   * @param bounds The region the new artboards cover.
   * @return The region the new artboards cover once placed.
   */
  Rect place_artboards(const Document& document,
    const std::vector<NodeId>& selection, const Rect& view, const Rect& bounds);

  /**
   * Finds where Adobe XD places an artboard created by a click of the
   * Artboard tool.
   * @param document The document the artboard is added to.
   * @param size The artboard's size.
   * @param point The point clicked in document space.
   * @param view The region of the document in view.
   * @return The artboard's frame.
   */
  Rect place_artboard_at(
    const Document& document, Size size, Point point, const Rect& view);

  /**
   * Returns the position on the canvas Adobe XD inserts a new artboard at,
   * above the other artboards.
   * @param document The document the artboard is added to.
   */
  std::size_t get_artboard_index(const Document& document);
}

#endif

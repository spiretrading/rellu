#ifndef RELLU_SCENE_COMPOSITING_HPP
#define RELLU_SCENE_COMPOSITING_HPP
#include <cstddef>
#include <optional>
#include <vector>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Group.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/Style.hpp"
#include "Rellu/Rendering/DrawList.hpp"

namespace Rellu {

  /**
   * Appends the commands drawing an outline's fill and stroke with their
   * effects, the way Adobe XD orders them: drop shadows beneath the fill and
   * stroke, a background blur beneath the fill, inner shadows atop the fill
   * and beneath the stroke, and an object blur over all of them.
   * @param list The draw list to append to.
   * @param fill The command filling the outline, if it is filled.
   * @param stroke The command stroking the outline, if it is stroked.
   * @param effects The effects in the order they apply.
   */
  void add_styled(DrawList& list, std::optional<FillCommand> fill,
    std::optional<FillCommand> stroke, const std::vector<Effect>& effects);

  /**
   * Converts a group's mask, narrowing a clip instead when the mask is a
   * single rectangle aligned with the axes of document coordinates.
   * @param mask The mask.
   * @param transform The transform from the group's coordinates to document
   *        coordinates.
   * @param tolerance The largest distance in document units between a curve
   *        and its approximation when uniting several mask shapes.
   * @param clip The clip of the group's children, narrowed when the mask is
   *        an axis aligned rectangle.
   * @return The region clipping the group's children, or none if the
   *         <i>clip</i> was narrowed instead.
   */
  std::optional<LayerMask> make_mask(const Mask& mask,
    const Matrix& transform, double tolerance, std::optional<Rect>& clip);

  /**
   * Composites the commands a node appended to a draw list as a whole,
   * applying the node's opacity, blend mode, isolation and mask through a
   * layer unless the result is the same without one.
   * @param list The draw list.
   * @param first The index of the node's first command.
   * @param header The node's header.
   * @param mask The region clipping the node's children, if any.
   * @param clip The rect in document coordinates clipping the node.
   */
  void add_composite(DrawList& list, std::size_t first,
    const NodeHeader& header, std::optional<LayerMask> mask,
    const std::optional<Rect>& clip);
}

#endif

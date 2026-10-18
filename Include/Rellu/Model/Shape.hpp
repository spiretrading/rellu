#ifndef RELLU_MODEL_SHAPE_HPP
#define RELLU_MODEL_SHAPE_HPP
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /** The payload of a shape node. */
  struct Shape {

    /** The outline. */
    ShapeGeometry m_geometry;

    /** The paint and effects. */
    Style m_style;

    bool operator ==(const Shape&) const = default;
  };
}

#endif

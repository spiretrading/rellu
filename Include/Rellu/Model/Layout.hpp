#ifndef RELLU_MODEL_LAYOUT_HPP
#define RELLU_MODEL_LAYOUT_HPP

namespace Rellu {

  /** Lists the axes along which content is arranged. */
  enum class Orientation : unsigned char {

    /** Along the x axis. */
    HORIZONTAL,

    /** Along the y axis. */
    VERTICAL
  };

  /** The space reserved on each side of a rectangle. */
  struct Margins {

    /** The space above. */
    double m_top = 0;

    /** The space to the right. */
    double m_right = 0;

    /** The space below. */
    double m_bottom = 0;

    /** The space to the left. */
    double m_left = 0;

    bool operator ==(const Margins&) const = default;
  };
}

#endif

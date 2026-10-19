#ifndef RELLU_TOOLS_GUIDE_SETTINGS_HPP
#define RELLU_TOOLS_GUIDE_SETTINGS_HPP
#include <cstddef>
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** How artboard guides are shown, as View > Guides sets it. */
  struct GuideSettings {

    /** Whether guides are drawn, created and snapped to. */
    bool m_is_visible = true;

    /** Whether existing guides are protected from being moved. */
    bool m_is_locked = false;

    bool operator ==(const GuideSettings&) const = default;
  };

  /** Identifies a guide of an artboard. */
  struct GuideReference {

    /** The artboard. */
    NodeId m_artboard;

    /** The index of the guide among the artboard's guides. */
    std::size_t m_index = 0;

    bool operator ==(const GuideReference&) const = default;
  };
}

#endif

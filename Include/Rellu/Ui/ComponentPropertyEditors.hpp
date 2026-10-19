#ifndef RELLU_UI_COMPONENT_PROPERTY_EDITORS_HPP
#define RELLU_UI_COMPONENT_PROPERTY_EDITORS_HPP
#include "Rellu/Ui/PropertyEditors.hpp"

namespace Rellu {

  /**
   * Adds the editors of the component section: the name of a main
   * component's component, the state of instances and the name of the state
   * a main component shows.
   * @param editors The editors to add to.
   */
  void add_component_property_editors(PropertyEditors& editors);
}

#endif

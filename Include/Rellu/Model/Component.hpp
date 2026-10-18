#ifndef RELLU_MODEL_COMPONENT_HPP
#define RELLU_MODEL_COMPONENT_HPP
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** Records how a component instance or state differs from its master. */
  struct Overrides {

    /** The nodes present here but not in the master. */
    std::vector<NodeId> m_added;

    /** The master nodes absent here. */
    std::vector<NodeId> m_removed;

    /** The reordered nodes, each mapped to the node now preceding it. */
    std::map<NodeId, NodeId> m_reordered;

    /** The names of the overridden properties, by node id. */
    std::map<NodeId, std::vector<std::string>> m_properties;

    bool operator ==(const Overrides&) const = default;
  };

  /** A named variation of a component. */
  struct ComponentState {

    /** The state's identifier. */
    StateId m_id;

    /** The state's name. */
    std::string m_name;

    /** The root of the group subtree drawn in this state. */
    NodeId m_tree;

    /** Maps each node of the tree to the master node it mirrors. */
    std::map<NodeId, NodeId> m_sync_map;

    /** How the state differs from the master. */
    Overrides m_overrides;

    bool operator ==(const ComponentState&) const = default;
  };

  /** Describes where a component linked from another document comes from. */
  struct ComponentLink {

    /** The reference to the linked element in its source. */
    std::string m_reference;

    /** The name of the source document or library. */
    std::string m_source_name;

    /** The modification time of the linked element, in milliseconds. */
    double m_last_modified = 0;

    bool operator ==(const ComponentLink& link) const;
  };

  /** A reusable master design with states. */
  struct Component {

    /** The component's identifier. */
    ComponentId m_id;

    /** The component's name. */
    std::string m_name;

    /** The root of the master group subtree. */
    NodeId m_master;

    /** The states other than the master. */
    std::vector<ComponentState> m_states;

    /** The state the master itself represents. */
    StateId m_default_state;

    /** The source of a linked component, if it is linked. */
    std::optional<ComponentLink> m_link;

    bool operator ==(const Component&) const = default;
  };

  inline bool ComponentLink::operator ==(const ComponentLink& link) const {
    return m_reference == link.m_reference &&
      m_source_name == link.m_source_name &&
      equals(m_last_modified, link.m_last_modified);
  }
}

#endif

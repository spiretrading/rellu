#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  using DefaultStates = std::unordered_map<ComponentId, StateId>;

  struct ComponentTable {
    std::shared_ptr<const DefaultStates> m_default_states;
    std::unordered_map<NodeId, Component> m_masters;
    std::unordered_map<NodeId, ComponentState> m_states;

    ComponentTable fork() const {
      auto table = ComponentTable();
      table.m_default_states = m_default_states;
      return table;
    }

    void merge(ComponentTable fork) {
      for(auto& master : fork.m_masters) {
        m_masters.insert_or_assign(master.first, std::move(master.second));
      }
    }
  };

  struct MasterComponent {
    Component m_component;
  };

  struct OverrideScope {
    Overrides m_overrides;
  };

  void ignore_component_keys(XdObject& ux) {
    ux.ignore({"componentType"_L1, "canonicalPaddingTop"_L1,
      "canonicalPaddingRight"_L1, "canonicalPaddingBottom"_L1,
      "canonicalPaddingLeft"_L1, "symbolPaddingTop"_L1,
      "symbolPaddingRight"_L1, "symbolPaddingBottom"_L1,
      "symbolPaddingLeft"_L1, "isMaster"_L1, "hash"_L1});
  }

  void ignore_link_keys(XdObject& ux) {
    ux.ignore({"linkedElementRef"_L1, "resolvedRefName"_L1,
      "linkedElementLastModified"_L1, "linkedElementBookmarkRef"_L1});
  }

  std::vector<NodeId> read_ids(XdObject& json, QLatin1StringView key) {
    auto ids = std::vector<NodeId>();
    if(auto array = json.get_array(key)) {
      ids.reserve(array->get_size());
      for(auto value : array->get_json()) {
        ids.push_back(to_node_id(value.toString()));
      }
    }
    return ids;
  }

  std::vector<std::string> read_names(XdObject& json) {
    auto names = std::vector<std::string>();
    if(auto array = json.get_array("propertyNames"_L1)) {
      names.reserve(array->get_size());
      for(auto value : array->get_json()) {
        names.push_back(value.toString().toStdString());
      }
    }
    return names;
  }

  Overrides read_overrides(XdObject& ux, const NodeId& id) {
    auto overrides = Overrides();
    if(ux.get_json().value("symbolOverrides"_L1).isNull()) {
      ux.consume("symbolOverrides"_L1);
      return overrides;
    }
    auto json = ux.get_object("symbolOverrides"_L1);
    if(!json) {
      return overrides;
    }
    overrides.m_added = read_ids(*json, "addedGuids"_L1);
    overrides.m_removed = read_ids(*json, "removedGuids"_L1);
    if(auto reordered = json->get_object("reorderedGuids"_L1)) {
      auto& map = reordered->get_json();
      for(auto entry = map.constBegin(); entry != map.constEnd(); ++entry) {
        overrides.m_reordered.emplace(
          to_node_id(entry.key()), to_node_id(entry.value().toString()));
      }
      reordered->consume_all();
    }
    auto names = read_names(*json);
    if(!names.empty()) {
      overrides.m_properties.emplace(id, std::move(names));
    }
    return overrides;
  }

  std::map<NodeId, NodeId> read_sync_map(XdObject& ux) {
    auto sync_map = std::map<NodeId, NodeId>();
    if(auto json = ux.get_object("syncMap"_L1)) {
      auto& map = json->get_json();
      for(auto entry = map.constBegin(); entry != map.constEnd(); ++entry) {
        sync_map.emplace(
          to_node_id(entry.value().toString()), to_node_id(entry.key()));
      }
      json->consume_all();
    }
    return sync_map;
  }

  std::vector<ComponentState> import_states(XdObject& ux, XdImport& import) {
    auto states = std::vector<ComponentState>();
    auto json = ux.get_array("states"_L1);
    if(!json) {
      return states;
    }
    auto& table = import.get_states().get<ComponentTable>();
    states.reserve(json->get_size());
    for(auto i = std::size_t(0); i != json->get_size(); ++i) {
      auto state_json = json->get_object(i);
      if(!state_json) {
        continue;
      }
      auto id = import.import_node(*state_json, nullptr, XdNodeRole::STATE);
      if(id.is_nil()) {
        continue;
      }
      if(auto entry = table.m_states.find(id); entry != table.m_states.end()) {
        states.push_back(std::move(entry->second));
        table.m_states.erase(entry);
      }
    }
    return states;
  }

  StateId read_state_id(XdObject& ux) {
    if(auto state = ux.get_string("stateId"_L1)) {
      return to_node_id(*state);
    }
    return StateId();
  }

  void import_master(XdNode& node, XdObject& ux) {
    auto& import = *node.m_import;
    auto component = Component();
    if(auto id = ux.get_string("symbolId"_L1)) {
      component.m_id = to_node_id(*id);
    }
    component.m_name = node.m_header.m_name;
    component.m_master = node.m_header.m_id;
    component.m_default_state = read_state_id(ux);
    if(auto reference = ux.get_string("linkedElementRef"_L1)) {
      auto link = ComponentLink();
      link.m_reference = to_std_string(*reference);
      if(auto name = ux.get_string("resolvedRefName"_L1)) {
        link.m_source_name = to_std_string(*name);
      }
      link.m_last_modified =
        ux.get_number("linkedElementLastModified"_L1, 0);
      component.m_link = std::move(link);
    }
    component.m_size = Size(
      ux.get_number("width"_L1, 0), ux.get_number("height"_L1, 0));
    auto groups = ux.get_json().value("assetGroupData"_L1).toObject();
    for(auto entry = groups.begin(); entry != groups.end(); ++entry) {
      if(entry->isString()) {
        component.m_placements.push_back(AssetPlacement(
          to_std_string(entry.key()), to_std_string(entry->toString())));
      }
    }
    ux.ignore({"linkedElementBookmarkRef"_L1});
    ignore_component_keys(ux);
    component.m_states = import_states(ux, import);
    node.m_states.get<MasterComponent>().m_component = std::move(component);
  }

  void import_state(XdNode& node, XdObject& ux) {
    auto state = ComponentState();
    state.m_id = read_state_id(ux);
    if(state.m_id.is_nil()) {
      state.m_id = node.m_header.m_id;
    }
    state.m_name = node.m_header.m_name;
    state.m_tree = node.m_header.m_id;
    state.m_sync_map = read_sync_map(ux);
    state.m_overrides = read_overrides(ux, node.m_header.m_id);
    state.m_size = Size(
      ux.get_number("width"_L1, 0), ux.get_number("height"_L1, 0));
    ux.ignore({"symbolId"_L1});
    ignore_component_keys(ux);
    ignore_link_keys(ux);
    node.m_states.get<OverrideScope>();
    node.m_import->get_states().get<ComponentTable>().m_states.insert_or_assign(
      node.m_header.m_id, std::move(state));
  }

  void import_instance(XdNode& node, Group& group, XdObject& ux) {
    auto& import = *node.m_import;
    auto& table = import.get_states().get<ComponentTable>();
    auto instance = ComponentInstance();
    instance.m_component = to_node_id(*ux.get_string("symbolId"_L1));
    instance.m_state = read_state_id(ux);
    auto is_default = instance.m_state == instance.m_component;
    if(!is_default && table.m_default_states) {
      auto& defaults = *table.m_default_states;
      auto default_state = defaults.find(instance.m_component);
      is_default = default_state != defaults.end() &&
        default_state->second == instance.m_state;
    }
    if(is_default) {
      instance.m_state = StateId();
    }
    instance.m_size = Size(
      ux.get_number("width"_L1, 0), ux.get_number("height"_L1, 0));
    instance.m_sync_map = read_sync_map(ux);
    instance.m_overrides = read_overrides(ux, node.m_header.m_id);
    instance.m_is_main = ux.get_bool("isMaster"_L1, false);
    instance.m_is_name_inherited =
      ux.get_bool("isCustomNameInherited"_L1, false);
    ignore_component_keys(ux);
    ignore_link_keys(ux);
    instance.m_states = import_states(ux, import);
    group.m_component_instance = std::move(instance);
    node.m_states.get<OverrideScope>();
  }

  void merge(Overrides source, Overrides& target) {
    target.m_added.insert(target.m_added.end(), source.m_added.begin(),
      source.m_added.end());
    target.m_removed.insert(target.m_removed.end(), source.m_removed.begin(),
      source.m_removed.end());
    target.m_reordered.merge(source.m_reordered);
    for(auto& entry : source.m_properties) {
      target.m_properties.insert_or_assign(
        entry.first, std::move(entry.second));
    }
    for(auto& entry : source.m_owners) {
      target.m_owners.insert_or_assign(entry.first, entry.second);
    }
  }

  void record_owner(Overrides& overrides, const NodeId& owner) {
    for(auto& id : overrides.m_added) {
      overrides.m_owners.insert_or_assign(id, owner);
    }
    for(auto& id : overrides.m_removed) {
      overrides.m_owners.insert_or_assign(id, owner);
    }
    for(auto& entry : overrides.m_reordered) {
      overrides.m_owners.insert_or_assign(entry.first, owner);
    }
  }

  void import_descendant(XdNode& node, XdObject& ux) {
    if(!ux.contains("symbolOverrides"_L1)) {
      return;
    }
    auto scope = static_cast<OverrideScope*>(nullptr);
    for(auto parent = node.m_parent; parent && !scope;
        parent = parent->m_parent) {
      scope = parent->m_states.find<OverrideScope>();
    }
    if(!scope) {
      ux.ignore({"symbolOverrides"_L1});
      return;
    }
    auto overrides = read_overrides(ux, node.m_header.m_id);
    record_owner(overrides, node.m_header.m_id);
    merge(std::move(overrides), scope->m_overrides);
  }
}

void Rellu::add_component_importer(XdImporter& importer) {
  importer.add_document_importer(XdPhase::COMPONENTS, [] (XdImport& import) {
    auto resources = import.get_resources().get_object("resources"_L1);
    if(!resources) {
      return;
    }
    auto ux = get_ux(*resources);
    if(!ux) {
      return;
    }
    auto symbols = ux->get_array("symbols"_L1);
    if(!symbols) {
      return;
    }
    auto& table = import.get_states().get<ComponentTable>();
    auto defaults = DefaultStates();
    auto nodes = std::vector<QJsonObject>();
    for(auto symbol : symbols->get_json()) {
      auto symbol_ux =
        symbol.toObject().value("meta"_L1).toObject().value("ux"_L1).toObject();
      auto id = symbol_ux.value("symbolId"_L1).toString();
      auto state = symbol_ux.value("stateId"_L1).toString();
      if(!id.isEmpty() && !state.isEmpty()) {
        defaults.emplace(to_node_id(id), to_node_id(state));
      }
      if(symbol.isObject()) {
        nodes.push_back(symbol.toObject());
      }
    }
    table.m_default_states =
      std::make_shared<const DefaultStates>(std::move(defaults));
    for(auto& id : import.import_nodes(nodes, XdNodeRole::MASTER,
        "agc.resources.meta.ux.symbols[]")) {
      if(id.is_nil()) {
        continue;
      }
      auto entry = table.m_masters.find(id);
      if(entry == table.m_masters.end()) {
        import.warn("Component master without a component");
        continue;
      }
      try {
        import.get_transaction().add_component(std::move(entry->second));
      } catch(const std::invalid_argument&) {
        import.warn("Component not added");
      }
      table.m_masters.erase(entry);
    }
  });
  importer.add_property_importer([] (XdNode& node) {
    auto ux = get_ux(*node.m_json);
    if(!ux) {
      return;
    }
    if(node.m_role == XdNodeRole::MASTER) {
      import_master(node, *ux);
    } else if(node.m_role == XdNodeRole::STATE) {
      import_state(node, *ux);
    } else if(auto group = node.m_payload ?
        std::get_if<Group>(&*node.m_payload) : nullptr; group &&
        ux->get_json().value("symbolId"_L1).isString()) {
      import_instance(node, *group, *ux);
    } else {
      import_descendant(node, *ux);
    }
  });
  importer.add_finisher([] (XdNode& node) {
    auto& import = *node.m_import;
    if(auto master = node.m_states.find<MasterComponent>()) {
      auto& component = master->m_component;
      import.get_states().get<ComponentTable>().m_masters.insert_or_assign(
        component.m_master, std::move(component));
    }
    auto scope = node.m_states.find<OverrideScope>();
    if(!scope || scope->m_overrides == Overrides()) {
      return;
    }
    if(node.m_role == XdNodeRole::STATE) {
      auto& states = import.get_states().get<ComponentTable>().m_states;
      if(auto entry = states.find(node.m_header.m_id); entry != states.end()) {
        merge(std::move(scope->m_overrides), entry->second.m_overrides);
      }
      return;
    }
    auto current = import.find_node(node.m_header.m_id);
    if(!current || !current->get_group() ||
        !current->get_group()->m_component_instance) {
      return;
    }
    auto group = *current->get_group();
    merge(std::move(scope->m_overrides),
      group.m_component_instance->m_overrides);
    import.replace_payload(node.m_header.m_id, std::move(group));
  });
}

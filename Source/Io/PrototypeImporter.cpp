#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  struct InheritedInteraction {
    NodeId m_owner;
    InteractionId m_source;
    bool m_is_enabled;
  };

  struct PrototypeTable {
    std::vector<Interaction> m_own;
    std::unordered_map<InteractionId, bool> m_enabled;
    std::vector<InheritedInteraction> m_inherited;
    std::vector<Flow> m_flows;

    PrototypeTable fork() const {
      return PrototypeTable();
    }

    void merge(PrototypeTable fork) {
      m_own.insert(m_own.end(), std::make_move_iterator(fork.m_own.begin()),
        std::make_move_iterator(fork.m_own.end()));
      for(auto& entry : fork.m_enabled) {
        m_enabled.insert_or_assign(entry.first, entry.second);
      }
      m_inherited.insert(m_inherited.end(), fork.m_inherited.begin(),
        fork.m_inherited.end());
      m_flows.insert(m_flows.end(),
        std::make_move_iterator(fork.m_flows.begin()),
        std::make_move_iterator(fork.m_flows.end()));
    }
  };

  std::optional<Transition> to_transition(const QString& name) {
    if(name == "none"_L1) {
      return Transition::NONE;
    } else if(name == "dissolve"_L1) {
      return Transition::DISSOLVE;
    } else if(name == "slide-left"_L1) {
      return Transition::SLIDE_LEFT;
    } else if(name == "slide-right"_L1) {
      return Transition::SLIDE_RIGHT;
    } else if(name == "slide-up"_L1) {
      return Transition::SLIDE_UP;
    } else if(name == "slide-down"_L1) {
      return Transition::SLIDE_DOWN;
    } else if(name == "push-left"_L1) {
      return Transition::PUSH_LEFT;
    } else if(name == "push-right"_L1) {
      return Transition::PUSH_RIGHT;
    } else if(name == "push-up"_L1) {
      return Transition::PUSH_UP;
    } else if(name == "push-down"_L1) {
      return Transition::PUSH_DOWN;
    } else if(name == "auto-animate"_L1) {
      return Transition::AUTO_ANIMATE;
    }
    return std::nullopt;
  }

  std::optional<Easing> to_easing(const QString& name) {
    if(name == "linear"_L1) {
      return Easing::LINEAR;
    } else if(name == "ease-in"_L1) {
      return Easing::EASE_IN;
    } else if(name == "ease-out"_L1) {
      return Easing::EASE_OUT;
    } else if(name == "ease-in-out"_L1) {
      return Easing::EASE_IN_OUT;
    } else if(name == "wind-up"_L1) {
      return Easing::WIND_UP;
    } else if(name == "bounce"_L1) {
      return Easing::BOUNCE;
    } else if(name == "snap"_L1) {
      return Easing::SNAP;
    }
    return std::nullopt;
  }

  Animation read_animation(XdObject& properties) {
    auto animation = Animation();
    auto& json = properties.get_json();
    if(auto transition =
        to_transition(json.value("transition"_L1).toString())) {
      animation.m_transition = *transition;
      properties.consume("transition"_L1);
    }
    if(auto easing = to_easing(json.value("easing"_L1).toString())) {
      animation.m_easing = *easing;
      properties.consume("easing"_L1);
    }
    animation.m_duration = properties.get_number("duration"_L1, 0);
    return animation;
  }

  NodeId read_destination(XdObject& properties) {
    if(auto destination = properties.get_string("destination"_L1)) {
      return to_node_id(*destination);
    }
    return NodeId();
  }

  std::optional<Trigger> read_trigger(XdObject& interaction,
      XdObject& properties) {
    auto event = interaction.get_json().value("triggerEvent"_L1).toString();
    auto trigger = [&] () -> std::optional<Trigger> {
      if(event == "tap"_L1) {
        return TapTrigger();
      } else if(event == "hover"_L1) {
        return HoverTrigger();
      } else if(event == "drag"_L1) {
        return DragTrigger();
      } else if(event == "time"_L1) {
        return TimeTrigger(properties.get_number("delay"_L1, 0));
      } else if(event == "keys-gamepad"_L1) {
        auto key = KeyTrigger();
        if(auto keys = properties.get_string("keysGamepad"_L1)) {
          key.m_keys = to_std_string(*keys);
        }
        auto device =
          properties.get_json().value("keysGamepadDevice"_L1).toString();
        if(device == "keyboard"_L1) {
          properties.consume("keysGamepadDevice"_L1);
        } else if(device == "gamepad"_L1) {
          key.m_device = KeyDevice::GAMEPAD;
          properties.consume("keysGamepadDevice"_L1);
        }
        properties.ignore({"keysGamepadPrintedBaseKey"_L1});
        return key;
      }
      return std::nullopt;
    }();
    if(trigger) {
      interaction.consume("triggerEvent"_L1);
    }
    return trigger;
  }

  std::optional<Action> read_action(XdObject& interaction,
      XdObject& properties) {
    auto name = interaction.get_json().value("action"_L1).toString();
    auto action = [&] () -> std::optional<Action> {
      if(name == "artboard-transition"_L1) {
        auto transition = ArtboardTransition();
        transition.m_destination = read_destination(properties);
        transition.m_animation = read_animation(properties);
        transition.m_preserve_scroll =
          properties.get_bool("preserveScrollPosition"_L1, false);
        return transition;
      } else if(name == "auto-animate"_L1) {
        auto transition = ArtboardTransition();
        transition.m_destination = read_destination(properties);
        transition.m_animation = read_animation(properties);
        transition.m_animation.m_transition = Transition::AUTO_ANIMATE;
        transition.m_preserve_scroll =
          properties.get_bool("preserveScrollPosition"_L1, false);
        return transition;
      } else if(name == "overlay-transition"_L1) {
        auto overlay = Overlay();
        overlay.m_destination = read_destination(properties);
        overlay.m_animation = read_animation(properties);
        if(auto position = properties.get_object("overlayPosition"_L1)) {
          overlay.m_position = Point(position->get_number("x"_L1, 0),
            position->get_number("y"_L1, 0));
        }
        properties.ignore({"preserveScrollPosition"_L1});
        return overlay;
      } else if(name == "state-transition"_L1) {
        auto transition = StateTransition();
        transition.m_state = read_destination(properties);
        transition.m_animation = read_animation(properties);
        properties.ignore({"preserveScrollPosition"_L1});
        return transition;
      } else if(name == "previous-artboard"_L1) {
        read_animation(properties);
        properties.ignore({"preserveScrollPosition"_L1});
        return PreviousArtboard();
      } else if(name == "video-playback"_L1) {
        auto playback = VideoPlayback();
        playback.m_target = read_destination(properties);
        auto command = properties.get_json().value("subtype"_L1).toString();
        if(command == "play"_L1) {
          properties.consume("subtype"_L1);
        } else if(command == "pause"_L1) {
          playback.m_command = VideoCommand::PAUSE;
          properties.consume("subtype"_L1);
        } else if(command == "stop"_L1) {
          playback.m_command = VideoCommand::STOP;
          properties.consume("subtype"_L1);
        }
        return playback;
      }
      return std::nullopt;
    }();
    if(action) {
      interaction.consume("action"_L1);
    }
    return action;
  }

  void import_interactions(XdNode& node, XdObject& ux) {
    auto list = ux.get_array("interactions"_L1);
    if(!list) {
      return;
    }
    auto& table = node.m_import->get_states().get<PrototypeTable>();
    for(auto i = std::size_t(0); i != list->get_size(); ++i) {
      auto entry = list->get_object(i);
      if(!entry) {
        continue;
      }
      auto guid = entry->get_string("guid"_L1);
      if(!guid) {
        continue;
      }
      auto id = to_node_id(*guid);
      auto is_enabled = entry->get_bool("enabled"_L1, true);
      entry->ignore({"valid"_L1});
      if(entry->get_bool("inherited"_L1, false)) {
        table.m_inherited.push_back(
          InheritedInteraction(node.m_header.m_id, id, is_enabled));
        continue;
      }
      table.m_enabled[id] = is_enabled;
      auto data = entry->get_object("data"_L1);
      if(!data) {
        continue;
      }
      data->ignore({"version"_L1});
      if(auto json = data->get_object("interaction"_L1)) {
        if(auto interaction = read_interaction(*json)) {
          interaction->m_id = id;
          interaction->m_owner = node.m_header.m_id;
          interaction->m_is_enabled = is_enabled;
          table.m_own.push_back(std::move(*interaction));
        }
      }
    }
  }

  void import_flow(XdNode& node, XdObject& ux) {
    if(!ux.get_bool("isFlowRoot"_L1, false)) {
      return;
    }
    auto flow = Flow();
    flow.m_artboard = node.m_header.m_id;
    if(auto name = ux.get_string("flowName"_L1)) {
      flow.m_name = to_std_string(*name);
    }
    node.m_import->get_states().get<PrototypeTable>().m_flows.push_back(
      std::move(flow));
  }

  void add(XdImport& import, Interaction interaction,
      std::unordered_map<InteractionId, Interaction>& added) {
    if(!import.get_document().find(interaction.m_owner)) {
      import.warn("Interaction owner not found");
      return;
    }
    if(!interaction.m_id.is_nil() && added.contains(interaction.m_id)) {
      return;
    }
    if(auto playback = std::get_if<VideoPlayback>(&interaction.m_action)) {
      if(playback->m_target.is_nil()) {
        playback->m_target = interaction.m_owner;
      }
    }
    try {
      auto id = import.get_transaction().add_interaction(interaction);
      interaction.m_id = id;
      added.emplace(id, std::move(interaction));
    } catch(const std::invalid_argument&) {
      import.warn("Interaction not added");
    }
  }

  void import_table(XdImport& import, PrototypeTable& table,
      std::unordered_map<InteractionId, Interaction>& added) {
    auto& root = import.get_interactions();
    root.ignore({"version"_L1});
    auto owners = root.get_object("interactions"_L1);
    if(!owners) {
      return;
    }
    auto& json = owners->get_json();
    for(auto owner = json.constBegin(); owner != json.constEnd(); ++owner) {
      auto list = owners->get_array(owner.key());
      if(!list) {
        continue;
      }
      auto owner_id = to_node_id(owner.key());
      for(auto i = std::size_t(0); i != list->get_size(); ++i) {
        auto entry = list->get_object(i);
        if(!entry) {
          continue;
        }
        auto interaction = read_interaction(*entry);
        if(!interaction) {
          import.warn("Unsupported interaction");
          continue;
        }
        if(auto id = entry->get_string("id"_L1)) {
          interaction->m_id = to_node_id(*id);
        }
        interaction->m_owner = owner_id;
        if(auto enabled = table.m_enabled.find(interaction->m_id);
            enabled != table.m_enabled.end()) {
          interaction->m_is_enabled = enabled->second;
        }
        add(import, std::move(*interaction), added);
      }
    }
  }
}

std::optional<Interaction> Rellu::read_interaction(XdObject& interaction) {
  auto properties = interaction.get_object("properties"_L1);
  if(!properties) {
    return std::nullopt;
  }
  auto trigger = read_trigger(interaction, *properties);
  if(!trigger) {
    return std::nullopt;
  }
  auto action = read_action(interaction, *properties);
  if(!action) {
    return std::nullopt;
  }
  properties->ignore({"voiceLocale"_L1, "destinationType"_L1});
  auto result = Interaction();
  result.m_trigger = std::move(*trigger);
  result.m_action = std::move(*action);
  return result;
}

void Rellu::add_prototype_importer(XdImporter& importer) {
  importer.add_finisher([] (XdNode& node) {
    auto ux = get_ux(*node.m_json);
    if(!ux) {
      return;
    }
    import_interactions(node, *ux);
    import_flow(node, *ux);
  });
  importer.add_document_importer(XdPhase::LINKS, [] (XdImport& import) {
    auto& table = import.get_states().get<PrototypeTable>();
    auto added = std::unordered_map<InteractionId, Interaction>();
    import_table(import, table, added);
    for(auto& interaction : table.m_own) {
      add(import, std::move(interaction), added);
    }
    for(auto& inherited : table.m_inherited) {
      auto source = added.find(inherited.m_source);
      if(source == added.end()) {
        import.warn("Inherited interaction not found");
        continue;
      }
      auto interaction = source->second;
      interaction.m_id = derive_node_id(
        QString::fromStdString(to_string(inherited.m_source)),
        QString::fromStdString(to_string(inherited.m_owner)));
      interaction.m_owner = inherited.m_owner;
      interaction.m_is_enabled = inherited.m_is_enabled;
      add(import, std::move(interaction), added);
    }
    auto flows = std::vector<Flow>();
    for(auto& flow : table.m_flows) {
      if(auto node = import.get_document().find(flow.m_artboard);
          node && node->get_artboard()) {
        flows.push_back(std::move(flow));
      }
    }
    if(!flows.empty()) {
      import.get_transaction().set_flows(std::move(flows));
    }
    if(auto home = import.get_interactions().get_string("homeArtboard"_L1)) {
      auto id = to_node_id(*home);
      if(auto node = import.get_document().find(id);
          node && node->get_artboard()) {
        import.get_transaction().set_home_artboard(id);
      } else {
        import.warn("Home artboard not found");
      }
    }
  });
}

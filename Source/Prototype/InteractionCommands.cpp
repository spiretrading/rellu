#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"

using namespace Rellu;

namespace {
  constexpr auto KEYS_LABEL = "%1 \xE2\x80\x94 %2";
  constexpr auto KEYS_COUNT_LABEL = "%1 \xE2\x80\x94 %2 (%3)";
  constexpr auto COUNT_LABEL = "%1 (%2)";
  constexpr auto GAMEPAD_PREFIX = std::string_view("\xF0\x9F\x8E\xAE ");

  std::string format_text(
      std::string_view pattern, const std::vector<std::string>& arguments) {
    auto text = std::string();
    for(auto i = std::size_t(0); i < pattern.size(); ++i) {
      if(pattern[i] == '%' && i + 1 < pattern.size() &&
          pattern[i + 1] >= '1' && pattern[i + 1] <= '9') {
        auto index = static_cast<std::size_t>(pattern[i + 1] - '1');
        if(index < arguments.size()) {
          text += arguments[index];
        }
        ++i;
      } else {
        text += pattern[i];
      }
    }
    return text;
  }

  std::string strip_gamepad(const std::string& keys) {
    auto text = keys;
    if(auto position = text.find(GAMEPAD_PREFIX);
        position != std::string::npos) {
      text.erase(position, GAMEPAD_PREFIX.size());
    }
    return text;
  }

  std::string get_trigger_name(const Trigger& trigger) {
    if(std::holds_alternative<DragTrigger>(trigger)) {
      return std::string(localize("prototype", "Drag"));
    } else if(std::holds_alternative<HoverTrigger>(trigger)) {
      return std::string(localize("prototype", "Hover"));
    } else if(std::holds_alternative<TimeTrigger>(trigger)) {
      return std::string(localize("prototype", "Time"));
    } else if(std::holds_alternative<KeyTrigger>(trigger)) {
      return std::string(localize("prototype", "Keys & Gamepad"));
    }
    return std::string(localize("prototype", "Tap"));
  }

  const ComponentInstance* get_instance(const Node& node) {
    if(auto group = node.get_group(); group && group->m_component_instance) {
      return &*group->m_component_instance;
    }
    return nullptr;
  }

  const Interaction* find_interaction(
      const Document& document, const InteractionId& id) {
    for(auto& interaction : document.get_prototype().m_interactions) {
      if(interaction.m_id == id) {
        return &interaction;
      }
    }
    return nullptr;
  }

  void require_artboard(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node || !node->get_artboard()) {
      throw std::invalid_argument("Not an artboard.");
    }
  }

  Animation get_animation(const Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      return transition->m_animation;
    } else if(auto state = std::get_if<StateTransition>(&action)) {
      return state->m_animation;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      return overlay->m_animation;
    } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
      return scroll->m_animation;
    }
    return make_empty_animation();
  }

  void require_destination(
      const Document& document, const Interaction& interaction) {
    auto destination = NodeId();
    if(auto transition =
        std::get_if<ArtboardTransition>(&interaction.m_action)) {
      destination = transition->m_destination;
    } else if(auto overlay = std::get_if<Overlay>(&interaction.m_action)) {
      destination = overlay->m_destination;
    }
    if(!destination.is_nil()) {
      require_artboard(document, destination);
    }
    if(auto scroll =
        std::get_if<ScrollTransition>(&interaction.m_action)) {
      if(!scroll->m_destination.is_nil() &&
          !document.find(scroll->m_destination)) {
        throw std::invalid_argument("Unknown layer.");
      }
    }
  }
}

double Rellu::to_stored_seconds(double seconds) {
  return static_cast<float>(seconds);
}

Animation Rellu::make_default_animation() {
  return Animation(Transition::DISSOLVE, Easing::EASE_OUT, DEFAULT_DURATION);
}

Animation Rellu::make_empty_animation() {
  return Animation(Transition::NONE, Easing::EASE_OUT, DEFAULT_DURATION);
}

Interaction Rellu::make_empty_interaction(const NodeId& owner) {
  auto interaction = Interaction();
  interaction.m_owner = owner;
  interaction.m_trigger = TapTrigger();
  interaction.m_action = ArtboardTransition(NodeId(), make_empty_animation());
  return interaction;
}

Interaction Rellu::make_new_interaction(const Document& document,
    const NodeId& owner, bool has_states, const Interaction* last) {
  auto interaction = make_empty_interaction(owner);
  if(last) {
    interaction.m_trigger = last->m_trigger;
    if(std::holds_alternative<KeyTrigger>(interaction.m_trigger)) {
      interaction.m_trigger = KeyTrigger();
    }
    if(auto transition = std::get_if<ArtboardTransition>(&last->m_action)) {
      interaction.m_action = ArtboardTransition(
        NodeId(), transition->m_animation, transition->m_preserve_scroll);
    } else if(auto state = std::get_if<StateTransition>(&last->m_action)) {
      interaction.m_action = StateTransition(StateId(), state->m_animation);
    } else if(auto overlay = std::get_if<Overlay>(&last->m_action)) {
      interaction.m_action =
        Overlay(NodeId(), Point(0, 0), overlay->m_animation);
    } else if(std::holds_alternative<Hyperlink>(last->m_action)) {
      interaction.m_action = Hyperlink();
    } else if(auto scroll = std::get_if<ScrollTransition>(&last->m_action)) {
      interaction.m_action =
        ScrollTransition(NodeId(), scroll->m_offset, scroll->m_animation);
    }
  }
  auto node = document.find(owner);
  auto is_artboard = node && node->get_artboard();
  if(auto scroll = std::get_if<ScrollTransition>(&interaction.m_action);
      scroll && is_artboard) {
    interaction.m_action = ArtboardTransition(NodeId(), scroll->m_animation);
  }
  auto get_order = [] (const Trigger& trigger) {
    if(std::holds_alternative<DragTrigger>(trigger)) {
      return 1;
    } else if(std::holds_alternative<HoverTrigger>(trigger)) {
      return 2;
    } else if(std::holds_alternative<TimeTrigger>(trigger)) {
      return 3;
    } else if(std::holds_alternative<KeyTrigger>(trigger)) {
      return 4;
    }
    return 0;
  };
  auto make_trigger = [] (int order) -> Trigger {
    if(order == 1) {
      return DragTrigger();
    } else if(order == 2) {
      return HoverTrigger();
    } else if(order == 3) {
      return TimeTrigger();
    } else if(order == 4) {
      return KeyTrigger();
    }
    return TapTrigger();
  };
  auto is_unique = [] (int order) {
    return order < 4;
  };
  auto is_valid = [&] (int order) {
    return (order != 2 || has_states) && (order != 3 || is_artboard);
  };
  auto& trigger = interaction.m_trigger;
  auto original = get_order(trigger);
  if(!is_valid(original)) {
    trigger = TapTrigger();
  }
  auto make_auto_animate = [&] {
    if(auto transition =
        std::get_if<ArtboardTransition>(&interaction.m_action)) {
      transition->m_animation.m_transition = Transition::AUTO_ANIMATE;
    } else {
      auto animation = get_animation(interaction.m_action);
      animation.m_transition = Transition::AUTO_ANIMATE;
      interaction.m_action = ArtboardTransition(NodeId(), animation);
    }
  };
  auto is_auto_animate = [&] {
    auto transition = std::get_if<ArtboardTransition>(&interaction.m_action);
    return transition &&
      transition->m_animation.m_transition == Transition::AUTO_ANIMATE;
  };
  if(std::holds_alternative<DragTrigger>(trigger) && !is_auto_animate()) {
    make_auto_animate();
  }
  if(auto order = get_order(trigger); is_unique(order)) {
    auto used = std::vector<int>();
    for(auto other : get_interactions(document, owner)) {
      used.push_back(get_order(other->m_trigger));
    }
    if(std::ranges::contains(used, order)) {
      used.push_back(original);
      if(!std::holds_alternative<ArtboardTransition>(interaction.m_action) &&
          !std::holds_alternative<StateTransition>(interaction.m_action)) {
        used.push_back(1);
        used.push_back(2);
      }
      auto next = std::ranges::max(used);
      if(is_unique(next)) {
        ++next;
      }
      while(next < 4 && !is_valid(next)) {
        ++next;
      }
      trigger = make_trigger(next);
      if(auto time = std::get_if<TimeTrigger>(&trigger)) {
        time->m_delay = DEFAULT_DELAY;
      }
    }
  }
  if(std::holds_alternative<DragTrigger>(trigger) && !is_auto_animate()) {
    make_auto_animate();
  }
  if(auto state = std::get_if<StateTransition>(&interaction.m_action)) {
    if(std::holds_alternative<DragTrigger>(trigger) ||
        std::holds_alternative<TimeTrigger>(trigger)) {
      interaction.m_action = ArtboardTransition(NodeId(), state->m_animation);
    }
  } else if(std::holds_alternative<HoverTrigger>(trigger)) {
    auto animation = get_animation(interaction.m_action);
    animation.m_transition = Transition::AUTO_ANIMATE;
    interaction.m_action = StateTransition(StateId(), animation);
  }
  return interaction;
}

Interaction Rellu::make_default_interaction(
    const NodeId& owner, const NodeId& destination) {
  auto interaction = Interaction();
  interaction.m_owner = owner;
  interaction.m_trigger = TapTrigger();
  interaction.m_action =
    ArtboardTransition(destination, make_default_animation());
  return interaction;
}

bool Rellu::is_complete(
    const Document& document, const Interaction& interaction) {
  auto& action = interaction.m_action;
  if(auto link = std::get_if<Hyperlink>(&action)) {
    return link->m_url.find_first_not_of(" \t\r\n") != std::string::npos;
  } else if(auto transition = std::get_if<ArtboardTransition>(&action)) {
    return !transition->m_destination.is_nil() &&
      transition->m_destination !=
        find_artboard(document, interaction.m_owner);
  } else if(auto overlay = std::get_if<Overlay>(&action)) {
    return !overlay->m_destination.is_nil();
  } else if(auto state = std::get_if<StateTransition>(&action)) {
    return !state->m_state.is_nil();
  } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
    return !scroll->m_destination.is_nil();
  }
  return true;
}

Interaction Rellu::retarget_interaction(const Document& document,
    Interaction interaction, const NodeId& destination) {
  auto node = document.find(destination);
  if(!node) {
    return interaction;
  }
  auto& action = interaction.m_action;
  auto animation = get_animation(action);
  if(node->get_artboard()) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      transition->m_destination = destination;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      overlay->m_destination = destination;
    } else {
      action = ArtboardTransition(destination, animation);
    }
    return interaction;
  }
  auto layer = destination;
  for(auto depth = 0; depth != 512; ++depth) {
    auto current = document.find(layer);
    if(!current || current->m_parent.is_nil()) {
      break;
    }
    auto parent = document.find(current->m_parent);
    if(!parent || parent->get_artboard()) {
      break;
    }
    layer = current->m_parent;
  }
  auto offset = 0.0;
  if(auto scroll = std::get_if<ScrollTransition>(&action)) {
    offset = scroll->m_offset;
  }
  animation.m_transition = Transition::DISSOLVE;
  action = ScrollTransition(layer, offset, animation);
  if(!std::holds_alternative<TapTrigger>(interaction.m_trigger) &&
      !std::holds_alternative<KeyTrigger>(interaction.m_trigger)) {
    interaction.m_trigger = TapTrigger();
  }
  return interaction;
}

Interaction Rellu::make_wired_interaction(const Document& document,
    const NodeId& owner, const NodeId& destination, bool has_states,
    const Interaction* last) {
  return retarget_interaction(document,
    make_new_interaction(document, owner, has_states, last), destination);
}

std::string Rellu::make_flow_name(const Prototype& prototype) {
  auto pattern = localize("name", "Flow %1");
  auto prefix = std::string(pattern.substr(0, pattern.find("%1")));
  auto used = std::vector<int>();
  for(auto& flow : prototype.m_flows) {
    auto name = std::string_view(flow.m_name);
    if(!name.starts_with(prefix)) {
      continue;
    }
    name.remove_prefix(prefix.size());
    auto number = 0;
    auto result =
      std::from_chars(name.data(), name.data() + name.size(), number);
    if(result.ec == std::errc() && result.ptr == name.data() + name.size()) {
      used.push_back(number);
    }
  }
  auto number = 1;
  while(std::ranges::contains(used, number)) {
    ++number;
  }
  return prefix + std::to_string(number);
}

std::vector<const Interaction*> Rellu::get_interactions(
    const Document& document, const NodeId& owner) {
  auto interactions = std::vector<const Interaction*>();
  for(auto& interaction : document.get_prototype().m_interactions) {
    if(interaction.m_owner == owner) {
      interactions.push_back(&interaction);
    }
  }
  return interactions;
}

const Flow* Rellu::find_flow(const Document& document, const NodeId& artboard) {
  for(auto& flow : document.get_prototype().m_flows) {
    if(flow.m_artboard == artboard) {
      return &flow;
    }
  }
  return nullptr;
}

std::vector<NodeId> Rellu::get_scroll_destinations(
    const Document& document, const NodeId& owner) {
  auto destinations = std::vector<NodeId>();
  auto artboard = find_artboard(document, owner);
  if(artboard.is_nil() || artboard == owner) {
    return destinations;
  }
  auto& children = document.get(artboard).m_children;
  for(auto i = children.rbegin(); i != children.rend(); ++i) {
    if(*i != owner) {
      destinations.push_back(*i);
    }
  }
  return destinations;
}

double Rellu::clamp_scroll_offset(
    const Document& document, const NodeId& destination, double offset) {
  if(destination.is_nil() || !document.find(destination)) {
    return 0;
  }
  auto artboard = find_artboard(document, destination);
  auto node = artboard.is_nil() ? nullptr : document.find(artboard);
  if(!node || !node->get_artboard()) {
    return 0;
  }
  auto& frame = node->get_artboard()->m_frame;
  auto bounds = get_world_bounds(document, destination);
  auto top = bounds ? bounds->m_y : frame.m_y;
  auto minimum = frame.m_y - top;
  auto maximum = frame.m_y + frame.m_height - top;
  return std::round(std::min(maximum, std::max(minimum, offset)));
}

Interaction Rellu::change_trigger(Interaction interaction, Trigger trigger) {
  auto& action = interaction.m_action;
  auto animation = get_animation(action);
  if(std::holds_alternative<TimeTrigger>(trigger)) {
    if(std::holds_alternative<ScrollTransition>(action)) {
      trigger = TapTrigger();
    } else if(std::holds_alternative<StateTransition>(action)) {
      action = ArtboardTransition(NodeId(), animation);
    }
  } else if(std::holds_alternative<DragTrigger>(trigger)) {
    animation.m_transition = Transition::AUTO_ANIMATE;
    animation.m_easing = Easing::LINEAR;
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      transition->m_animation = animation;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      action = ArtboardTransition(overlay->m_destination, animation);
    } else {
      action = ArtboardTransition(NodeId(), animation);
    }
  } else if(std::holds_alternative<HoverTrigger>(trigger) &&
      !std::holds_alternative<StateTransition>(action)) {
    animation.m_transition = Transition::AUTO_ANIMATE;
    action = StateTransition(StateId(), animation);
  }
  interaction.m_trigger = std::move(trigger);
  return interaction;
}

ComponentStateChoices Rellu::find_component_states(
    const Document& document, const NodeId& id) {
  auto current = id;
  auto owner = NodeId();
  auto tree_state = StateId();
  for(auto depth = 0; depth != 512 && !current.is_nil(); ++depth) {
    auto node = document.find(current);
    if(!node) {
      break;
    }
    if(get_instance(*node)) {
      owner = current;
      break;
    }
    if(node->m_parent.is_nil() && !document.is_on_canvas(current)) {
      auto root = current;
      document.get_nodes().for_each([&] (const auto& candidate) {
        if(auto instance = get_instance(candidate)) {
          for(auto& state : instance->m_states) {
            if(state.m_tree == root) {
              owner = candidate.get_id();
              tree_state = state.m_id;
            }
          }
        }
      });
      break;
    }
    current = node->m_parent;
  }
  auto choices = ComponentStateChoices();
  if(owner.is_nil()) {
    return choices;
  }
  auto& instance = *get_instance(document.get(owner));
  auto& components = document.get_resources().m_components;
  auto component = components.find(instance.m_component);
  auto default_state = instance.m_component;
  if(component != components.end() &&
      !component->second.m_default_state.is_nil()) {
    default_state = component->second.m_default_state;
  }
  choices.m_current = tree_state;
  if(choices.m_current.is_nil()) {
    choices.m_current = instance.m_state;
  }
  if(choices.m_current.is_nil()) {
    choices.m_current = default_state;
  }
  auto& states = choices.m_states;
  states.push_back(ComponentStateChoice(default_state, "Default State"));
  auto add = [&] (const ComponentState& state) {
    if(std::ranges::any_of(states, [&] (const auto& choice) {
        return choice.m_id == state.m_id;
      })) {
      return;
    }
    states.push_back(
      ComponentStateChoice(state.m_id, state.m_name));
  };
  for(auto& state : instance.m_states) {
    add(state);
  }
  if(component != components.end()) {
    for(auto& state : component->second.m_states) {
      add(state);
    }
  }
  return choices;
}

InteractionId Rellu::find_default_interaction(
    const Document& document, const NodeId& owner) {
  auto interactions = get_interactions(document, owner);
  if(interactions.empty()) {
    return InteractionId();
  }
  for(auto interaction : interactions) {
    if(!is_same_wire(*interaction, *interactions.front())) {
      return InteractionId();
    }
  }
  sort_by_trigger(interactions);
  return interactions.front()->m_id;
}

NodeId Rellu::get_wire_destination(const Interaction& interaction) {
  auto& action = interaction.m_action;
  if(auto transition = std::get_if<ArtboardTransition>(&action)) {
    return transition->m_destination;
  } else if(auto overlay = std::get_if<Overlay>(&action)) {
    return overlay->m_destination;
  } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
    return scroll->m_destination;
  }
  return NodeId();
}

bool Rellu::is_same_wire(const Interaction& left, const Interaction& right) {
  return left.m_owner == right.m_owner &&
    get_wire_destination(left) == get_wire_destination(right);
}

void Rellu::sort_by_trigger(std::vector<const Interaction*>& interactions) {
  auto get_rank = [] (const Trigger& trigger) {
    if(std::holds_alternative<DragTrigger>(trigger)) {
      return 1;
    } else if(std::holds_alternative<HoverTrigger>(trigger)) {
      return 2;
    } else if(std::holds_alternative<TimeTrigger>(trigger)) {
      return 3;
    } else if(std::holds_alternative<KeyTrigger>(trigger)) {
      return 4;
    }
    return 0;
  };
  std::ranges::stable_sort(interactions, [&] (auto left, auto right) {
    auto& first = left->m_trigger;
    auto& second = right->m_trigger;
    if(get_rank(first) != get_rank(second)) {
      return get_rank(first) < get_rank(second);
    }
    if(auto time = std::get_if<TimeTrigger>(&first)) {
      return time->m_delay < std::get<TimeTrigger>(second).m_delay;
    }
    if(auto key = std::get_if<KeyTrigger>(&first)) {
      auto& other = std::get<KeyTrigger>(second);
      if(key->m_device != other.m_device) {
        return key->m_device < other.m_device;
      }
      return key->m_keys < other.m_keys;
    }
    return false;
  });
}

std::vector<const Interaction*> Rellu::get_wire(
    const Document& document, const Interaction& interaction) {
  auto wire = std::vector<const Interaction*>();
  for(auto candidate : get_interactions(document, interaction.m_owner)) {
    if(is_same_wire(*candidate, interaction)) {
      wire.push_back(candidate);
    }
  }
  sort_by_trigger(wire);
  return wire;
}

InteractionId Rellu::find_next_interaction(
    const Document& document, const std::vector<InteractionId>& removed) {
  auto deleted = std::vector<const Interaction*>();
  for(auto& id : removed) {
    if(auto interaction = find_interaction(document, id)) {
      deleted.push_back(interaction);
    }
  }
  if(deleted.empty() || std::ranges::any_of(deleted, [&] (auto interaction) {
      return !is_same_wire(*interaction, *deleted.front());
    })) {
    return InteractionId();
  }
  sort_by_trigger(deleted);
  auto first = deleted.front();
  auto wire = std::vector<const Interaction*>();
  for(auto interaction : get_interactions(document, first->m_owner)) {
    if(is_same_wire(*interaction, *first) &&
        std::ranges::find(deleted, interaction) == deleted.end()) {
      wire.push_back(interaction);
    }
  }
  if(wire.empty()) {
    return InteractionId();
  }
  wire.push_back(first);
  sort_by_trigger(wire);
  auto index = static_cast<std::size_t>(
    std::ranges::find(wire, first) - wire.begin());
  return wire[index == 0 ? 1 : index - 1]->m_id;
}

std::vector<std::string> Rellu::label_interactions(
    const std::vector<const Interaction*>& interactions) {
  auto labels = std::vector<std::string>();
  auto counts = std::vector<std::pair<std::string, int>>();
  auto sorted = interactions;
  sort_by_trigger(sorted);
  for(auto interaction : sorted) {
    auto key = std::get_if<KeyTrigger>(&interaction->m_trigger);
    if(key && !key->m_keys.empty()) {
      labels.push_back(format_text(localize("prototype", KEYS_LABEL),
        {std::string(localize("prototype", "Keys")),
          strip_gamepad(key->m_keys)}));
      continue;
    }
    auto name = get_trigger_name(interaction->m_trigger);
    auto count = std::ranges::find(
      counts, name, &std::pair<std::string, int>::first);
    if(count == counts.end()) {
      counts.emplace_back(name, 1);
      labels.push_back(name);
      continue;
    }
    if(count->second == 1) {
      if(auto first = std::ranges::find(labels, name);
          first != labels.end()) {
        *first = name + " - 1";
      }
    }
    ++count->second;
    labels.push_back(name + " - " + std::to_string(count->second));
  }
  return labels;
}

std::string Rellu::summarize_wire(
    const std::vector<const Interaction*>& interactions) {
  struct KeyGroup {
    std::string m_device;
    std::optional<std::string> m_keys;
    int m_count = 0;
  };
  auto counts = std::vector<std::pair<std::string, int>>();
  auto keys = std::vector<KeyGroup>();
  auto sorted = interactions;
  sort_by_trigger(sorted);
  for(auto interaction : sorted) {
    if(auto key = std::get_if<KeyTrigger>(&interaction->m_trigger)) {
      auto device = std::string(localize(
        "prototype", key->m_keys.empty() ? "Keys & Gamepad" : "Keys"));
      auto label = std::optional<std::string>();
      if(!key->m_keys.empty()) {
        label = strip_gamepad(key->m_keys);
      }
      auto group = std::ranges::find(keys, device, &KeyGroup::m_device);
      if(group == keys.end()) {
        keys.push_back(KeyGroup(device, label, 1));
      } else {
        if(group->m_keys != label) {
          group->m_keys = std::nullopt;
        }
        ++group->m_count;
      }
      continue;
    }
    auto name = get_trigger_name(interaction->m_trigger);
    auto count = std::ranges::find(
      counts, name, &std::pair<std::string, int>::first);
    if(count == counts.end()) {
      counts.emplace_back(name, 1);
    } else {
      ++count->second;
    }
  }
  auto labels = std::vector<std::string>();
  for(auto& count : counts) {
    if(count.second > 1) {
      labels.push_back(
        count.first + " (" + std::to_string(count.second) + ")");
    } else {
      labels.push_back(count.first);
    }
  }
  for(auto& group : keys) {
    if(group.m_keys) {
      if(group.m_count == 1) {
        labels.push_back(format_text(localize("prototype", KEYS_LABEL),
          {group.m_device, *group.m_keys}));
      } else {
        labels.push_back(format_text(localize("prototype", KEYS_COUNT_LABEL),
          {group.m_device, *group.m_keys, std::to_string(group.m_count)}));
      }
    } else if(group.m_count > 1) {
      labels.push_back(format_text(localize("prototype", COUNT_LABEL),
        {group.m_device, std::to_string(group.m_count)}));
    } else {
      labels.push_back(group.m_device);
    }
  }
  auto summary = std::string();
  for(auto& label : labels) {
    if(!summary.empty()) {
      summary += ", ";
    }
    summary += label;
  }
  return summary;
}

AddInteractionCommand::AddInteractionCommand(Interaction interaction)
    : m_interaction(std::move(interaction)) {
  if(m_interaction.m_id.is_nil()) {
    m_interaction.m_id = generate_node_id();
  }
}

const Interaction& AddInteractionCommand::get_interaction() const {
  return m_interaction;
}

std::string AddInteractionCommand::get_label() const {
  return "Add Interaction";
}

void AddInteractionCommand::apply(
    const Document& document, Transaction& transaction) {
  require_destination(document, m_interaction);
  transaction.add_interaction(m_interaction);
}

SetInteractionCommand::SetInteractionCommand(
  Interaction interaction) noexcept
  : m_interaction(std::move(interaction)) {}

std::string SetInteractionCommand::get_label() const {
  return "Change Interaction";
}

void SetInteractionCommand::apply(
    const Document& document, Transaction& transaction) {
  auto current = find_interaction(document, m_interaction.m_id);
  if(!current) {
    throw std::invalid_argument("Unknown interaction.");
  }
  require_destination(document, m_interaction);
  if(*current != m_interaction) {
    transaction.replace_interaction(m_interaction);
  }
}

SetScrollOffsetCommand::SetScrollOffsetCommand(
  const InteractionId& id, double offset) noexcept
  : m_id(id),
    m_offset(offset) {}

std::string SetScrollOffsetCommand::get_label() const {
  return "Y-Offset";
}

void SetScrollOffsetCommand::apply(
    const Document& document, Transaction& transaction) {
  auto current = find_interaction(document, m_id);
  if(!current ||
      !std::holds_alternative<ScrollTransition>(current->m_action)) {
    throw std::invalid_argument("Not a Scroll To.");
  }
  auto changed = *current;
  auto& scroll = std::get<ScrollTransition>(changed.m_action);
  if(equals(scroll.m_offset, m_offset)) {
    return;
  }
  scroll.m_offset = m_offset;
  transaction.replace_interaction(std::move(changed));
}

bool SetScrollOffsetCommand::merge(const Command& command) {
  auto offset = dynamic_cast<const SetScrollOffsetCommand*>(&command);
  if(!offset || offset->m_id != m_id) {
    return false;
  }
  m_offset = offset->m_offset;
  return true;
}

RemoveInteractionCommand::RemoveInteractionCommand(
  const InteractionId& id) noexcept
  : m_id(id) {}

std::string RemoveInteractionCommand::get_label() const {
  return "Delete Interaction";
}

void RemoveInteractionCommand::apply(
    const Document& document, Transaction& transaction) {
  if(!find_interaction(document, m_id)) {
    throw std::invalid_argument("Unknown interaction.");
  }
  transaction.remove_interaction(m_id);
}

AddFlowCommand::AddFlowCommand(
  const NodeId& artboard, std::string name) noexcept
  : m_artboard(artboard),
    m_name(std::move(name)) {}

std::string AddFlowCommand::get_label() const {
  return "Mark as flow root";
}

void AddFlowCommand::apply(
    const Document& document, Transaction& transaction) {
  require_artboard(document, m_artboard);
  if(find_flow(document, m_artboard)) {
    return;
  }
  auto& prototype = document.get_prototype();
  auto flows = prototype.m_flows;
  auto name = m_name;
  if(name.empty()) {
    name = make_flow_name(prototype);
  }
  flows.push_back(Flow(m_artboard, std::move(name)));
  transaction.set_flows(std::move(flows));
}

RemoveFlowCommand::RemoveFlowCommand(const NodeId& artboard) noexcept
  : m_artboard(artboard) {}

std::string RemoveFlowCommand::get_label() const {
  return "Unmark as flow root";
}

void RemoveFlowCommand::apply(
    const Document& document, Transaction& transaction) {
  auto& prototype = document.get_prototype();
  auto flows = prototype.m_flows;
  auto count = std::erase_if(flows, [&] (const auto& flow) {
    return flow.m_artboard == m_artboard;
  });
  if(count != 0) {
    transaction.set_flows(std::move(flows));
  }
  if(prototype.m_home_artboard == m_artboard) {
    transaction.set_home_artboard(NodeId());
  }
}

RenameFlowCommand::RenameFlowCommand(
  const NodeId& artboard, std::string name) noexcept
  : m_artboard(artboard),
    m_name(std::move(name)) {}

std::string RenameFlowCommand::get_label() const {
  return "Rename Flow";
}

void RenameFlowCommand::apply(
    const Document& document, Transaction& transaction) {
  auto flows = document.get_prototype().m_flows;
  auto flow = std::ranges::find_if(flows, [&] (const auto& flow) {
    return flow.m_artboard == m_artboard;
  });
  if(flow == flows.end()) {
    throw std::invalid_argument("No flow starts on the artboard.");
  }
  if(flow->m_name != m_name) {
    flow->m_name = m_name;
    transaction.set_flows(std::move(flows));
  }
}

SetHomeArtboardCommand::SetHomeArtboardCommand(
  const NodeId& artboard) noexcept
  : m_artboard(artboard) {}

std::string SetHomeArtboardCommand::get_label() const {
  return "Set Home Artboard";
}

void SetHomeArtboardCommand::apply(
    const Document& document, Transaction& transaction) {
  if(!m_artboard.is_nil()) {
    require_artboard(document, m_artboard);
  }
  if(document.get_prototype().m_home_artboard != m_artboard) {
    transaction.set_home_artboard(m_artboard);
  }
}

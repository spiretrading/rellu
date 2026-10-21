#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"

using namespace Rellu;

namespace {
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

InteractionId Rellu::find_default_interaction(
    const Document& document, const NodeId& owner) {
  auto interactions = get_interactions(document, owner);
  if(interactions.empty()) {
    return InteractionId();
  }
  auto get_wire = [] (const Interaction& interaction) {
    auto& action = interaction.m_action;
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      return transition->m_destination;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      return overlay->m_destination;
    } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
      return scroll->m_destination;
    }
    return NodeId();
  };
  auto wire = get_wire(*interactions.front());
  for(auto interaction : interactions) {
    if(get_wire(*interaction) != wire) {
      return InteractionId();
    }
  }
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
  return interactions.front()->m_id;
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
  return "Add Flow";
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
  return "Remove Flow";
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

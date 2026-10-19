#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <utility>
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
  }
}

Animation Rellu::make_default_animation() {
  return Animation(Transition::DISSOLVE, Easing::EASE_OUT, DEFAULT_DURATION);
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
  constexpr auto PREFIX = std::string_view("Flow ");
  auto used = std::vector<int>();
  for(auto& flow : prototype.m_flows) {
    auto name = std::string_view(flow.m_name);
    if(!name.starts_with(PREFIX)) {
      continue;
    }
    name.remove_prefix(PREFIX.size());
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
  return std::string(PREFIX) + std::to_string(number);
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

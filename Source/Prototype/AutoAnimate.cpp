#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Prototype/AutoAnimate.hpp"
#include "Rellu/Prototype/Interpolation.hpp"

using namespace Rellu;

namespace {
  bool is_container(const Node& node) {
    if(auto reference = node.get_reference()) {
      return reference->m_has_children;
    }
    return node.get_group() || node.get_artboard();
  }

  bool is_interpolated(const Node& node) {
    return node.get_shape() || node.get_artboard();
  }

  bool is_compatible(const Node& left, const Node& right) {
    return left.get_kind() == right.get_kind() || left.get_reference() ||
      right.get_reference();
  }

  Matrix get_local_transform(const Document& document, const Node& node) {
    if(node.get_reference()) {
      if(auto resolution =
          resolve_reference(document.get_nodes(), node.get_id())) {
        return resolution->m_transform;
      }
    }
    return node.m_header.m_transform;
  }
}

std::vector<LayerMatch> Rellu::match_layers(
    const Document& document, const NodeId& from, const NodeId& to) {
  auto matches = std::vector<LayerMatch>();
  auto source = document.find(from);
  auto target = document.find(to);
  if(!source || !target) {
    return matches;
  }
  matches.push_back(LayerMatch(from, to));
  for(auto i = std::size_t(0); i != matches.size(); ++i) {
    auto pair = matches[i];
    auto& first = document.get(pair.m_from);
    auto& second = document.get(pair.m_to);
    if(!is_container(first) || !is_container(second)) {
      continue;
    }
    auto& sources = first.m_children;
    auto used = std::vector<bool>(sources.size(), false);
    for(auto& id : second.m_children) {
      auto& child = document.get(id);
      for(auto j = std::size_t(0); j != sources.size(); ++j) {
        if(used[j]) {
          continue;
        }
        auto& candidate = document.get(sources[j]);
        if(candidate.m_header.m_name == child.m_header.m_name &&
            is_compatible(candidate, child)) {
          used[j] = true;
          matches.push_back(LayerMatch(sources[j], id));
          break;
        }
      }
    }
  }
  return matches;
}

bool Rellu::is_auto_animate_matched(
    const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node || node->m_parent.is_nil()) {
    return false;
  }
  auto artboard = node->m_parent;
  if(!document.get(artboard).get_artboard()) {
    return false;
  }
  auto linked = std::vector<NodeId>();
  for(auto& interaction : document.get_prototype().m_interactions) {
    auto transition =
      std::get_if<ArtboardTransition>(&interaction.m_action);
    if(!transition ||
        transition->m_animation.m_transition != Transition::AUTO_ANIMATE) {
      continue;
    }
    auto source = find_artboard(document, interaction.m_owner);
    auto destination = find_artboard(document, transition->m_destination);
    if(source == artboard && !destination.is_nil()) {
      linked.push_back(destination);
    } else if(destination == artboard && !source.is_nil()) {
      linked.push_back(source);
    }
  }
  for(auto& other : linked) {
    if(other == artboard) {
      continue;
    }
    for(auto& match : match_layers(document, artboard, other)) {
      if(match.m_from == id) {
        return true;
      }
    }
  }
  return false;
}

AutoAnimation::AutoAnimation(Document& document, const NodeId& from,
    const NodeId& to, bool is_matching)
    : m_document(&document),
      m_is_finished(false) {
  if(is_matching) {
    m_matches = match_layers(document, from, to);
  } else if(document.find(from) && document.find(to)) {
    m_matches.push_back(LayerMatch(from, to));
  }
  auto matched_from = std::unordered_set<NodeId>();
  auto matched_to = std::unordered_set<NodeId>();
  auto counterparts = std::unordered_map<NodeId, NodeId>();
  for(auto& match : m_matches) {
    matched_from.insert(match.m_from);
    matched_to.insert(match.m_to);
    counterparts.emplace(match.m_from, match.m_to);
  }
  for(auto i = std::size_t(0); i != m_matches.size(); ++i) {
    auto& match = m_matches[i];
    auto& start = document.get(match.m_from);
    auto& end = document.get(match.m_to);
    auto pair = Pair(match.m_to, start.m_header, start.m_payload,
      end.m_header, end.m_payload);
    pair.m_start_transform = get_local_transform(document, start);
    pair.m_end_transform = get_local_transform(document, end);
    pair.m_start_header.m_transform = pair.m_start_transform;
    if(end.get_reference()) {
      pair.m_start_header.m_transform = end.m_header.m_transform;
    }
    if(i == 0) {
      pair.m_start_header = end.m_header;
      if(!start.get_artboard() || !end.get_artboard()) {
        pair.m_start_payload = end.m_payload;
      }
    }
    m_pairs.push_back(std::move(pair));
    if(is_container(end)) {
      for(auto& child : end.m_children) {
        if(!matched_to.contains(child)) {
          m_arrivals.push_back(Fade(child, document.get(child).m_header));
        }
      }
    }
    if(is_container(start)) {
      for(auto j = std::size_t(0); j != start.m_children.size(); ++j) {
        auto& child = start.m_children[j];
        if(!matched_from.contains(child)) {
          m_departures.push_back(Departure(
            child, match.m_from, j, document.get(child).m_header));
        }
      }
    }
  }
  if(!m_departures.empty()) {
    auto transaction = Transaction(document);
    for(auto& departure : m_departures) {
      auto parent = counterparts.at(departure.m_parent);
      if(!is_container(document.get(parent))) {
        parent = to;
      }
      transaction.move(departure.m_id, parent,
        document.get_children(parent).size());
    }
    transaction.commit();
    document.clear_history();
  }
  apply(0);
}

const std::vector<LayerMatch>& AutoAnimation::get_matches() const {
  return m_matches;
}

std::vector<NodeId> AutoAnimation::get_departing() const {
  auto departing = std::vector<NodeId>();
  for(auto& departure : m_departures) {
    departing.push_back(departure.m_id);
  }
  return departing;
}

void AutoAnimation::apply(double progress) {
  if(m_is_finished) {
    return;
  }
  auto transaction = Transaction(*m_document);
  for(auto& pair : m_pairs) {
    auto header =
      interpolate(pair.m_start_header, pair.m_end_header, progress);
    if(header != m_document->get(pair.m_id).m_header) {
      transaction.replace_header(std::move(header));
    }
    auto& node = m_document->get(pair.m_id);
    if(auto reference = node.get_reference()) {
      auto payload = *reference;
      payload.m_transform =
        interpolate(pair.m_start_transform, pair.m_end_transform, progress);
      if(Payload(payload) != node.m_payload) {
        transaction.replace_payload(pair.m_id, std::move(payload));
      }
    } else if(is_interpolated(node)) {
      auto payload =
        interpolate(pair.m_start_payload, pair.m_end_payload, progress);
      if(payload != m_document->get(pair.m_id).m_payload) {
        transaction.replace_payload(pair.m_id, std::move(payload));
      }
    }
  }
  auto fade = std::clamp(progress, 0.0, 1.0);
  for(auto& arrival : m_arrivals) {
    auto header = arrival.m_header;
    header.m_opacity *= fade;
    if(header != m_document->get(arrival.m_id).m_header) {
      transaction.replace_header(std::move(header));
    }
  }
  for(auto& departure : m_departures) {
    auto header = departure.m_header;
    header.m_opacity *= 1 - fade;
    if(header != m_document->get(departure.m_id).m_header) {
      transaction.replace_header(std::move(header));
    }
  }
  transaction.commit();
  m_document->clear_history();
}

void AutoAnimation::finish() {
  if(m_is_finished) {
    return;
  }
  m_is_finished = true;
  auto transaction = Transaction(*m_document);
  for(auto& pair : m_pairs) {
    auto& node = m_document->get(pair.m_id);
    if(node.m_header != pair.m_end_header) {
      transaction.replace_header(pair.m_end_header);
    }
    if(m_document->get(pair.m_id).m_payload != pair.m_end_payload) {
      transaction.replace_payload(pair.m_id, pair.m_end_payload);
    }
  }
  for(auto& arrival : m_arrivals) {
    if(m_document->get(arrival.m_id).m_header != arrival.m_header) {
      transaction.replace_header(arrival.m_header);
    }
  }
  auto departures = m_departures;
  std::ranges::stable_sort(departures, [] (const auto& left,
      const auto& right) {
    return left.m_index < right.m_index;
  });
  for(auto& departure : departures) {
    transaction.move(departure.m_id, departure.m_parent, departure.m_index);
    transaction.replace_header(departure.m_header);
  }
  transaction.commit();
  m_document->clear_history();
}

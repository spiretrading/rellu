#include <algorithm>
#include <stdexcept>
#include <utility>
#include "Rellu/Commands/GuideCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  const Artboard* find_artboard_payload(
      const Document& document, const NodeId& id) {
    if(auto node = document.find(id)) {
      return node->get_artboard();
    }
    return nullptr;
  }

  void set_guides(const Document& document, Transaction& transaction,
      const NodeId& id, std::vector<Guide> guides) {
    auto artboard = find_artboard_payload(document, id);
    if(!artboard) {
      throw std::invalid_argument("The node is not an artboard.");
    }
    if(artboard->m_guides == guides) {
      return;
    }
    auto replacement = *artboard;
    replacement.m_guides = std::move(guides);
    transaction.replace_payload(id, std::move(replacement));
  }
}

std::vector<NodeId> Rellu::get_guide_artboards(
    const Document& document, const std::vector<NodeId>& selection) {
  auto artboards = std::vector<NodeId>();
  for(auto& id : selection) {
    if(find_artboard_payload(document, id)) {
      artboards.push_back(id);
    }
  }
  if(!artboards.empty()) {
    return artboards;
  }
  for(auto& id : selection) {
    if(!document.find(id)) {
      continue;
    }
    auto artboard = find_artboard(document, id);
    if(!artboard.is_nil() && std::find(artboards.begin(), artboards.end(),
        artboard) == artboards.end()) {
      artboards.push_back(artboard);
    }
  }
  return artboards;
}

bool Rellu::is_within_artboards(const Document& document,
    const std::vector<NodeId>& selection, bool is_multiple, bool has_guides) {
  if(selection.empty() || find_artboard_payload(document, selection.front())) {
    return false;
  }
  auto artboards = std::vector<NodeId>();
  for(auto& id : selection) {
    if(!document.find(id)) {
      return false;
    }
    auto artboard = find_artboard(document, id);
    if(artboard.is_nil()) {
      return false;
    }
    if(std::find(artboards.begin(), artboards.end(), artboard) ==
        artboards.end()) {
      if(has_guides &&
          find_artboard_payload(document, artboard)->m_guides.empty()) {
        return false;
      }
      artboards.push_back(artboard);
    }
    if(artboards.size() > 1 && !is_multiple) {
      return false;
    }
  }
  return !artboards.empty();
}

void Rellu::insert_guide(std::vector<Guide>& guides, Guide guide) {
  auto position = std::find_if(guides.begin(), guides.end(),
    [&] (const auto& other) {
      if(other.m_orientation == guide.m_orientation) {
        return other.m_position >= guide.m_position;
      }
      return other.m_orientation == Orientation::VERTICAL;
    });
  guides.insert(position, std::move(guide));
}

std::vector<Guide> Rellu::merge_guides(std::vector<Guide> guides,
    const std::vector<Guide>& pasted, Size size) {
  for(auto& guide : pasted) {
    auto limit = guide.m_orientation == Orientation::VERTICAL ?
      size.m_width - 1 : size.m_height - 1;
    if(guide.m_position > limit || std::any_of(guides.begin(), guides.end(),
        [&] (const auto& other) {
          return other.m_orientation == guide.m_orientation &&
            other.m_position == guide.m_position;
        })) {
      continue;
    }
    insert_guide(guides, guide);
  }
  return guides;
}

SetGuidesCommand::SetGuidesCommand(std::string label, const NodeId& artboard,
  std::vector<Guide> guides) noexcept
  : m_label(std::move(label)),
    m_artboard(artboard),
    m_guides(std::move(guides)) {}

std::string SetGuidesCommand::get_label() const {
  return m_label;
}

void SetGuidesCommand::apply(
    const Document& document, Transaction& transaction) {
  set_guides(document, transaction, m_artboard, m_guides);
}

ClearGuidesCommand::ClearGuidesCommand(std::vector<NodeId> artboards) noexcept
  : m_artboards(std::move(artboards)) {}

std::string ClearGuidesCommand::get_label() const {
  return "Clear Guides";
}

void ClearGuidesCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : m_artboards) {
    set_guides(document, transaction, id, {});
  }
}

PasteGuidesCommand::PasteGuidesCommand(std::vector<NodeId> artboards,
  std::vector<Guide> guides, std::function<std::string ()> make_id)
  : m_artboards(std::move(artboards)),
    m_guides(std::move(guides)),
    m_make_id(std::move(make_id)) {}

std::string PasteGuidesCommand::get_label() const {
  return "Paste Guides";
}

void PasteGuidesCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : m_artboards) {
    auto artboard = find_artboard_payload(document, id);
    if(!artboard) {
      throw std::invalid_argument("The node is not an artboard.");
    }
    auto pasted = m_guides;
    for(auto& guide : pasted) {
      guide.m_id = m_make_id();
    }
    set_guides(document, transaction, id, merge_guides(artboard->m_guides,
      pasted, artboard->m_frame.get_size()));
  }
}

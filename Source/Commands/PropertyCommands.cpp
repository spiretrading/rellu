#include <algorithm>
#include <unordered_set>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"

using namespace Rellu;

namespace {
  void edit_headers(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& nodes, auto edit) {
    auto visited = std::unordered_set<NodeId>();
    for(auto& id : nodes) {
      if(visited.insert(id).second) {
        edit_rendered_header(document, transaction, id,
          [&] (const auto&, auto& header) { edit(header); });
      }
    }
  }
}

SetOpacityCommand::SetOpacityCommand(
  std::vector<NodeId> nodes, double opacity) noexcept
  : m_nodes(std::move(nodes)),
    m_opacity(std::clamp(opacity, 0.0, 1.0)) {}

std::string SetOpacityCommand::get_label() const {
  return "Change Opacity";
}

void SetOpacityCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes, [&] (auto& header) {
    header.m_opacity = m_opacity;
  });
}

bool SetOpacityCommand::merge(const Command& command) {
  auto opacity = dynamic_cast<const SetOpacityCommand*>(&command);
  if(!opacity || opacity->m_nodes != m_nodes) {
    return false;
  }
  m_opacity = opacity->m_opacity;
  return true;
}

SetBlendModeCommand::SetBlendModeCommand(
  std::vector<NodeId> nodes, BlendMode blend_mode) noexcept
  : m_nodes(std::move(nodes)),
    m_blend_mode(blend_mode) {}

std::string SetBlendModeCommand::get_label() const {
  return "Change Blend Mode";
}

void SetBlendModeCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes, [&] (auto& header) {
    header.m_blend_mode = m_blend_mode;
  });
}

SetVisibilityCommand::SetVisibilityCommand(
  std::vector<NodeId> nodes, bool is_visible) noexcept
  : m_nodes(std::move(nodes)),
    m_is_visible(is_visible) {}

std::string SetVisibilityCommand::get_label() const {
  if(m_is_visible) {
    return "Show";
  }
  return "Hide";
}

void SetVisibilityCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes, [&] (auto& header) {
    header.m_is_visible = m_is_visible;
  });
}

SetLockCommand::SetLockCommand(
  std::vector<NodeId> nodes, bool is_locked) noexcept
  : m_nodes(std::move(nodes)),
    m_is_locked(is_locked) {}

std::string SetLockCommand::get_label() const {
  if(m_is_locked) {
    return "Lock";
  }
  return "Unlock";
}

void SetLockCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes, [&] (auto& header) {
    header.m_is_locked = m_is_locked;
  });
}

RenameCommand::RenameCommand(const NodeId& id, std::string name) noexcept
  : m_id(id),
    m_name(std::move(name)) {}

std::string RenameCommand::get_label() const {
  return "Rename";
}

void RenameCommand::apply(const Document& document, Transaction& transaction) {
  if(rename_instance(document, transaction, m_id, m_name)) {
    return;
  }
  edit_headers(document, transaction, std::vector{m_id}, [&] (auto& header) {
    header.m_name = m_name;
    header.m_has_custom_name = true;
    header.m_has_external_name = false;
  });
}

bool RenameCommand::merge(const Command& command) {
  auto rename = dynamic_cast<const RenameCommand*>(&command);
  if(!rename || rename->m_id != m_id) {
    return false;
  }
  m_name = rename->m_name;
  return true;
}

SetColorProfileCommand::SetColorProfileCommand(ColorProfile profile) noexcept
  : m_profile(profile) {}

std::string SetColorProfileCommand::get_label() const {
  if(m_profile == ColorProfile::SRGB) {
    return "sRGB";
  }
  return "Unmanaged";
}

void SetColorProfileCommand::apply(
    const Document& document, Transaction& transaction) {
  auto profile = document.get_resources().m_color_profile;
  if(profile == ColorProfile::NONE) {
    profile = ColorProfile::UNMANAGED;
  }
  if(profile != m_profile) {
    transaction.set_color_profile(m_profile);
  }
}

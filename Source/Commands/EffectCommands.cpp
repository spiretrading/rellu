#include <algorithm>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/EffectCommands.hpp"

using namespace Rellu;

namespace {
  constexpr auto DEFAULT_SHADOW_OFFSET = 3.0;
  constexpr auto DEFAULT_SHADOW_RADIUS = 3.0;
  constexpr auto DEFAULT_SHADOW_ALPHA = 41 / 255.0;
  constexpr auto DEFAULT_BLUR_AMOUNT = 30.0;
  constexpr auto DEFAULT_BRIGHTNESS = 15.0;
  constexpr auto DEFAULT_FILL_OPACITY = 0.0;

  bool is_kind(const Effect& effect, EffectKind kind) {
    if(kind == EffectKind::INNER_SHADOW) {
      return std::holds_alternative<InnerShadow>(effect);
    } else if(kind == EffectKind::DROP_SHADOW) {
      return std::holds_alternative<DropShadow>(effect);
    }
    return std::holds_alternative<Blur>(effect);
  }

  template<typename T>
  void assign(T& target, const std::optional<T>& source) {
    if(source) {
      target = *source;
    }
  }

  template<typename T>
  void assign(std::optional<T>& target, const std::optional<T>& source) {
    if(source) {
      target = source;
    }
  }

  void edit_shadow(auto& shadow, const EffectEdit& edit) {
    assign(shadow.m_is_visible, edit.m_is_visible);
    assign(shadow.m_color, edit.m_color);
    assign(shadow.m_offset.m_x, edit.m_x);
    assign(shadow.m_offset.m_y, edit.m_y);
    if(edit.m_blur) {
      shadow.m_blur = std::max(*edit.m_blur, 0.0);
    }
  }

  void edit_effect(Effect& effect, const EffectEdit& edit) {
    if(auto shadow = std::get_if<DropShadow>(&effect)) {
      edit_shadow(*shadow, edit);
    } else if(auto shadow = std::get_if<InnerShadow>(&effect)) {
      edit_shadow(*shadow, edit);
    } else if(auto blur = std::get_if<Blur>(&effect)) {
      assign(blur->m_is_visible, edit.m_is_visible);
      if(edit.m_blur) {
        blur->m_amount = std::max(*edit.m_blur, 0.0);
      }
      assign(blur->m_brightness, edit.m_brightness);
      if(edit.m_fill_opacity) {
        blur->m_fill_opacity = std::clamp(*edit.m_fill_opacity, 0.0, 1.0);
      }
      assign(blur->m_is_background, edit.m_is_background);
    }
  }

  void edit_effects(
      std::vector<Effect>& effects, EffectKind kind, const EffectEdit& edit) {
    auto effect = std::find_if(effects.begin(), effects.end(),
      [&] (const auto& effect) {
        return is_kind(effect, kind);
      });
    if(effect == effects.end()) {
      effects.push_back(make_default_effect(kind));
      effect = effects.end() - 1;
    }
    edit_effect(*effect, edit);
  }
}

Effect Rellu::make_default_effect(EffectKind kind) {
  auto color = Color(0, 0, 0, DEFAULT_SHADOW_ALPHA);
  auto offset = Point(0, DEFAULT_SHADOW_OFFSET);
  if(kind == EffectKind::INNER_SHADOW) {
    return InnerShadow(offset, DEFAULT_SHADOW_RADIUS, color, true);
  } else if(kind == EffectKind::DROP_SHADOW) {
    return DropShadow(offset, DEFAULT_SHADOW_RADIUS, color, true);
  }
  auto blur = Blur();
  blur.m_amount = DEFAULT_BLUR_AMOUNT;
  blur.m_brightness = DEFAULT_BRIGHTNESS;
  blur.m_fill_opacity = DEFAULT_FILL_OPACITY;
  blur.m_is_background = true;
  return blur;
}

const Effect* Rellu::find_effect(
    const std::vector<Effect>& effects, EffectKind kind) {
  auto effect = std::find_if(effects.begin(), effects.end(),
    [&] (const auto& effect) {
      return is_kind(effect, kind);
    });
  if(effect == effects.end()) {
    return nullptr;
  }
  return &*effect;
}

SetEffectCommand::SetEffectCommand(
  std::vector<NodeId> nodes, EffectKind kind, EffectEdit edit) noexcept
  : m_nodes(std::move(nodes)),
    m_kind(kind),
    m_edit(std::move(edit)) {}

std::string SetEffectCommand::get_label() const {
  return "Change Effect";
}

void SetEffectCommand::apply(
    const Document& document, Transaction& transaction) {
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : m_nodes) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      continue;
    }
    if(auto shape = node->get_shape()) {
      auto payload = *shape;
      edit_effects(payload.m_style.m_effects, m_kind, m_edit);
      if(payload != *shape) {
        transaction.replace_payload(id, std::move(payload));
      }
    } else if(auto text = node->get_text()) {
      auto payload = *text;
      edit_effects(payload.m_style.m_effects, m_kind, m_edit);
      if(payload != *text) {
        transaction.replace_payload(id, std::move(payload));
      }
    }
  }
}

bool SetEffectCommand::merge(const Command& command) {
  auto effect = dynamic_cast<const SetEffectCommand*>(&command);
  if(!effect || effect->m_nodes != m_nodes || effect->m_kind != m_kind) {
    return false;
  }
  auto& edit = effect->m_edit;
  assign(m_edit.m_is_visible, edit.m_is_visible);
  assign(m_edit.m_color, edit.m_color);
  assign(m_edit.m_x, edit.m_x);
  assign(m_edit.m_y, edit.m_y);
  assign(m_edit.m_blur, edit.m_blur);
  assign(m_edit.m_brightness, edit.m_brightness);
  assign(m_edit.m_fill_opacity, edit.m_fill_opacity);
  assign(m_edit.m_is_background, edit.m_is_background);
  return true;
}

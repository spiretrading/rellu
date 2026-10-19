#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/TextCommands.hpp"

using namespace Rellu;

namespace {
  const ColorSwatch* find_swatch_by_id(
      const Document& document, const SwatchId& id) {
    auto& swatches = document.get_resources().m_swatches;
    auto swatch = std::find_if(swatches.begin(), swatches.end(),
      [&] (const auto& swatch) {
        return swatch.m_id == id;
      });
    if(swatch == swatches.end()) {
      return nullptr;
    }
    return &*swatch;
  }

  const CharacterStyle* find_style(
      const Document& document, const CharacterStyleId& id) {
    auto& styles = document.get_resources().m_character_styles;
    auto style = std::find_if(styles.begin(), styles.end(),
      [&] (const auto& style) {
        return style.m_id == id;
      });
    if(style == styles.end()) {
      return nullptr;
    }
    return &*style;
  }

  bool relink(Fill& fill, const SwatchId& id, const ColorSwatch* swatch) {
    auto solid = std::get_if<SolidFill>(&fill);
    if(!solid || solid->m_swatch != id) {
      return false;
    }
    if(swatch) {
      solid->m_color = swatch->m_color;
    } else {
      solid->m_swatch = SwatchId();
    }
    return true;
  }

  void relink_fills(const Document& document, Transaction& transaction,
      const SwatchId& id, const ColorSwatch* swatch) {
    auto changes = std::vector<std::pair<NodeId, Payload>>();
    document.get_nodes().for_each([&] (const Node& node) {
      if(auto shape = node.get_shape()) {
        auto payload = *shape;
        if(relink(payload.m_style.m_fill, id, swatch) && payload != *shape) {
          changes.emplace_back(node.get_id(), std::move(payload));
        }
      } else if(auto text = node.get_text()) {
        auto payload = *text;
        if(relink(payload.m_style.m_fill, id, swatch)) {
          auto& color = std::get<SolidFill>(payload.m_style.m_fill).m_color;
          payload.m_base_style.m_color = color;
          for(auto& run : payload.m_runs) {
            run.m_style.m_color = color;
          }
          if(payload != *text) {
            changes.emplace_back(node.get_id(), std::move(payload));
          }
        }
      } else if(auto artboard = node.get_artboard()) {
        auto payload = *artboard;
        if(relink(payload.m_background, id, swatch) && payload != *artboard) {
          changes.emplace_back(node.get_id(), std::move(payload));
        }
      }
    });
    for(auto& change : changes) {
      transaction.replace_payload(change.first, std::move(change.second));
    }
  }

  TextStyleEdit make_edit(const TextStyle& style) {
    auto edit = TextStyleEdit();
    edit.m_family = style.m_font.m_family;
    edit.m_style = style.m_font.m_style;
    edit.m_postscript_name = style.m_font.m_postscript_name;
    edit.m_size = style.m_font.m_size;
    edit.m_letter_spacing = style.m_letter_spacing;
    edit.m_color = style.m_color;
    edit.m_is_underlined = style.m_is_underlined;
    edit.m_is_struck_through = style.m_is_struck_through;
    edit.m_transform = style.m_transform;
    edit.m_script = style.m_script;
    return edit;
  }

  Text restyle(const Text& text, const CharacterStyle& style) {
    auto payload = edit_text_style(text,
      TextRange(0, text.m_content->size()), make_edit(style.m_style));
    auto paragraph = ParagraphStyleEdit();
    paragraph.m_line_height = style.m_line_height;
    paragraph.m_paragraph_spacing = style.m_paragraph_spacing;
    payload = edit_paragraph_style(payload, paragraph);
    if(payload.m_runs.empty()) {
      payload.m_runs = get_runs(payload);
    }
    for(auto& run : payload.m_runs) {
      run.m_character_style = style.m_id;
    }
    if(auto solid = std::get_if<SolidFill>(&payload.m_style.m_fill)) {
      solid->m_color = style.m_style.m_color;
    }
    return payload;
  }

  void relink_text(const Document& document, Transaction& transaction,
      const CharacterStyleId& id, const CharacterStyle* style) {
    auto changes = std::vector<std::pair<NodeId, Text>>();
    document.get_nodes().for_each([&] (const Node& node) {
      auto text = node.get_text();
      if(!text) {
        return;
      }
      auto payload = *text;
      auto is_linked = false;
      for(auto& run : payload.m_runs) {
        if(run.m_character_style != id) {
          continue;
        }
        is_linked = true;
        if(style) {
          run.m_style = style->m_style;
        } else {
          run.m_character_style = CharacterStyleId();
        }
      }
      if(!is_linked) {
        return;
      }
      if(style) {
        payload.m_paragraph_style.m_line_height = style->m_line_height;
        payload.m_paragraph_style.m_paragraph_spacing =
          style->m_paragraph_spacing;
      }
      if(payload != *text) {
        changes.emplace_back(node.get_id(), std::move(payload));
      }
    });
    for(auto& change : changes) {
      transaction.replace_payload(change.first, std::move(change.second));
    }
  }

  void collect_styled(const Document& document, const NodeId& id,
      std::unordered_set<NodeId>& visited, std::vector<const Node*>& leaves) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      return;
    }
    if(node->get_group()) {
      for(auto& child : node->m_children) {
        collect_styled(document, child, visited, leaves);
      }
    } else if(node->get_shape() || node->get_text() || node->get_artboard()) {
      leaves.push_back(node);
    }
  }

  void collect_styled(const Document& document,
      const std::vector<NodeId>& nodes, std::vector<const Node*>& leaves) {
    auto visited = std::unordered_set<NodeId>();
    for(auto& id : nodes) {
      collect_styled(document, id, visited, leaves);
    }
  }

  bool is_same_style(const CharacterStyle& style, const Text& text) {
    auto described = make_character_style(text);
    return style.m_style == described.m_style &&
      style.m_line_height == described.m_line_height &&
      style.m_paragraph_spacing == described.m_paragraph_spacing;
  }
}

AddSwatchCommand::AddSwatchCommand(ColorSwatch swatch) noexcept
    : m_swatch(std::move(swatch)) {
  if(m_swatch.m_id.is_nil()) {
    m_swatch.m_id = generate_node_id();
  }
}

const SwatchId& AddSwatchCommand::get_id() const {
  return m_swatch.m_id;
}

std::string AddSwatchCommand::get_label() const {
  return "Add Color";
}

void AddSwatchCommand::apply(
    const Document& document, Transaction& transaction) {
  transaction.add_swatch(m_swatch);
}

ReplaceSwatchCommand::ReplaceSwatchCommand(ColorSwatch swatch) noexcept
  : m_swatch(std::move(swatch)) {}

std::string ReplaceSwatchCommand::get_label() const {
  return "Edit Color";
}

void ReplaceSwatchCommand::apply(
    const Document& document, Transaction& transaction) {
  auto current = find_swatch_by_id(document, m_swatch.m_id);
  if(!current) {
    throw std::invalid_argument("Unknown swatch.");
  }
  if(*current == m_swatch) {
    return;
  }
  transaction.replace_swatch(m_swatch);
  relink_fills(document, transaction, m_swatch.m_id, &m_swatch);
}

bool ReplaceSwatchCommand::merge(const Command& command) {
  auto replace = dynamic_cast<const ReplaceSwatchCommand*>(&command);
  if(!replace || replace->m_swatch.m_id != m_swatch.m_id) {
    return false;
  }
  m_swatch = replace->m_swatch;
  return true;
}

RenameSwatchCommand::RenameSwatchCommand(
  const SwatchId& id, std::string name) noexcept
  : m_id(id),
    m_name(std::move(name)) {}

std::string RenameSwatchCommand::get_label() const {
  return "Rename";
}

void RenameSwatchCommand::apply(
    const Document& document, Transaction& transaction) {
  auto current = find_swatch_by_id(document, m_id);
  if(!current) {
    throw std::invalid_argument("Unknown swatch.");
  }
  if(current->m_name == m_name) {
    return;
  }
  auto swatch = *current;
  swatch.m_name = m_name;
  transaction.replace_swatch(std::move(swatch));
}

RemoveSwatchCommand::RemoveSwatchCommand(const SwatchId& id) noexcept
  : m_id(id) {}

std::string RemoveSwatchCommand::get_label() const {
  return "Delete";
}

void RemoveSwatchCommand::apply(
    const Document& document, Transaction& transaction) {
  if(!find_swatch_by_id(document, m_id)) {
    throw std::invalid_argument("Unknown swatch.");
  }
  relink_fills(document, transaction, m_id, nullptr);
  transaction.remove_swatch(m_id);
}

AddCharacterStyleCommand::AddCharacterStyleCommand(
    CharacterStyle style) noexcept
    : m_style(std::move(style)) {
  if(m_style.m_id.is_nil()) {
    m_style.m_id = generate_node_id();
  }
}

const CharacterStyleId& AddCharacterStyleCommand::get_id() const {
  return m_style.m_id;
}

std::string AddCharacterStyleCommand::get_label() const {
  return "Add Character Style";
}

void AddCharacterStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  transaction.add_character_style(m_style);
}

ReplaceCharacterStyleCommand::ReplaceCharacterStyleCommand(
  CharacterStyle style) noexcept
  : m_style(std::move(style)) {}

std::string ReplaceCharacterStyleCommand::get_label() const {
  return "Edit Character Style";
}

void ReplaceCharacterStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  auto current = find_style(document, m_style.m_id);
  if(!current) {
    throw std::invalid_argument("Unknown character style.");
  }
  if(*current == m_style) {
    return;
  }
  transaction.replace_character_style(m_style);
  relink_text(document, transaction, m_style.m_id, &m_style);
}

bool ReplaceCharacterStyleCommand::merge(const Command& command) {
  auto replace = dynamic_cast<const ReplaceCharacterStyleCommand*>(&command);
  if(!replace || replace->m_style.m_id != m_style.m_id) {
    return false;
  }
  m_style = replace->m_style;
  return true;
}

RenameCharacterStyleCommand::RenameCharacterStyleCommand(
  const CharacterStyleId& id, std::string name) noexcept
  : m_id(id),
    m_name(std::move(name)) {}

std::string RenameCharacterStyleCommand::get_label() const {
  return "Rename";
}

void RenameCharacterStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  auto current = find_style(document, m_id);
  if(!current) {
    throw std::invalid_argument("Unknown character style.");
  }
  if(current->m_name == m_name) {
    return;
  }
  auto style = *current;
  style.m_name = m_name;
  transaction.replace_character_style(std::move(style));
}

RemoveCharacterStyleCommand::RemoveCharacterStyleCommand(
  const CharacterStyleId& id) noexcept
  : m_id(id) {}

std::string RemoveCharacterStyleCommand::get_label() const {
  return "Delete";
}

void RemoveCharacterStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  if(!find_style(document, m_id)) {
    throw std::invalid_argument("Unknown character style.");
  }
  relink_text(document, transaction, m_id, nullptr);
  transaction.remove_character_style(m_id);
}

ApplyCharacterStyleCommand::ApplyCharacterStyleCommand(
  std::vector<NodeId> nodes, const CharacterStyleId& style) noexcept
  : m_nodes(std::move(nodes)),
    m_style(style) {}

std::string ApplyCharacterStyleCommand::get_label() const {
  return "Apply Character Style";
}

void ApplyCharacterStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  auto style = find_style(document, m_style);
  if(!style) {
    throw std::invalid_argument("Unknown character style.");
  }
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : m_nodes) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      continue;
    }
    if(auto text = node->get_text()) {
      auto payload = restyle(*text, *style);
      if(payload != *text) {
        transaction.replace_payload(id, std::move(payload));
      }
    }
  }
}

std::unique_ptr<Command> Rellu::make_add_colors_command(
    const Document& document, const std::vector<NodeId>& nodes) {
  auto leaves = std::vector<const Node*>();
  collect_styled(document, nodes, leaves);
  auto& existing = document.get_resources().m_swatches;
  auto swatches = std::vector<ColorSwatch>();
  auto add = [&] (ColorSwatch swatch) {
    auto is_same = [&] (const ColorSwatch& other) {
      return other.m_color == swatch.m_color &&
        other.m_gradient == swatch.m_gradient;
    };
    if(std::none_of(existing.begin(), existing.end(), is_same) &&
        std::none_of(swatches.begin(), swatches.end(), is_same)) {
      swatches.push_back(std::move(swatch));
    }
  };
  auto add_fill = [&] (const Fill& fill) {
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      add(ColorSwatch(SwatchId(), std::string(), solid->m_color));
    } else if(auto gradient = std::get_if<GradientFill>(&fill);
        gradient && !gradient->m_stops.empty()) {
      add(ColorSwatch(SwatchId(), std::string(),
        gradient->m_stops.front().m_color, gradient->m_stops));
    }
  };
  for(auto leaf : leaves) {
    auto style = leaf->get_shape() ? &leaf->get_shape()->m_style :
      leaf->get_text() ? &leaf->get_text()->m_style : nullptr;
    if(auto artboard = leaf->get_artboard()) {
      add_fill(artboard->m_background);
    }
    if(!style) {
      continue;
    }
    add_fill(style->m_fill);
    if(style->m_stroke && style->m_stroke->m_is_enabled) {
      add(ColorSwatch(SwatchId(), std::string(), style->m_stroke->m_color));
    }
  }
  if(swatches.empty()) {
    return nullptr;
  }
  auto commands = std::vector<std::unique_ptr<Command>>();
  for(auto& swatch : swatches) {
    commands.push_back(std::make_unique<AddSwatchCommand>(std::move(swatch)));
  }
  return std::make_unique<CompositeCommand>("Add Color", std::move(commands));
}

std::unique_ptr<Command> Rellu::make_add_character_styles_command(
    const Document& document, const std::vector<NodeId>& nodes) {
  auto leaves = std::vector<const Node*>();
  collect_styled(document, nodes, leaves);
  auto& existing = document.get_resources().m_character_styles;
  auto styles = std::vector<CharacterStyle>();
  for(auto leaf : leaves) {
    auto text = leaf->get_text();
    if(!text) {
      continue;
    }
    auto style = make_character_style(*text);
    auto is_same = [&] (const CharacterStyle& other) {
      return other.m_style == style.m_style &&
        other.m_line_height == style.m_line_height &&
        other.m_paragraph_spacing == style.m_paragraph_spacing;
    };
    if(std::none_of(existing.begin(), existing.end(), is_same) &&
        std::none_of(styles.begin(), styles.end(), is_same)) {
      styles.push_back(std::move(style));
    }
  }
  if(styles.empty()) {
    return nullptr;
  }
  auto commands = std::vector<std::unique_ptr<Command>>();
  for(auto& style : styles) {
    commands.push_back(
      std::make_unique<AddCharacterStyleCommand>(std::move(style)));
  }
  return std::make_unique<CompositeCommand>(
    "Add Character Style", std::move(commands));
}

void Rellu::add_asset_commands(CommandRegistry& registry) {
  registry.add("object.add_color", make_add_colors_command);
  registry.add(
    "object.add_character_style", make_add_character_styles_command);
}

Fill Rellu::make_swatch_fill(const ColorSwatch& swatch) {
  if(swatch.m_gradient.empty()) {
    return SolidFill(swatch.m_color, swatch.m_id);
  }
  auto gradient = GradientFill();
  gradient.m_geometry = LinearGradient(Point(0, 0.5), Point(1, 0.5));
  gradient.m_stops = swatch.m_gradient;
  return gradient;
}

CharacterStyle Rellu::make_character_style(const Text& text) {
  auto style = CharacterStyle();
  style.m_style = get_style(text, TextRange(0, 0));
  style.m_line_height = text.m_paragraph_style.m_line_height;
  style.m_paragraph_spacing = text.m_paragraph_style.m_paragraph_spacing;
  return style;
}

const ColorSwatch* Rellu::find_swatch(
    const Resources& resources, const Fill& fill) {
  auto& swatches = resources.m_swatches;
  if(auto solid = std::get_if<SolidFill>(&fill)) {
    if(!solid->m_swatch.is_nil()) {
      for(auto& swatch : swatches) {
        if(swatch.m_id == solid->m_swatch) {
          return &swatch;
        }
      }
    }
    for(auto& swatch : swatches) {
      if(swatch.m_gradient.empty() && swatch.m_color == solid->m_color) {
        return &swatch;
      }
    }
  } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
    for(auto& swatch : swatches) {
      if(!swatch.m_gradient.empty() && swatch.m_gradient == gradient->m_stops) {
        return &swatch;
      }
    }
  }
  return nullptr;
}

const CharacterStyle* Rellu::find_character_style(
    const Resources& resources, const Text& text) {
  auto& styles = resources.m_character_styles;
  if(!text.m_runs.empty() && !text.m_runs.front().m_character_style.is_nil()) {
    for(auto& style : styles) {
      if(style.m_id == text.m_runs.front().m_character_style) {
        return &style;
      }
    }
  }
  for(auto& style : styles) {
    if(is_same_style(style, text)) {
      return &style;
    }
  }
  return nullptr;
}

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/TextCommands.hpp"

using namespace Rellu;

namespace {
  const auto FIRST_KEY = std::string("a");
  const auto LAST_KEY = std::string("{");
  const auto START_ORDER = std::string("n");
  const auto NEW_GROUP_NAME = std::string("New group");
  constexpr auto MAXIMUM_DEPTH = 256;

  class AddAssetsCommand : public Command {
    public:
      AddAssetsCommand(std::string label, std::vector<ColorSwatch> swatches,
        std::vector<CharacterStyle> styles) noexcept
        : m_label(std::move(label)),
          m_swatches(std::move(swatches)),
          m_styles(std::move(styles)) {}

      std::string get_label() const override {
        return m_label;
      }

      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::string m_label;
      std::vector<ColorSwatch> m_swatches;
      std::vector<CharacterStyle> m_styles;
  };

  std::string get_key(const std::string& key, const std::string& fallback) {
    return std::clamp(key.empty() ? fallback : key, FIRST_KEY, LAST_KEY);
  }

  std::string get_order_between(std::string lower, std::string upper) {
    lower = get_key(lower, FIRST_KEY);
    upper = get_key(upper, LAST_KEY);
    if(lower == upper) {
      return lower;
    } else if(upper < lower) {
      std::swap(lower, upper);
    }
    for(auto i = std::size_t(0); i != lower.size(); ++i) {
      auto high = i < upper.size() ? upper[i] : LAST_KEY.front();
      if(lower[i] < high) {
        auto middle = static_cast<char>(
          std::floor((lower[i] + high) / 2.0 + 0.5));
        if(middle == high) {
          return lower.substr(0, i + 1) +
            get_order_between(lower.substr(i + 1), std::string());
        }
        return lower.substr(0, i) + middle;
      }
    }
    return lower +
      get_order_between(FIRST_KEY, upper.substr(lower.size()));
  }

  const std::vector<AssetPlacement>* find_placements(
      const Resources& resources, const NodeId& asset) {
    for(auto& swatch : resources.m_swatches) {
      if(swatch.m_id == asset) {
        return &swatch.m_placements;
      }
    }
    for(auto& style : resources.m_character_styles) {
      if(style.m_id == asset) {
        return &style.m_placements;
      }
    }
    return nullptr;
  }

  const AssetGroup* find_group(
      const Resources& resources, const std::string& id) {
    for(auto& group : resources.m_asset_groups) {
      if(group.m_id == id) {
        return &group;
      }
    }
    return nullptr;
  }

  std::string find_root(const Resources& resources, std::string group) {
    for(auto depth = 0; depth != MAXIMUM_DEPTH; ++depth) {
      auto entry = find_group(resources, group);
      if(!entry) {
        return group;
      }
      group = entry->m_parent;
    }
    return group;
  }

  std::optional<std::string> find_order(
      const std::vector<AssetPlacement>& placements, const std::string& group) {
    for(auto& placement : placements) {
      if(placement.m_group == group) {
        return placement.m_order;
      }
    }
    return std::nullopt;
  }

  std::vector<NodeId> get_section_assets(
      const Resources& resources, const std::string& section) {
    auto assets = std::vector<NodeId>();
    if(section == COLOR_SECTION) {
      for(auto& swatch : resources.m_swatches) {
        assets.push_back(swatch.m_id);
      }
    } else if(section == CHARACTER_STYLE_SECTION) {
      for(auto& style : resources.m_character_styles) {
        assets.push_back(style.m_id);
      }
    }
    return assets;
  }

  std::string find_first_order(const Resources& resources,
      const std::string& group, const std::vector<NodeId>& excluded) {
    auto first = START_ORDER;
    for(auto& entry : list_assets(resources, group)) {
      auto order = std::optional<std::string>();
      if(!entry.m_group.empty()) {
        order = find_group(resources, entry.m_group)->m_order;
      } else if(std::find(excluded.begin(), excluded.end(), entry.m_asset) ==
          excluded.end()) {
        order = find_order(*find_placements(resources, entry.m_asset), group);
      }
      if(order && *order < first) {
        first = *order;
      }
    }
    return first;
  }

  void set_placements(const Document& document, Transaction& transaction,
      const NodeId& asset, std::vector<AssetPlacement> placements) {
    auto& resources = document.get_resources();
    for(auto& swatch : resources.m_swatches) {
      if(swatch.m_id == asset) {
        auto replacement = swatch;
        replacement.m_placements = std::move(placements);
        if(replacement != swatch) {
          transaction.replace_swatch(std::move(replacement));
        }
        return;
      }
    }
    for(auto& style : resources.m_character_styles) {
      if(style.m_id == asset) {
        auto replacement = style;
        replacement.m_placements = std::move(placements);
        if(!(replacement == style)) {
          transaction.replace_character_style(std::move(replacement));
        }
        return;
      }
    }
  }

  void order_new_assets(const Document& document, Transaction& transaction,
      const std::string& section) {
    auto& resources = document.get_resources();
    auto assets = std::vector<NodeId>();
    for(auto& asset : get_section_assets(resources, section)) {
      if(find_placements(resources, asset)->empty()) {
        assets.push_back(asset);
      }
    }
    if(assets.empty()) {
      return;
    }
    auto keys = make_orders_between(assets.size(), std::string(),
      find_first_order(resources, section, assets));
    for(auto i = std::size_t(0); i != assets.size(); ++i) {
      set_placements(document, transaction, assets[i],
        {AssetPlacement(section, keys[keys.size() - 1 - i])});
    }
  }

  void remove_from_groups(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& assets) {
    auto groups = document.get_resources().m_asset_groups;
    for(auto& group : groups) {
      std::erase_if(group.m_elements, [&] (const auto& element) {
        return std::any_of(assets.begin(), assets.end(),
          [&] (const auto& asset) {
            return to_string(asset) == element;
          });
      });
    }
    if(groups != document.get_resources().m_asset_groups) {
      transaction.set_asset_groups(std::move(groups));
    }
  }

  std::string check_assets(
      const Resources& resources, const std::vector<NodeId>& assets) {
    if(assets.empty()) {
      throw std::invalid_argument("No assets.");
    }
    auto section = find_section(resources, assets.front());
    if(section.empty() || std::any_of(assets.begin(), assets.end(),
        [&] (const auto& asset) {
          return find_section(resources, asset) != section;
        })) {
      throw std::invalid_argument("Assets of different sections.");
    }
    return section;
  }

  void collect_visible(const Document& document, const NodeId& id,
      std::vector<const Node*>& nodes) {
    auto node = document.find(id);
    if(!node || !node->m_header.m_is_visible) {
      return;
    }
    nodes.push_back(node);
    for(auto& child : node->m_children) {
      collect_visible(document, child, nodes);
    }
  }

  std::vector<const Node*> get_visible_nodes(const Document& document) {
    auto nodes = std::vector<const Node*>();
    for(auto& id : document.get_canvas()) {
      collect_visible(document, id, nodes);
    }
    return nodes;
  }

  bool is_painted(const Fill& fill, const ColorSwatch& swatch) {
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      return solid->m_swatch == swatch.m_id ||
        (swatch.m_gradient.empty() && solid->m_color == swatch.m_color);
    } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
      return !swatch.m_gradient.empty() &&
        gradient->m_stops == swatch.m_gradient;
    }
    return false;
  }

  bool is_painted(const Style& style, const ColorSwatch& swatch) {
    return is_painted(style.m_fill, swatch) || (swatch.m_gradient.empty() &&
      style.m_stroke && style.m_stroke->m_is_enabled &&
      style.m_stroke->m_color == swatch.m_color);
  }

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
  return "Add Color to Assets";
}

void AddSwatchCommand::apply(
    const Document& document, Transaction& transaction) {
  transaction.add_swatch(m_swatch);
  order_new_assets(document, transaction, COLOR_SECTION);
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
  return "Rename Color";
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
  return "Delete Color";
}

void RemoveSwatchCommand::apply(
    const Document& document, Transaction& transaction) {
  if(!find_swatch_by_id(document, m_id)) {
    throw std::invalid_argument("Unknown swatch.");
  }
  relink_fills(document, transaction, m_id, nullptr);
  remove_from_groups(document, transaction, {m_id});
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
  return "Add Character Style to Assets";
}

void AddCharacterStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  transaction.add_character_style(m_style);
  order_new_assets(document, transaction, CHARACTER_STYLE_SECTION);
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
  return "Rename Character Style";
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
  return "Delete Character Style";
}

void RemoveCharacterStyleCommand::apply(
    const Document& document, Transaction& transaction) {
  if(!find_style(document, m_id)) {
    throw std::invalid_argument("Unknown character style.");
  }
  relink_text(document, transaction, m_id, nullptr);
  remove_from_groups(document, transaction, {m_id});
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
  auto label = swatches.size() == 1 ? std::string("Add Color to Assets") :
    std::string("Add Colors to Assets");
  for(auto& swatch : swatches) {
    swatch.m_id = generate_node_id();
  }
  return std::make_unique<AddAssetsCommand>(std::move(label),
    std::move(swatches), std::vector<CharacterStyle>());
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
  auto label = styles.size() == 1 ?
    std::string("Add Character Style to Assets") :
    std::string("Add Character Styles to Assets");
  for(auto& style : styles) {
    style.m_id = generate_node_id();
  }
  return std::make_unique<AddAssetsCommand>(std::move(label),
    std::vector<ColorSwatch>(), std::move(styles));
}

void AddAssetsCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& swatch : m_swatches) {
    transaction.add_swatch(swatch);
  }
  for(auto& style : m_styles) {
    transaction.add_character_style(style);
  }
  if(!m_swatches.empty()) {
    order_new_assets(document, transaction, COLOR_SECTION);
  }
  if(!m_styles.empty()) {
    order_new_assets(document, transaction, CHARACTER_STYLE_SECTION);
  }
}

std::string Rellu::make_order_between(
    const std::string& lower, const std::string& upper) {
  return get_order_between(lower, upper);
}

std::vector<std::string> Rellu::make_orders_between(
    std::size_t count, const std::string& lower, const std::string& upper) {
  if(count == 0) {
    return {};
  }
  auto middle = get_order_between(lower, upper);
  auto left = count / 2;
  auto orders = make_orders_between(left, lower, middle);
  orders.push_back(middle);
  auto right = make_orders_between(count - left - 1, middle, upper);
  orders.insert(orders.end(), right.begin(), right.end());
  return orders;
}

std::string Rellu::find_section(
    const Resources& resources, const NodeId& asset) {
  for(auto& swatch : resources.m_swatches) {
    if(swatch.m_id == asset) {
      return COLOR_SECTION;
    }
  }
  for(auto& style : resources.m_character_styles) {
    if(style.m_id == asset) {
      return CHARACTER_STYLE_SECTION;
    }
  }
  return std::string();
}

std::string Rellu::find_asset_group(
    const Resources& resources, const NodeId& asset) {
  auto section = find_section(resources, asset);
  if(section.empty()) {
    return section;
  }
  for(auto& placement : *find_placements(resources, asset)) {
    if(placement.m_group == section ||
        find_group(resources, placement.m_group)) {
      return placement.m_group;
    }
  }
  return section;
}

std::vector<AssetEntry> Rellu::list_assets(
    const Resources& resources, const std::string& group) {
  struct Keyed {
    std::string m_order;
    AssetEntry m_entry;
  };
  auto keyed = std::vector<Keyed>();
  for(auto& entry : resources.m_asset_groups) {
    if(entry.m_parent == group) {
      keyed.push_back(Keyed(entry.m_order, AssetEntry(entry.m_id, NodeId())));
    }
  }
  auto entries = std::vector<AssetEntry>();
  for(auto& asset :
      get_section_assets(resources, find_root(resources, group))) {
    if(find_asset_group(resources, asset) != group) {
      continue;
    }
    if(auto order = find_order(*find_placements(resources, asset), group)) {
      keyed.push_back(Keyed(*order, AssetEntry(std::string(), asset)));
    } else {
      entries.insert(entries.begin(), AssetEntry(std::string(), asset));
    }
  }
  std::stable_sort(keyed.begin(), keyed.end(),
    [] (const auto& left, const auto& right) {
      return left.m_order < right.m_order;
    });
  for(auto& entry : keyed) {
    entries.push_back(std::move(entry.m_entry));
  }
  return entries;
}

std::vector<NodeId> Rellu::find_swatch_instances(
    const Document& document, const ColorSwatch& swatch) {
  auto instances = std::vector<NodeId>();
  for(auto node : get_visible_nodes(document)) {
    auto is_instance = false;
    if(auto shape = node->get_shape()) {
      is_instance = is_painted(shape->m_style, swatch);
    } else if(auto text = node->get_text()) {
      is_instance = is_painted(text->m_style, swatch) ||
        (swatch.m_gradient.empty() && std::any_of(text->m_runs.begin(),
          text->m_runs.end(), [&] (const auto& run) {
            return run.m_style.m_color == swatch.m_color;
          }));
    } else if(auto artboard = node->get_artboard()) {
      is_instance = is_painted(artboard->m_background, swatch);
    }
    if(is_instance) {
      instances.push_back(node->get_id());
    }
  }
  return instances;
}

std::vector<NodeId> Rellu::find_character_style_instances(
    const Document& document, const CharacterStyle& style) {
  auto instances = std::vector<NodeId>();
  for(auto node : get_visible_nodes(document)) {
    auto text = node->get_text();
    if(!text) {
      continue;
    }
    if(is_same_style(style, *text) || std::any_of(text->m_runs.begin(),
        text->m_runs.end(), [&] (const auto& run) {
          return run.m_character_style == style.m_id;
        })) {
      instances.push_back(node->get_id());
    }
  }
  return instances;
}

GroupAssetsCommand::GroupAssetsCommand(std::vector<NodeId> assets) noexcept
  : m_assets(std::move(assets)),
    m_group(to_string(generate_node_id())) {}

const std::string& GroupAssetsCommand::get_group() const {
  return m_group;
}

std::string GroupAssetsCommand::get_label() const {
  return "Create Asset Group";
}

void GroupAssetsCommand::apply(
    const Document& document, Transaction& transaction) {
  auto& resources = document.get_resources();
  check_assets(resources, m_assets);
  auto parent = find_asset_group(resources, m_assets.front());
  auto order = find_order(*find_placements(resources, m_assets.front()),
    parent).value_or(make_order_between(std::string(),
      find_first_order(resources, parent, {})));
  auto group = AssetGroup();
  group.m_id = m_group;
  group.m_name = NEW_GROUP_NAME;
  group.m_order = order;
  group.m_parent = parent;
  for(auto& asset : m_assets) {
    group.m_elements.push_back(to_string(asset));
  }
  auto groups = resources.m_asset_groups;
  for(auto& entry : groups) {
    std::erase_if(entry.m_elements, [&] (const auto& element) {
      return std::find(group.m_elements.begin(), group.m_elements.end(),
        element) != group.m_elements.end();
    });
    if(entry.m_id == parent) {
      entry.m_children.push_back(m_group);
    }
  }
  groups.push_back(std::move(group));
  transaction.set_asset_groups(std::move(groups));
  auto keys =
    make_orders_between(m_assets.size(), std::string(), std::string());
  for(auto i = std::size_t(0); i != m_assets.size(); ++i) {
    set_placements(document, transaction, m_assets[i],
      {AssetPlacement(m_group, keys[i])});
  }
}

MoveAssetsCommand::MoveAssetsCommand(
  std::vector<NodeId> assets, std::string group) noexcept
  : m_assets(std::move(assets)),
    m_group(std::move(group)) {}

std::string MoveAssetsCommand::get_label() const {
  return "Set Order in Group";
}

void MoveAssetsCommand::apply(
    const Document& document, Transaction& transaction) {
  auto& resources = document.get_resources();
  auto section = check_assets(resources, m_assets);
  if(find_root(resources, m_group) != section) {
    throw std::invalid_argument("Group of another section.");
  }
  auto keys = make_orders_between(m_assets.size(), std::string(),
    find_first_order(resources, m_group, m_assets));
  auto groups = resources.m_asset_groups;
  for(auto& entry : groups) {
    std::erase_if(entry.m_elements, [&] (const auto& element) {
      return std::any_of(m_assets.begin(), m_assets.end(),
        [&] (const auto& asset) {
          return to_string(asset) == element;
        });
    });
    if(entry.m_id == m_group) {
      for(auto& asset : m_assets) {
        entry.m_elements.push_back(to_string(asset));
      }
    }
  }
  if(groups != resources.m_asset_groups) {
    transaction.set_asset_groups(std::move(groups));
  }
  for(auto i = std::size_t(0); i != m_assets.size(); ++i) {
    set_placements(document, transaction, m_assets[i],
      {AssetPlacement(m_group, keys[i])});
  }
}

AddAssetGroupCommand::AddAssetGroupCommand(std::string parent) noexcept
  : m_parent(std::move(parent)),
    m_group(to_string(generate_node_id())) {}

const std::string& AddAssetGroupCommand::get_group() const {
  return m_group;
}

std::string AddAssetGroupCommand::get_label() const {
  return "Create Asset Group";
}

void AddAssetGroupCommand::apply(
    const Document& document, Transaction& transaction) {
  auto& resources = document.get_resources();
  if(!find_group(resources, m_parent)) {
    throw std::invalid_argument("Unknown asset group.");
  }
  auto groups = resources.m_asset_groups;
  auto group = AssetGroup();
  group.m_id = m_group;
  group.m_name = NEW_GROUP_NAME;
  group.m_order = make_order_between(
    std::string(), find_first_order(resources, m_parent, {}));
  group.m_parent = m_parent;
  for(auto& entry : groups) {
    if(entry.m_id == m_parent) {
      entry.m_children.push_back(m_group);
    }
  }
  groups.push_back(std::move(group));
  transaction.set_asset_groups(std::move(groups));
}

RemoveAssetGroupCommand::RemoveAssetGroupCommand(
  std::string group, bool is_content_deleted) noexcept
  : m_group(std::move(group)),
    m_is_content_deleted(is_content_deleted) {}

std::string RemoveAssetGroupCommand::get_label() const {
  return "Delete Asset Group";
}

void RemoveAssetGroupCommand::apply(
    const Document& document, Transaction& transaction) {
  auto& resources = document.get_resources();
  auto group = find_group(resources, m_group);
  if(!group) {
    throw std::invalid_argument("Unknown asset group.");
  }
  auto parent = group->m_parent;
  auto removed = std::vector<std::string>({m_group});
  if(m_is_content_deleted) {
    for(auto i = std::size_t(0); i != removed.size(); ++i) {
      for(auto& entry : resources.m_asset_groups) {
        if(entry.m_parent == removed[i]) {
          removed.push_back(entry.m_id);
        }
      }
    }
  }
  auto is_removed = [&] (const std::string& id) {
    return std::find(removed.begin(), removed.end(), id) != removed.end();
  };
  auto assets = std::vector<NodeId>();
  for(auto section : {COLOR_SECTION, CHARACTER_STYLE_SECTION}) {
    for(auto& asset : get_section_assets(resources, section)) {
      if(is_removed(find_asset_group(resources, asset))) {
        assets.push_back(asset);
      }
    }
  }
  auto groups = resources.m_asset_groups;
  std::erase_if(groups, [&] (const auto& entry) {
    return is_removed(entry.m_id);
  });
  for(auto& entry : groups) {
    std::erase_if(entry.m_children, is_removed);
    if(!m_is_content_deleted && entry.m_parent == m_group) {
      entry.m_parent = parent;
    }
  }
  if(!m_is_content_deleted) {
    for(auto& entry : groups) {
      if(entry.m_id != parent) {
        continue;
      }
      for(auto& child : group->m_children) {
        entry.m_children.push_back(child);
      }
      for(auto& element : group->m_elements) {
        entry.m_elements.push_back(element);
      }
    }
  }
  transaction.set_asset_groups(std::move(groups));
  for(auto& asset : assets) {
    if(m_is_content_deleted) {
      if(find_section(resources, asset) == COLOR_SECTION) {
        relink_fills(document, transaction, asset, nullptr);
        transaction.remove_swatch(asset);
      } else {
        relink_text(document, transaction, asset, nullptr);
        transaction.remove_character_style(asset);
      }
      continue;
    }
    auto placements = *find_placements(resources, asset);
    for(auto& placement : placements) {
      if(placement.m_group == m_group) {
        placement.m_group = parent;
      }
    }
    set_placements(document, transaction, asset, std::move(placements));
  }
}

RenameAssetGroupCommand::RenameAssetGroupCommand(
  std::string group, std::string name) noexcept
  : m_group(std::move(group)),
    m_name(std::move(name)) {}

std::string RenameAssetGroupCommand::get_label() const {
  return "Rename Asset Group";
}

void RenameAssetGroupCommand::apply(
    const Document& document, Transaction& transaction) {
  auto groups = document.get_resources().m_asset_groups;
  auto group = std::find_if(groups.begin(), groups.end(),
    [&] (const auto& group) {
      return group.m_id == m_group;
    });
  if(group == groups.end()) {
    throw std::invalid_argument("Unknown asset group.");
  }
  if(group->m_name == m_name) {
    return;
  }
  group->m_name = m_name;
  transaction.set_asset_groups(std::move(groups));
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

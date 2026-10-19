#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include "Rellu/Commands/CopyNames.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Localization/Localization.hpp"

using namespace Rellu;

namespace {
  constexpr auto SEPARATOR = std::string_view(" \xE2\x80\x93 ");

  bool is_digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
      [] (const auto& c) { return c >= '0' && c <= '9'; });
  }

  bool is_numbered(std::string_view name, std::string_view prefix) {
    return name.starts_with(prefix) && is_digits(name.substr(prefix.size()));
  }

  std::optional<std::pair<std::string_view, long long>> split_number(
      std::string_view name) {
    for(auto position = std::size_t(0); position < name.size(); ++position) {
      auto rest = name.substr(position);
      auto digits = std::string_view();
      if(rest.starts_with(SEPARATOR) &&
          is_digits(rest.substr(SEPARATOR.size()))) {
        digits = rest.substr(SEPARATOR.size());
      } else if(rest.starts_with(' ') && is_digits(rest.substr(1))) {
        digits = rest.substr(1);
      } else {
        continue;
      }
      auto number = 0LL;
      auto result =
        std::from_chars(digits.data(), digits.data() + digits.size(), number);
      if(result.ec != std::errc()) {
        return std::nullopt;
      }
      return std::pair(name.substr(0, position), number);
    }
    return std::nullopt;
  }

  bool is_plain_group(const Group& group) {
    return !group.m_repeat_grid && !group.m_scroll_group && !group.m_mask &&
      !group.m_component_instance;
  }

  std::vector<std::string> get_kinds(const Node& node) {
    if(auto shape = node.get_shape()) {
      auto& geometry = shape->m_geometry;
      if(std::holds_alternative<RectangleShape>(geometry)) {
        return {"Rectangle", "Image"};
      } else if(std::holds_alternative<EllipseShape>(geometry)) {
        return {"Ellipse"};
      } else if(std::holds_alternative<PolygonShape>(geometry)) {
        return {"Polygon"};
      } else if(std::holds_alternative<LineShape>(geometry)) {
        return {"Line"};
      } else if(auto compound = std::get_if<CompoundShape>(&geometry)) {
        auto prefix = get_name_prefix(compound->m_operation);
        prefix.pop_back();
        return {prefix};
      }
      return {"Path"};
    } else if(auto group = node.get_group()) {
      if(group->m_repeat_grid) {
        return {"Repeat Grid"};
      } else if(group->m_scroll_group) {
        return {"Scroll Group"};
      } else if(group->m_mask) {
        return {"Mask Group"};
      }
      return {"Group"};
    }
    return {};
  }

  bool is_numbered_kind(std::string_view name, std::string_view kind) {
    return std::any_of(LANGUAGES.begin(), LANGUAGES.end(),
      [&] (const auto& language) {
        return is_numbered(name,
          std::string(localize("name", kind, language)) + ' ');
      });
  }

  bool has_custom_name(const Node& node) {
    auto& name = node.m_header.m_name;
    if(node.get_artboard() || node.get_reference()) {
      return true;
    } else if(name.empty()) {
      return false;
    } else if(auto text = node.get_text()) {
      return name != make_text_name(*text->m_content);
    }
    auto kinds = get_kinds(node);
    return std::none_of(kinds.begin(), kinds.end(),
      [&] (const auto& kind) { return is_numbered_kind(name, kind); });
  }

  std::string make_name(const Document& document, const Node& node) {
    if(auto text = node.get_text()) {
      return make_text_name(*text->m_content);
    }
    auto kinds = get_kinds(node);
    auto kind = std::find_if(kinds.begin(), kinds.end(),
      [&] (const auto& kind) {
        return is_numbered_kind(node.m_header.m_name, kind);
      });
    if(kind == kinds.end()) {
      kind = kinds.begin();
    }
    return make_node_name(
      document, std::string(localize("name", *kind)) + ' ');
  }

  std::string make_instance_copy_name(
      const Document& document, const std::string& component) {
    auto base = std::string_view(component);
    if(base.find(SEPARATOR) != std::string_view::npos) {
      if(auto split = split_number(base)) {
        base = split->first;
      }
    }
    auto highest = 0LL;
    document.get_nodes().for_each([&] (const Node& node) {
      auto group = node.get_group();
      if(!group || !group->m_component_instance) {
        return;
      }
      if(auto split = split_number(node.m_header.m_name);
          split && split->first == base) {
        highest = std::max(highest, split->second);
      }
    });
    return std::string(base) + std::string(SEPARATOR) +
      std::to_string(highest + 1);
  }

  bool has_instance_name(const std::string& name, const std::string& base) {
    if(name.empty() || name == base) {
      return true;
    }
    auto split = split_number(name);
    return split && split->first == base;
  }

  void rename(const Document& document, Transaction& transaction,
      const NodeId& id, std::string name) {
    auto& node = document.get(id);
    if(node.m_header.m_name == name) {
      return;
    }
    auto header = node.m_header;
    header.m_name = std::move(name);
    transaction.replace_header(std::move(header));
  }

  void name_instance(const Document& document, Transaction& transaction,
      const Node& node, const ComponentInstance& instance) {
    auto& components = document.get_resources().m_components;
    auto component = components.find(instance.m_component);
    if(component == components.end() ||
        !has_instance_name(node.m_header.m_name, component->second.m_name)) {
      return;
    }
    rename(document, transaction, node.get_id(),
      make_instance_copy_name(document, component->second.m_name));
  }

  bool name_subtree(const Document& document, Transaction& transaction,
      const NodeId& id, bool is_kept) {
    auto& node = document.get(id);
    if(auto group = node.get_group(); group && group->m_component_instance) {
      name_instance(document, transaction, node, *group->m_component_instance);
      return true;
    }
    auto children = node.m_children;
    for(auto& child : children) {
      name_subtree(document, transaction, child, is_kept);
    }
    auto& current = document.get(id);
    if(has_custom_name(current) ||
        (is_kept && !current.m_header.m_name.empty())) {
      return false;
    }
    rename(document, transaction, id, make_name(document, current));
    return true;
  }
}

void Rellu::name_copy(const Document& document, Transaction& transaction,
    const NodeId& id, bool is_kept) {
  auto& node = document.get(id);
  auto group = node.get_group();
  auto is_subtree_kept = is_kept || node.get_artboard() ||
    (group && (group->m_repeat_grid || is_plain_group(*group)));
  if(!name_subtree(document, transaction, id, is_subtree_kept) && !is_kept &&
      !has_custom_name(document.get(id))) {
    rename(document, transaction, id, make_name(document, document.get(id)));
  }
}

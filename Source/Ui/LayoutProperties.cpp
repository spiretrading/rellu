#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StackCommands.hpp"
#include "Rellu/Ui/LayoutProperties.hpp"

using namespace Rellu;

namespace {
  const auto ORIENTATIONS = QStringList({"Horizontal", "Vertical"});
  constexpr auto MAXIMUM_SPACING = 10000.0;

  PropertyDescriptor& add(PropertyDescriptor& parent, const std::string& name,
      QString label, PropertyValue value = PropertyValue()) {
    parent.m_children.push_back(PropertyDescriptor(parent.m_path + '.' + name,
      std::move(label), std::move(value), std::vector<PropertyDescriptor>()));
    return parent.m_children.back();
  }

  std::optional<double> get_number(const PropertyValue& value) {
    if(auto number = std::get_if<double>(&value);
        number && std::isfinite(*number)) {
      return *number;
    } else if(auto integer = std::get_if<int>(&value)) {
      return static_cast<double>(*integer);
    }
    return std::nullopt;
  }

  struct ItemSpacing {
    NodeId m_stack;
    std::vector<NodeId> m_before;
    std::vector<NodeId> m_after;
  };

  std::optional<ItemSpacing> get_item_spacing(const Document& document,
      const std::vector<NodeId>& nodes, const TextMeasurer& measurer) {
    if(nodes.empty()) {
      return std::nullopt;
    }
    auto node = document.find(nodes.front());
    auto parent = node && !node->m_parent.is_nil() ?
      document.find(node->m_parent) : nullptr;
    auto group = parent ? parent->get_group() : nullptr;
    if(!group || !group->m_stack) {
      return std::nullopt;
    }
    auto items = get_stack_items(document, parent->get_id());
    std::erase_if(items, [&] (const auto& item) {
      return !document.get(item).m_header.m_is_visible ||
        !get_local_bounds(document, item, BoundsType::GEOMETRY, measurer);
    });
    auto is_selected = [&] (const NodeId& id) {
      return std::find(nodes.begin(), nodes.end(), id) != nodes.end();
    };
    if(items.size() < 2 ||
        std::none_of(items.begin(), items.end(), is_selected)) {
      return std::nullopt;
    }
    auto spacing = ItemSpacing(parent->get_id(), {}, {});
    for(auto i = std::size_t(1); i < items.size(); ++i) {
      if(is_selected(items[i])) {
        spacing.m_before.push_back(items[i - 1]);
      }
      if(is_selected(items[i - 1])) {
        spacing.m_after.push_back(items[i - 1]);
      }
    }
    return spacing;
  }

  PropertySetter make_item_spacing_setter(
      bool is_before, TextMeasurer measurer) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      auto spacing = get_item_spacing(document, nodes, measurer);
      if(!number || !spacing) {
        return nullptr;
      }
      auto& items = is_before ? spacing->m_before : spacing->m_after;
      if(items.empty()) {
        return nullptr;
      }
      auto commands = std::vector<std::unique_ptr<Command>>();
      for(auto& item : items) {
        commands.push_back(std::make_unique<SetStackSpacingCommand>(
          spacing->m_stack, item, *number, measurer));
      }
      return std::make_unique<CompositeCommand>(is_before ?
        "Spacing Before Value" : "Spacing After Value", std::move(commands));
    };
  }

  void add_padding(PropertyDescriptor& section, const Margins* padding) {
    auto& property =
      add(section, "padding", "Padding", static_cast<bool>(padding));
    if(!padding) {
      return;
    }
    add(property, "top", "Top", padding->m_top);
    add(property, "right", "Right", padding->m_right);
    add(property, "bottom", "Bottom", padding->m_bottom);
    add(property, "left", "Left", padding->m_left);
  }

  void add_stack(PropertyDescriptor& section, const Stack* stack) {
    auto& property =
      add(section, "stack", "Stack", static_cast<bool>(stack));
    if(!stack) {
      return;
    }
    add(property, "orientation", "Direction",
      Choice(static_cast<int>(stack->m_orientation), ORIENTATIONS));
    auto spacing = std::optional<double>();
    auto is_mixed = false;
    for(auto& entry : stack->m_spacing) {
      if(spacing && *spacing != entry.second) {
        is_mixed = true;
      }
      spacing = spacing.value_or(entry.second);
    }
    add(property, "spacing", "Spacing", spacing.value_or(0)).m_is_mixed =
      is_mixed;
  }

  PropertySetter make_padding_side_setter(
      PaddingSide side, TextMeasurer measurer) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      if(!number) {
        return nullptr;
      }
      return std::make_unique<SetPaddingValueCommand>(
        nodes, side, *number, measurer);
    };
  }

  PropertySetter make_grid_spacing_setter(Orientation orientation) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      if(!number) {
        return nullptr;
      }
      return std::make_unique<SetRepeatGridSpacingCommand>(
        nodes, orientation, *number);
    };
  }

  void wrap_stack_position(PropertyEditors& editors, const std::string& path,
      Orientation orientation) {
    auto editor = editors.find(path);
    if(!editor) {
      return;
    }
    auto wrapped = *editor;
    auto setter = editor->m_setter;
    wrapped.m_setter = [=] (const Document& document,
        const std::vector<NodeId>& nodes, const PropertyValue& value) ->
        std::unique_ptr<Command> {
      if(!setter || !nodes.empty() && std::all_of(nodes.begin(), nodes.end(),
          [&] (const auto& id) {
            return get_stack_direction(document, id) == orientation;
          })) {
        return nullptr;
      }
      return setter(document, nodes, value);
    };
    editors.add(path, std::move(wrapped));
  }

  void wrap_grid_size(
      PropertyEditors& editors, const std::string& path, bool is_width) {
    auto editor = editors.find(path);
    if(!editor) {
      return;
    }
    auto wrapped = *editor;
    auto setter = editor->m_setter;
    wrapped.m_setter = [=] (const Document& document,
        const std::vector<NodeId>& nodes, const PropertyValue& value) ->
        std::unique_ptr<Command> {
      auto commands = std::vector<std::unique_ptr<Command>>();
      auto others = std::vector<NodeId>();
      auto number = get_number(value);
      for(auto& id : nodes) {
        if(!is_repeat_grid(document, id)) {
          others.push_back(id);
        } else if(number) {
          auto size = document.get(id).get_group()->m_repeat_grid->m_size;
          if(is_width) {
            size.m_width = *number;
          } else {
            size.m_height = *number;
          }
          commands.push_back(
            std::make_unique<ResizeRepeatGridCommand>(id, size));
        }
      }
      if(!others.empty() && setter) {
        if(auto command = setter(document, others, value)) {
          commands.push_back(std::move(command));
        }
      }
      if(commands.empty()) {
        return nullptr;
      } else if(commands.size() == 1) {
        return std::move(commands.front());
      }
      return std::make_unique<CompositeCommand>(
        "Resize", std::move(commands));
    };
    editors.add(path, std::move(wrapped));
  }
}

void Rellu::add_layout_properties(
    const Node& node, PropertyDescriptor& section) {
  auto group = node.get_group();
  if(!group) {
    return;
  }
  if(auto& grid = group->m_repeat_grid) {
    auto& property = add(section, "repeat_grid", "Repeat Grid");
    add(property, "column_spacing", "Column Spacing", grid->m_column_spacing);
    add(property, "row_spacing", "Row Spacing", grid->m_row_spacing);
    return;
  }
  auto padding = static_cast<const Margins*>(nullptr);
  if(group->m_stack && group->m_stack->m_has_background) {
    padding = &group->m_stack->m_padding;
  } else if(!group->m_stack && group->m_padding &&
      group->m_padding->m_has_background) {
    padding = &group->m_padding->m_padding;
  }
  add_padding(section, padding);
  add_stack(section, group->m_stack ? &*group->m_stack : nullptr);
}

void Rellu::add_item_spacing_properties(const Document& document,
    const std::vector<NodeId>& nodes, const TextMeasurer& measurer,
    PropertyDescriptor& section) {
  auto spacing = get_item_spacing(document, nodes, measurer);
  if(!spacing) {
    return;
  }
  auto& stack = document.get(spacing->m_stack);
  auto& layout = *stack.get_group()->m_stack;
  auto add_spacing = [&] (const char* name, const char* label,
      const std::vector<NodeId>& items) {
    if(items.empty()) {
      return;
    }
    auto value = get_stack_margin(document, layout, items.front());
    auto& property = add(section, name, label, value);
    property.m_is_mixed = std::any_of(items.begin(), items.end(),
      [&] (const auto& item) {
        return get_stack_margin(document, layout, item) != value;
      });
    property.m_is_read_only = stack.m_header.m_is_locked;
  };
  add_spacing("spacing_before", "Spacing Before Value", spacing->m_before);
  add_spacing("spacing_after", "Spacing After Value", spacing->m_after);
}

void Rellu::add_layout_property_editors(
    PropertyEditors& editors, TextMeasurer measurer) {
  editors.add("layout.spacing_before", PropertyEditor(
    make_item_spacing_setter(true, measurer), 0.0, MAXIMUM_SPACING));
  editors.add("layout.spacing_after", PropertyEditor(
    make_item_spacing_setter(false, measurer), 0.0, MAXIMUM_SPACING));
  editors.add("layout.padding", PropertyEditor([=] (const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) ->
      std::unique_ptr<Command> {
    auto is_enabled = std::get_if<bool>(&value);
    if(!is_enabled) {
      return nullptr;
    }
    return std::make_unique<SetPaddingCommand>(nodes, *is_enabled, measurer);
  }));
  editors.add("layout.padding.top", PropertyEditor(
    make_padding_side_setter(PaddingSide::TOP, measurer), 0.0));
  editors.add("layout.padding.right", PropertyEditor(
    make_padding_side_setter(PaddingSide::RIGHT, measurer), 0.0));
  editors.add("layout.padding.bottom", PropertyEditor(
    make_padding_side_setter(PaddingSide::BOTTOM, measurer), 0.0));
  editors.add("layout.padding.left", PropertyEditor(
    make_padding_side_setter(PaddingSide::LEFT, measurer), 0.0));
  editors.add("layout.stack", PropertyEditor([=] (const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) ->
      std::unique_ptr<Command> {
    auto is_enabled = std::get_if<bool>(&value);
    if(!is_enabled || nodes.empty()) {
      return nullptr;
    }
    auto orientation = std::optional<Orientation>();
    if(*is_enabled) {
      orientation =
        get_natural_orientation(document, nodes.front(), measurer);
    }
    return std::make_unique<SetStackCommand>(nodes, orientation, measurer);
  }));
  editors.add("layout.stack.orientation", PropertyEditor(
    [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto choice = std::get_if<Choice>(&value);
      if(!choice || choice->m_index < 0 ||
          choice->m_index >= ORIENTATIONS.size()) {
        return nullptr;
      }
      return std::make_unique<SetStackCommand>(nodes,
        static_cast<Orientation>(choice->m_index), measurer);
    }));
  editors.add("layout.stack.spacing", PropertyEditor(
    [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      if(!number) {
        return nullptr;
      }
      auto commands = std::vector<std::unique_ptr<Command>>();
      commands.push_back(
        std::make_unique<SetStackSpacingCommand>(nodes, *number, measurer));
      return std::make_unique<CompositeCommand>(
        "All Spacings Value", std::move(commands));
    }));
  editors.add("layout.repeat_grid.column_spacing", PropertyEditor(
    make_grid_spacing_setter(Orientation::HORIZONTAL)));
  editors.add("layout.repeat_grid.row_spacing", PropertyEditor(
    make_grid_spacing_setter(Orientation::VERTICAL)));
  wrap_grid_size(editors, "transform.width", true);
  wrap_grid_size(editors, "transform.height", false);
  wrap_stack_position(editors, "transform.x", Orientation::HORIZONTAL);
  wrap_stack_position(editors, "transform.y", Orientation::VERTICAL);
}

std::optional<Orientation> Rellu::get_stack_direction(
    const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node || node->m_parent.is_nil()) {
    return std::nullopt;
  }
  auto parent = document.find(node->m_parent);
  auto group = parent ? parent->get_group() : nullptr;
  if(!group || !group->m_stack || group->m_stack->m_has_background &&
      parent->m_children.front() == id) {
    return std::nullopt;
  }
  return group->m_stack->m_orientation;
}

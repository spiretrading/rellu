#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StackCommands.hpp"
#include "Rellu/Ui/LayoutProperties.hpp"

using namespace Rellu;

namespace {
  const auto ORIENTATIONS = QStringList({"Horizontal", "Vertical"});

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
      if(nodes.size() == 1 && is_repeat_grid(document, nodes.front())) {
        auto number = get_number(value);
        if(!number) {
          return nullptr;
        }
        auto size =
          document.get(nodes.front()).get_group()->m_repeat_grid->m_size;
        if(is_width) {
          size.m_width = *number;
        } else {
          size.m_height = *number;
        }
        return std::make_unique<ResizeRepeatGridCommand>(nodes.front(), size);
      }
      if(!setter) {
        return nullptr;
      }
      return setter(document, nodes, value);
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

void Rellu::add_layout_property_editors(
    PropertyEditors& editors, TextMeasurer measurer) {
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
      return std::make_unique<SetStackSpacingCommand>(
        nodes, *number, measurer);
    }));
  editors.add("layout.repeat_grid.column_spacing", PropertyEditor(
    make_grid_spacing_setter(Orientation::HORIZONTAL)));
  editors.add("layout.repeat_grid.row_spacing", PropertyEditor(
    make_grid_spacing_setter(Orientation::VERTICAL)));
  wrap_grid_size(editors, "transform.width", true);
  wrap_grid_size(editors, "transform.height", false);
}

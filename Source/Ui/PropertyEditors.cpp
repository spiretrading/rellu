#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>
#include <variant>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/CornerRadiusCommands.hpp"
#include "Rellu/Commands/EffectCommands.hpp"
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PolygonCommands.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/ColorPicker.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_BLUR = 50.0;
  constexpr auto MAXIMUM_BRIGHTNESS = 50.0;
  constexpr auto MAXIMUM_COLUMNS = 100.0;

  class SetBlendModeCommand : public Command {
    public:
      SetBlendModeCommand(
          std::vector<NodeId> nodes, BlendMode blend_mode) noexcept
        : m_nodes(std::move(nodes)),
          m_blend_mode(blend_mode) {}

      std::string get_label() const override {
        return "Change Blend Mode";
      }

      void apply(
          const Document& document, Transaction& transaction) override {
        for(auto& id : m_nodes) {
          edit_rendered_header(document, transaction, id,
            [&] (const auto&, auto& header) {
              header.m_blend_mode = m_blend_mode;
            });
        }
      }

    private:
      std::vector<NodeId> m_nodes;
      BlendMode m_blend_mode;
  };

  std::optional<double> get_number(const PropertyValue& value) {
    if(auto number = std::get_if<double>(&value); number &&
        std::isfinite(*number)) {
      return *number;
    }
    return std::nullopt;
  }

  std::vector<NodeId> get_movable(
      const Document& document, const std::vector<NodeId>& nodes) {
    auto movable = get_top_level(document, nodes);
    std::erase_if(movable, [&] (const auto& id) {
      return document.get(id).m_parent.is_nil() && !document.is_on_canvas(id);
    });
    return movable;
  }

  bool has_artboard(
      const Document& document, const std::vector<NodeId>& nodes) {
    return std::any_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
      return document.get(id).get_artboard();
    });
  }

  double get_rotation(const Document& document, const NodeId& id) {
    auto transform = get_transform(document, id);
    return std::atan2(transform.m_b, transform.m_a) * 180 / std::numbers::pi;
  }

  PropertySetter make_position_setter(bool is_x, TextMeasurer measurer) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      auto targets = get_movable(document, nodes);
      if(!number || targets.empty()) {
        return nullptr;
      }
      auto frame = get_frame(document, nodes, measurer);
      if(!frame) {
        return nullptr;
      }
      auto delta = is_x ? Point(*number - frame->m_x, 0) :
        Point(0, *number - frame->m_y);
      if(nodes.size() == 1 && !document.get(nodes.front()).get_artboard()) {
        auto parent = get_parent_transform(document, nodes.front());
        delta = Point(parent.m_a * delta.m_x + parent.m_c * delta.m_y,
          parent.m_b * delta.m_x + parent.m_d * delta.m_y);
      }
      return std::make_unique<TranslateCommand>(std::move(targets), delta);
    };
  }

  PropertySetter make_size_setter(bool is_width, TextMeasurer measurer) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      auto targets = get_movable(document, nodes);
      if(!number || !(*number > 0) || targets.empty()) {
        return nullptr;
      }
      auto lock = std::optional<Size>();
      if(targets.size() == 1) {
        lock = document.get(targets.front()).m_header.m_aspect_lock;
        if(lock && !(lock->m_width > 0 && lock->m_height > 0)) {
          lock = std::nullopt;
        }
      }
      auto resize = [&] (Rect bounds) {
        if(is_width) {
          bounds.m_width = *number;
          if(lock) {
            bounds.m_height = *number * lock->m_height / lock->m_width;
          }
        } else {
          bounds.m_height = *number;
          if(lock) {
            bounds.m_width = *number * lock->m_width / lock->m_height;
          }
        }
        return bounds;
      };
      if(targets.size() == 1) {
        auto& id = targets.front();
        if(auto artboard = document.get(id).get_artboard()) {
          return make_resize_command(targets, artboard->m_frame,
            resize(artboard->m_frame), measurer);
        }
        auto local =
          get_local_bounds(document, id, BoundsType::GEOMETRY, measurer);
        auto world = get_world_transform(document, id);
        auto inverse = invert(world);
        if(!local || !inverse) {
          return nullptr;
        }
        return std::make_unique<TransformCommand>(std::move(targets),
          world * make_rect_mapping(*local, resize(*local)) * *inverse,
          "Resize", TransformMode::RESIZE, ResizeRule::RESPONSIVE, measurer);
      }
      auto bounds =
        get_world_bounds(document, targets, BoundsType::GEOMETRY, measurer);
      if(!bounds) {
        return nullptr;
      }
      return make_resize_command(
        std::move(targets), *bounds, resize(*bounds), measurer);
    };
  }

  PropertySetter make_rotation_setter(TextMeasurer measurer) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      auto targets = get_movable(document, nodes);
      if(!number || targets.empty() || has_artboard(document, targets)) {
        return nullptr;
      }
      auto rotation = get_rotation(document, targets.front());
      if(std::any_of(targets.begin(), targets.end(), [&] (const auto& id) {
          return get_rotation(document, id) != rotation;
        })) {
        rotation = 0;
      }
      auto center = Point(0, 0);
      if(targets.size() == 1) {
        auto local = get_local_bounds(
          document, targets.front(), BoundsType::GEOMETRY, measurer);
        if(!local) {
          return nullptr;
        }
        center = get_world_transform(document, targets.front()).map(
          local->get_center());
      } else if(auto bounds = get_world_bounds(
          document, targets, BoundsType::GEOMETRY, measurer)) {
        center = bounds->get_center();
      } else {
        return nullptr;
      }
      return make_rotate_command(std::move(targets),
        (*number - rotation) * std::numbers::pi / 180, center);
    };
  }

  const Fill* find_fill(const Node& node) {
    if(auto shape = node.get_shape()) {
      return &shape->m_style.m_fill;
    } else if(auto text = node.get_text()) {
      return &text->m_style.m_fill;
    } else if(auto artboard = node.get_artboard()) {
      return &artboard->m_background;
    }
    return nullptr;
  }

  std::optional<Color> find_fill_color(
      const Document& document, const std::vector<NodeId>& nodes) {
    for(auto& id : nodes) {
      auto fill = find_fill(document.get(id));
      if(!fill) {
        continue;
      }
      if(auto solid = std::get_if<SolidFill>(fill)) {
        return solid->m_color;
      } else if(auto gradient = std::get_if<GradientFill>(fill);
          gradient && !gradient->m_stops.empty()) {
        return gradient->m_stops.front().m_color;
      }
    }
    return std::nullopt;
  }

  std::unique_ptr<Command> set_fill_type(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto choice = std::get_if<Choice>(&value);
    if(!choice || nodes.empty()) {
      return nullptr;
    }
    auto color =
      find_fill_color(document, nodes).value_or(make_color(255, 255, 255));
    if(choice->m_index == 0) {
      return std::make_unique<SetFillCommand>(nodes, NoFill());
    } else if(choice->m_index == 1) {
      return std::make_unique<SetFillCommand>(nodes, SolidFill(color));
    } else if(choice->m_index == 2) {
      auto gradient = GradientFill();
      gradient.m_geometry = LinearGradient(Point(0.5, 0), Point(0.5, 1));
      gradient.m_stops = make_default_stops(color);
      return std::make_unique<SetFillCommand>(nodes, std::move(gradient));
    }
    return nullptr;
  }

  std::unique_ptr<Command> set_fill_enabled(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto is_enabled = std::get_if<bool>(&value);
    if(!is_enabled || nodes.empty()) {
      return nullptr;
    }
    auto color =
      find_fill_color(document, nodes).value_or(make_color(255, 255, 255));
    auto commands = std::vector<std::unique_ptr<Command>>();
    for(auto& id : nodes) {
      auto fill = Fill(NoFill());
      if(auto current = find_fill(document.get(id))) {
        if(!*is_enabled) {
          fill = disable_fill(*current);
        } else {
          fill = enable_fill(*current);
          if(std::holds_alternative<NoFill>(fill)) {
            fill = SolidFill(color);
          }
        }
      } else if(*is_enabled) {
        fill = SolidFill(color);
      }
      commands.push_back(std::make_unique<SetFillCommand>(
        std::vector(1, id), std::move(fill)));
    }
    if(commands.size() == 1) {
      return std::move(commands.front());
    }
    auto label = commands.front()->get_label();
    return std::make_unique<CompositeCommand>(
      std::move(label), std::move(commands));
  }

  std::vector<double> get_dashes(
      const Document& document, const std::vector<NodeId>& nodes) {
    for(auto& id : nodes) {
      auto& node = document.get(id);
      if(auto shape = node.get_shape(); shape && shape->m_style.m_stroke) {
        return shape->m_style.m_stroke->m_dashes;
      } else if(auto text = node.get_text();
          text && text->m_style.m_stroke) {
        return text->m_style.m_stroke->m_dashes;
      }
    }
    return {};
  }

  PropertySetter make_dash_setter(bool is_dash) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto number = get_number(value);
      if(!number || *number < 0 || nodes.empty()) {
        return nullptr;
      }
      auto dashes = get_dashes(document, nodes);
      auto dash = dashes.empty() ? 0.0 : dashes[0];
      auto gap = dashes.size() > 1 ? dashes[1] : dash;
      if(is_dash) {
        dash = *number;
      } else {
        gap = *number;
      }
      auto edit = StrokeEdit();
      if(dash == 0) {
        edit.m_dashes = std::vector<double>();
      } else {
        edit.m_dashes = std::vector<double>({dash, gap});
      }
      return std::make_unique<SetStrokeCommand>(nodes, std::move(edit));
    };
  }

  std::unique_ptr<Command> set_fill_color(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto color = std::get_if<Color>(&value);
    if(!color || nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<SetFillCommand>(nodes, SolidFill(*color));
  }

  PropertySetter make_stroke_setter(auto edit) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      if(nodes.empty()) {
        return nullptr;
      }
      auto stroke = StrokeEdit();
      if(!edit(stroke, value)) {
        return nullptr;
      }
      return std::make_unique<SetStrokeCommand>(nodes, std::move(stroke));
    };
  }

  std::unique_ptr<Command> set_opacity(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto number = get_number(value);
    if(!number || nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<SetOpacityCommand>(nodes, *number);
  }

  PropertySetter make_corner_setter(std::optional<Corner> corner) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto radius = get_number(value);
      if(!radius || nodes.empty()) {
        return nullptr;
      }
      if(corner) {
        return std::make_unique<SetCornerRadiusCommand>(
          nodes, *corner, *radius);
      }
      return std::make_unique<SetCornerRadiusCommand>(nodes, *radius);
    };
  }

  std::unique_ptr<Command> set_blend_mode(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto choice = std::get_if<Choice>(&value);
    if(!choice || nodes.empty()) {
      return nullptr;
    }
    auto blend_mode = get_blend_mode(*choice);
    if(!blend_mode) {
      return nullptr;
    }
    return std::make_unique<SetBlendModeCommand>(nodes, *blend_mode);
  }

  std::unique_ptr<Command> set_name(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto name = std::get_if<QString>(&value);
    if(!name || nodes.size() != 1 || name->trimmed().isEmpty()) {
      return nullptr;
    }
    return std::make_unique<RenameCommand>(
      nodes.front(), name->toStdString());
  }

  std::unique_ptr<Command> set_visibility(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto is_visible = std::get_if<bool>(&value);
    if(!is_visible || nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<SetVisibilityCommand>(nodes, *is_visible);
  }

  std::unique_ptr<Command> set_lock(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto is_locked = std::get_if<bool>(&value);
    if(!is_locked || nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<SetLockCommand>(nodes, *is_locked);
  }

  PropertyEditor make_editor(PropertySetter setter);
  PropertyEditor make_number_editor(PropertySetter setter,
    std::optional<double> minimum, std::optional<double> maximum);

  std::optional<double> get_numeric(const PropertyValue& value) {
    if(auto number = std::get_if<int>(&value)) {
      return static_cast<double>(*number);
    }
    return get_number(value);
  }

  PropertySetter make_bool_setter(auto make) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto flag = std::get_if<bool>(&value);
      if(!flag || nodes.empty()) {
        return nullptr;
      }
      return make(document, nodes, *flag);
    };
  }

  PropertySetter make_constraint_setter(
      std::optional<bool> ConstraintsEdit::* member, TextMeasurer measurer) {
    return make_bool_setter(
      [=] (const Document&, const std::vector<NodeId>& nodes, bool flag) {
        auto edit = ConstraintsEdit();
        edit.*member = flag;
        return std::make_unique<SetConstraintsCommand>(nodes, edit, measurer);
      });
  }

  PropertySetter make_resize_mode_setter(TextMeasurer measurer) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto choice = std::get_if<Choice>(&value);
      if(!choice || nodes.empty() || choice->m_index < 0 ||
          choice->m_index > 1) {
        return nullptr;
      }
      auto edit = ConstraintsEdit();
      edit.m_is_custom = choice->m_index == 1;
      return std::make_unique<SetConstraintsCommand>(nodes, edit, measurer);
    };
  }

  PropertySetter make_effect_setter(EffectKind kind, auto edit) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      if(nodes.empty()) {
        return nullptr;
      }
      auto effect = EffectEdit();
      if(!edit(effect, value)) {
        return nullptr;
      }
      return std::make_unique<SetEffectCommand>(nodes, kind, effect);
    };
  }

  void add_effect_editors(PropertyEditors& editors, const std::string& path,
      EffectKind kind) {
    editors.add(path, make_editor(make_effect_setter(kind,
      [] (EffectEdit& edit, const PropertyValue& value) {
        if(auto flag = std::get_if<bool>(&value)) {
          edit.m_is_visible = *flag;
          return true;
        }
        return false;
      })));
    if(kind != EffectKind::BLUR) {
      editors.add(path + ".color", make_editor(make_effect_setter(kind,
        [] (EffectEdit& edit, const PropertyValue& value) {
          if(auto color = std::get_if<Color>(&value)) {
            edit.m_color = *color;
            return true;
          }
          return false;
        })));
      editors.add(path + ".x", make_number_editor(make_effect_setter(kind,
        [] (EffectEdit& edit, const PropertyValue& value) {
          edit.m_x = get_number(value);
          return static_cast<bool>(edit.m_x);
        }), std::nullopt, std::nullopt));
      editors.add(path + ".y", make_number_editor(make_effect_setter(kind,
        [] (EffectEdit& edit, const PropertyValue& value) {
          edit.m_y = get_number(value);
          return static_cast<bool>(edit.m_y);
        }), std::nullopt, std::nullopt));
      auto blur = make_number_editor(make_effect_setter(kind,
        [] (EffectEdit& edit, const PropertyValue& value) {
          edit.m_blur = get_number(value);
          return static_cast<bool>(edit.m_blur);
        }), 0.0, std::nullopt);
      blur.m_scale = 2;
      editors.add(path + ".blur", std::move(blur));
      return;
    }
    editors.add(path + ".type", make_editor(make_effect_setter(kind,
      [] (EffectEdit& edit, const PropertyValue& value) {
        auto choice = std::get_if<Choice>(&value);
        if(!choice || choice->m_index < 0 || choice->m_index > 1) {
          return false;
        }
        edit.m_is_background = choice->m_index == 0;
        return true;
      })));
    editors.add(path + ".amount", make_number_editor(make_effect_setter(kind,
      [] (EffectEdit& edit, const PropertyValue& value) {
        edit.m_blur = get_number(value);
        return static_cast<bool>(edit.m_blur);
      }), 0.0, MAXIMUM_BLUR));
    editors.add(path + ".brightness", make_number_editor(make_effect_setter(
      kind, [] (EffectEdit& edit, const PropertyValue& value) {
        edit.m_brightness = get_number(value);
        return static_cast<bool>(edit.m_brightness);
      }), -MAXIMUM_BRIGHTNESS, MAXIMUM_BRIGHTNESS));
    auto opacity = make_number_editor(make_effect_setter(kind,
      [] (EffectEdit& edit, const PropertyValue& value) {
        edit.m_fill_opacity = get_number(value);
        return static_cast<bool>(edit.m_fill_opacity);
      }), 0.0, 1.0);
    opacity.m_scale = 100;
    opacity.m_suffix = "%";
    editors.add(path + ".opacity", std::move(opacity));
  }

  PropertySetter make_grid_setter(auto edit) {
    return [=] (const Document& document, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      if(nodes.empty()) {
        return nullptr;
      }
      auto grid = GridEdit();
      if(!edit(document, nodes, grid, value)) {
        return nullptr;
      }
      return std::make_unique<SetGridCommand>(nodes, grid);
    };
  }

  PropertySetter make_grid_number_setter(
      std::optional<double> GridEdit::* member) {
    return make_grid_setter([=] (const Document&, const std::vector<NodeId>&,
        GridEdit& edit, const PropertyValue& value) {
      edit.*member = get_numeric(value);
      return static_cast<bool>(edit.*member);
    });
  }

  std::optional<Margins> get_margins(
      const Document& document, const std::vector<NodeId>& nodes) {
    for(auto& id : nodes) {
      if(auto artboard = document.get(id).get_artboard()) {
        if(auto& grid = artboard->m_layout_grid) {
          return grid->m_margins;
        }
        return make_default_grid(
          *artboard, document.get_resources().m_grid_defaults).m_margins;
      }
    }
    return std::nullopt;
  }

  void add_grid_editors(PropertyEditors& editors) {
    editors.add("grid.visible", make_editor(make_grid_setter(
      [] (const Document&, const std::vector<NodeId>&, GridEdit& edit,
          const PropertyValue& value) {
        if(auto flag = std::get_if<bool>(&value)) {
          edit.m_is_visible = *flag;
          return true;
        }
        return false;
      })));
    editors.add("grid.type", make_editor(make_grid_setter(
      [] (const Document&, const std::vector<NodeId>&, GridEdit& edit,
          const PropertyValue& value) {
        auto choice = std::get_if<Choice>(&value);
        if(!choice || choice->m_index < 0 || choice->m_index > 1) {
          return false;
        }
        edit.m_type =
          choice->m_index == 0 ? GridType::LAYOUT : GridType::SQUARE;
        return true;
      })));
    editors.add("grid.columns", make_number_editor(make_grid_setter(
      [] (const Document&, const std::vector<NodeId>&, GridEdit& edit,
          const PropertyValue& value) {
        auto number = get_numeric(value);
        if(!number) {
          return false;
        }
        edit.m_column_count = static_cast<int>(std::lround(*number));
        return true;
      }), 1.0, MAXIMUM_COLUMNS));
    editors.add("grid.gutter", make_number_editor(
      make_grid_number_setter(&GridEdit::m_gutter), 0.0, std::nullopt));
    editors.add("grid.column_width", make_number_editor(
      make_grid_number_setter(&GridEdit::m_column_width), 1.0, std::nullopt));
    editors.add("grid.linked_margins", make_editor(make_grid_setter(
      [] (const Document& document, const std::vector<NodeId>& nodes,
          GridEdit& edit, const PropertyValue& value) {
        auto flag = std::get_if<bool>(&value);
        auto margins = get_margins(document, nodes);
        if(!flag || !margins) {
          return false;
        }
        if(*flag) {
          edit.m_right = margins->m_left;
          edit.m_bottom = margins->m_top;
        } else {
          edit.m_top = margins->m_top;
        }
        return true;
      })));
    editors.add("grid.margin_linked", make_number_editor(make_grid_setter(
      [] (const Document&, const std::vector<NodeId>&, GridEdit& edit,
          const PropertyValue& value) {
        auto number = get_numeric(value);
        if(!number) {
          return false;
        }
        edit.m_left = *number;
        edit.m_right = *number;
        return true;
      }), 0.0, std::nullopt));
    editors.add("grid.margin_top", make_number_editor(
      make_grid_number_setter(&GridEdit::m_top), 0.0, std::nullopt));
    editors.add("grid.margin_right", make_number_editor(
      make_grid_number_setter(&GridEdit::m_right), 0.0, std::nullopt));
    editors.add("grid.margin_bottom", make_number_editor(
      make_grid_number_setter(&GridEdit::m_bottom), 0.0, std::nullopt));
    editors.add("grid.margin_left", make_number_editor(
      make_grid_number_setter(&GridEdit::m_left), 0.0, std::nullopt));
    editors.add("grid.column_color", make_editor(make_grid_setter(
      [] (const Document&, const std::vector<NodeId>&, GridEdit& edit,
          const PropertyValue& value) {
        if(auto color = std::get_if<Color>(&value)) {
          edit.m_column_color = *color;
          return true;
        }
        return false;
      })));
    editors.add("grid.use_default", make_editor(make_grid_setter(
      [] (const Document&, const std::vector<NodeId>&, GridEdit& edit,
          const PropertyValue&) {
        edit.m_is_default = true;
        return true;
      })));
    editors.add("grid.make_default", make_editor(
      [] (const Document& document, const std::vector<NodeId>& nodes,
          const PropertyValue&) -> std::unique_ptr<Command> {
        for(auto& id : nodes) {
          if(document.get(id).get_artboard()) {
            return std::make_unique<MakeGridDefaultCommand>(id);
          }
        }
        return nullptr;
      }));
    editors.add("grid.size", make_number_editor(
      make_grid_number_setter(&GridEdit::m_square_size), 1.0, std::nullopt));
    editors.add("grid.color", make_editor(make_grid_setter(
      [] (const Document&, const std::vector<NodeId>&, GridEdit& edit,
          const PropertyValue& value) {
        if(auto color = std::get_if<Color>(&value)) {
          edit.m_square_color = *color;
          return true;
        }
        return false;
      })));
  }

  std::unique_ptr<Command> set_scrolling(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto choice = std::get_if<Choice>(&value);
    if(!choice || nodes.empty() || choice->m_index < 0 ||
        choice->m_index > 1) {
      return nullptr;
    }
    return std::make_unique<SetViewportCommand>(
      nodes, std::nullopt, choice->m_index == 1);
  }

  std::unique_ptr<Command> set_viewport_height(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto number = get_number(value);
    if(!number || nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<SetViewportCommand>(nodes, *number, true);
  }

  std::unique_ptr<Command> set_fill(const Document& document,
      const std::vector<NodeId>& nodes, const PropertyValue& value) {
    auto fill = std::get_if<Fill>(&value);
    if(!fill || nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<SetFillCommand>(nodes, *fill);
  }

  PropertyEditor make_editor(PropertySetter setter) {
    auto editor = PropertyEditor();
    editor.m_setter = std::move(setter);
    return editor;
  }

  PropertyEditor make_number_editor(PropertySetter setter,
      std::optional<double> minimum, std::optional<double> maximum) {
    auto editor = make_editor(std::move(setter));
    editor.m_minimum = minimum;
    editor.m_maximum = maximum;
    return editor;
  }
}

void PropertyEditors::add(std::string path, PropertyEditor editor) {
  m_editors.insert_or_assign(std::move(path), std::move(editor));
}

const PropertyEditor* PropertyEditors::find(std::string_view path) const {
  if(auto editor = m_editors.find(path); editor != m_editors.end()) {
    return &editor->second;
  }
  return nullptr;
}

PropertyEditors Rellu::make_standard_property_editors(TextMeasurer measurer) {
  auto editors = PropertyEditors();
  editors.add("layer.name", make_editor(set_name));
  editors.add("layer.visible", make_editor(set_visibility));
  editors.add("layer.locked", make_editor(set_lock));
  editors.add("transform.x", make_number_editor(
    make_position_setter(true, measurer), std::nullopt, std::nullopt));
  editors.add("transform.y", make_number_editor(
    make_position_setter(false, measurer), std::nullopt, std::nullopt));
  editors.add("transform.width", make_number_editor(
    make_size_setter(true, measurer), 0.0, std::nullopt));
  editors.add("transform.height", make_number_editor(
    make_size_setter(false, measurer), 0.0, std::nullopt));
  editors.add("transform.rotation", make_number_editor(
    make_rotation_setter(measurer), std::nullopt, std::nullopt));
  auto opacity = make_number_editor(set_opacity, 0.0, 1.0);
  opacity.m_scale = 100;
  opacity.m_suffix = "%";
  editors.add("appearance.opacity", std::move(opacity));
  editors.add("appearance.blend_mode", make_editor(set_blend_mode));
  editors.add("geometry.radii.all", make_number_editor(
    make_corner_setter(std::nullopt), 0.0, std::nullopt));
  editors.add("geometry.radii.top_left", make_number_editor(
    make_corner_setter(Corner::TOP_LEFT), 0.0, std::nullopt));
  editors.add("geometry.radii.top_right", make_number_editor(
    make_corner_setter(Corner::TOP_RIGHT), 0.0, std::nullopt));
  editors.add("geometry.radii.bottom_right", make_number_editor(
    make_corner_setter(Corner::BOTTOM_RIGHT), 0.0, std::nullopt));
  editors.add("geometry.radii.bottom_left", make_number_editor(
    make_corner_setter(Corner::BOTTOM_LEFT), 0.0, std::nullopt));
  editors.add("geometry.corner_radius", make_number_editor(
    make_corner_setter(std::nullopt), 0.0, std::nullopt));
  editors.add("fill", make_editor(set_fill_type));
  editors.add("fill.enabled", make_editor(set_fill_enabled));
  editors.add("fill.color", make_editor(set_fill_color));
  editors.add("stroke.enabled", make_editor(make_stroke_setter(
    [] (StrokeEdit& edit, const PropertyValue& value) {
      if(auto is_enabled = std::get_if<bool>(&value)) {
        edit.m_is_enabled = *is_enabled;
        return true;
      }
      return false;
    })));
  editors.add("stroke.color", make_editor(make_stroke_setter(
    [] (StrokeEdit& edit, const PropertyValue& value) {
      if(auto color = std::get_if<Color>(&value)) {
        edit.m_color = *color;
        return true;
      }
      return false;
    })));
  editors.add("stroke.width", make_number_editor(make_stroke_setter(
    [] (StrokeEdit& edit, const PropertyValue& value) {
      if(auto width = get_number(value); width && *width >= 0) {
        edit.m_width = *width;
        return true;
      }
      return false;
    }), 0.0, std::nullopt));
  editors.add("stroke.alignment", make_editor(make_stroke_setter(
    [] (StrokeEdit& edit, const PropertyValue& value) {
      auto choice = std::get_if<Choice>(&value);
      if(!choice || choice->m_index < 0 ||
          choice->m_index > static_cast<int>(StrokeAlignment::OUTSIDE)) {
        return false;
      }
      edit.m_alignment = static_cast<StrokeAlignment>(choice->m_index);
      return true;
    })));
  editors.add("stroke.cap", make_editor(make_stroke_setter(
    [] (StrokeEdit& edit, const PropertyValue& value) {
      auto choice = std::get_if<Choice>(&value);
      if(!choice || choice->m_index < 0 ||
          choice->m_index > static_cast<int>(StrokeCap::SQUARE)) {
        return false;
      }
      edit.m_cap = static_cast<StrokeCap>(choice->m_index);
      return true;
    })));
  editors.add("stroke.join", make_editor(make_stroke_setter(
    [] (StrokeEdit& edit, const PropertyValue& value) {
      auto choice = std::get_if<Choice>(&value);
      if(!choice || choice->m_index < 0 ||
          choice->m_index > static_cast<int>(StrokeJoin::BEVEL)) {
        return false;
      }
      edit.m_join = static_cast<StrokeJoin>(choice->m_index);
      return true;
    })));
  editors.add("stroke.dash",
    make_number_editor(make_dash_setter(true), 0.0, std::nullopt));
  editors.add("stroke.gap",
    make_number_editor(make_dash_setter(false), 0.0, std::nullopt));
  editors.add("fill.paint", make_editor(set_fill));
  editors.add("geometry.corners", make_number_editor(
    [] (const Document&, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto count = get_numeric(value);
      if(!count || nodes.empty()) {
        return nullptr;
      }
      return std::make_unique<SetCornerCountCommand>(
        nodes, static_cast<int>(std::lround(*count)));
    }, SetCornerCountCommand::MINIMUM_CORNERS,
    SetCornerCountCommand::MAXIMUM_CORNERS));
  auto star_ratio = make_number_editor(
    [] (const Document&, const std::vector<NodeId>& nodes,
        const PropertyValue& value) -> std::unique_ptr<Command> {
      auto ratio = get_number(value);
      if(!ratio || nodes.empty()) {
        return nullptr;
      }
      return std::make_unique<SetStarRatioCommand>(nodes, *ratio * 100);
    }, SetStarRatioCommand::MINIMUM_RATIO / 100,
    SetStarRatioCommand::MAXIMUM_RATIO / 100);
  star_ratio.m_scale = 100;
  star_ratio.m_suffix = "%";
  editors.add("geometry.star_ratio", std::move(star_ratio));
  editors.add("transform.lock_aspect", make_editor(make_bool_setter(
    [=] (const Document&, const std::vector<NodeId>& nodes, bool flag) {
      return std::make_unique<SetAspectLockCommand>(nodes, flag, measurer);
    })));
  editors.add("transform.fixed_position", make_editor(make_bool_setter(
    [] (const Document&, const std::vector<NodeId>& nodes, bool flag) {
      return std::make_unique<SetFixedPositionCommand>(nodes, flag);
    })));
  editors.add("layout.responsive",
    make_editor(
      make_constraint_setter(&ConstraintsEdit::m_is_enabled, measurer)));
  editors.add("layout.responsive.mode",
    make_editor(make_resize_mode_setter(measurer)));
  editors.add("layout.responsive.top",
    make_editor(make_constraint_setter(&ConstraintsEdit::m_top, measurer)));
  editors.add("layout.responsive.right",
    make_editor(make_constraint_setter(&ConstraintsEdit::m_right, measurer)));
  editors.add("layout.responsive.bottom",
    make_editor(make_constraint_setter(&ConstraintsEdit::m_bottom, measurer)));
  editors.add("layout.responsive.left",
    make_editor(make_constraint_setter(&ConstraintsEdit::m_left, measurer)));
  editors.add("layout.responsive.width",
    make_editor(make_constraint_setter(&ConstraintsEdit::m_width, measurer)));
  editors.add("layout.responsive.height",
    make_editor(make_constraint_setter(&ConstraintsEdit::m_height, measurer)));
  editors.add("layout.scrolling", make_editor(set_scrolling));
  editors.add("layout.viewport_height",
    make_number_editor(set_viewport_height, 1.0, std::nullopt));
  add_effect_editors(editors, "effects.inner_shadow", EffectKind::INNER_SHADOW);
  add_effect_editors(editors, "effects.drop_shadow", EffectKind::DROP_SHADOW);
  add_effect_editors(editors, "effects.blur", EffectKind::BLUR);
  add_grid_editors(editors);
  editors.add("export.marked", make_editor(make_bool_setter(
    [] (const Document&, const std::vector<NodeId>& nodes, bool flag) {
      return std::make_unique<SetExportMarkCommand>(nodes, flag);
    })));
  return editors;
}

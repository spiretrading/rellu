#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto PROPERTY_CHANGES = ChangeMask::NAME |
    ChangeMask::VISIBILITY | ChangeMask::LOCK | ChangeMask::TRANSFORM |
    ChangeMask::OPACITY | ChangeMask::BLEND_MODE | ChangeMask::ISOLATION |
    ChangeMask::CONSTRAINTS | ChangeMask::EXPORT | ChangeMask::GEOMETRY |
    ChangeMask::STYLE | ChangeMask::TEXT | ChangeMask::ROLES;

  using Names = std::vector<std::string>;

  bool contains(const Names& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
  }

  void add_name(Names& names, std::string_view name) {
    if(!contains(names, name)) {
      names.push_back(std::string(name));
    }
  }

  bool is_same(const auto& left, const auto& right) {
    if constexpr(std::is_floating_point_v<std::decay_t<decltype(left)>>) {
      return equals(left, right);
    } else {
      return left == right;
    }
  }

  const Style* get_style(const Node& node) {
    if(auto shape = node.get_shape()) {
      return &shape->m_style;
    } else if(auto text = node.get_text()) {
      return &text->m_style;
    }
    return nullptr;
  }

  std::optional<Margins> get_padding(const Group& group) {
    if(group.m_stack) {
      return group.m_stack->m_padding;
    } else if(group.m_padding) {
      return group.m_padding->m_padding;
    }
    return std::nullopt;
  }

  bool has_background(const Group& group) {
    if(group.m_stack) {
      return group.m_stack->m_has_background;
    } else if(group.m_padding) {
      return group.m_padding->m_has_background;
    }
    return false;
  }

  Shape* edit_shape(Node& node) {
    if(auto shape = std::get_if<Box<Shape>>(&node.m_payload)) {
      return &shape->edit();
    }
    return nullptr;
  }

  Text* edit_text(Node& node) {
    if(auto text = std::get_if<Box<Text>>(&node.m_payload)) {
      return &text->edit();
    }
    return nullptr;
  }

  Style* edit_style(Node& node) {
    if(auto shape = edit_shape(node)) {
      return &shape->m_style;
    } else if(auto text = edit_text(node)) {
      return &text->m_style;
    }
    return nullptr;
  }

  using Comparison =
    std::function<void (const Node& node, const Node& source, Names& names)>;
  using Assignment = std::function<
    void (Node& node, const Node& source, const Names& overridden)>;

  struct Field {
    ChangeMask m_mask;
    std::vector<std::string_view> m_names;
    bool m_is_placement;
    Comparison m_compare;
    Assignment m_assign;
  };

  Field make_header_field(ChangeMask mask, std::string_view name,
      bool is_placement, auto member) {
    return Field(mask, {name}, is_placement,
      [=] (const Node& node, const Node& source, Names& names) {
        if(!is_same(member(node.m_header), member(source.m_header))) {
          add_name(names, name);
        }
      },
      [=] (Node& node, const Node& source, const Names&) {
        member(node.m_header) = member(source.m_header);
      });
  }

  Field make_constraint_field(std::string_view name, bool Constraints::*flag) {
    return make_header_field(ChangeMask::CONSTRAINTS, name, false,
      [=] (auto& header) -> auto& {
        return header.m_constraints.*flag;
      });
  }

  Field make_stroke_field(std::string_view name, auto member) {
    return Field(ChangeMask::STYLE, {name}, false,
      [=] (const Node& node, const Node& source, Names& names) {
        auto style = get_style(node);
        auto source_style = get_style(source);
        if(style && source_style && style->m_stroke &&
            source_style->m_stroke && !is_same(
              member(*style->m_stroke), member(*source_style->m_stroke))) {
          add_name(names, name);
        }
      },
      [=] (Node& node, const Node& source, const Names&) {
        auto source_style = get_style(source);
        if(!source_style || !source_style->m_stroke) {
          return;
        }
        auto style = get_style(node);
        if(!style || !style->m_stroke) {
          return;
        }
        auto edited = edit_style(node);
        member(*edited->m_stroke) = member(*source_style->m_stroke);
      });
  }

  bool is_effect(const Effect& effect, std::size_t kind) {
    return effect.index() == kind;
  }

  std::vector<Effect> get_effects(const Style& style, std::size_t kind) {
    auto effects = std::vector<Effect>();
    for(auto& effect : style.m_effects) {
      if(is_effect(effect, kind)) {
        effects.push_back(effect);
      }
    }
    return effects;
  }

  Field make_effect_field(std::string_view name, std::size_t kind) {
    return Field(ChangeMask::STYLE, {name}, false,
      [=] (const Node& node, const Node& source, Names& names) {
        auto style = get_style(node);
        auto source_style = get_style(source);
        if(style && source_style &&
            get_effects(*style, kind) != get_effects(*source_style, kind)) {
          add_name(names, name);
        }
      },
      [=] (Node& node, const Node& source, const Names&) {
        auto source_style = get_style(source);
        if(!source_style || !get_style(node)) {
          return;
        }
        auto replacement = get_effects(*source_style, kind);
        auto& effects = edit_style(node)->m_effects;
        auto position = std::find_if(effects.begin(), effects.end(),
          [&] (const auto& effect) { return is_effect(effect, kind); });
        auto index = static_cast<std::size_t>(position - effects.begin());
        std::erase_if(effects, [&] (const auto& effect) {
          return is_effect(effect, kind);
        });
        index = std::min(index, effects.size());
        effects.insert(effects.begin() + static_cast<std::ptrdiff_t>(index),
          replacement.begin(), replacement.end());
      });
  }

  template<typename G>
  Field make_geometry_field(std::string_view name, auto member) {
    return Field(ChangeMask::GEOMETRY, {name}, false,
      [=] (const Node& node, const Node& source, Names& names) {
        auto shape = node.get_shape();
        auto source_shape = source.get_shape();
        if(!shape || !source_shape) {
          return;
        }
        auto geometry = std::get_if<G>(&shape->m_geometry);
        auto source_geometry = std::get_if<G>(&source_shape->m_geometry);
        if(geometry && source_geometry &&
            !is_same(member(*geometry), member(*source_geometry))) {
          add_name(names, name);
        }
      },
      [=] (Node& node, const Node& source, const Names&) {
        auto source_shape = source.get_shape();
        if(!source_shape || !node.get_shape()) {
          return;
        }
        auto source_geometry = std::get_if<G>(&source_shape->m_geometry);
        if(!source_geometry ||
            !std::holds_alternative<G>(node.get_shape()->m_geometry)) {
          return;
        }
        member(std::get<G>(edit_shape(node)->m_geometry)) =
          member(*source_geometry);
      });
  }

  bool has_fine_geometry(const ShapeGeometry& geometry) {
    return std::holds_alternative<RectangleShape>(geometry) ||
      std::holds_alternative<EllipseShape>(geometry) ||
      std::holds_alternative<LineShape>(geometry);
  }

  Field make_text_style_field(std::string_view name, auto member) {
    return Field(ChangeMask::TEXT, {name}, false,
      [=] (const Node& node, const Node& source, Names& names) {
        auto text = node.get_text();
        auto source_text = source.get_text();
        if(text && source_text && !is_same(member(text->m_base_style),
            member(source_text->m_base_style))) {
          add_name(names, name);
        }
      },
      [=] (Node& node, const Node& source, const Names& overridden) {
        auto source_text = source.get_text();
        if(!source_text || !node.get_text()) {
          return;
        }
        auto value = member(source_text->m_base_style);
        auto text = edit_text(node);
        member(text->m_base_style) = value;
        if(contains(overridden, "text") ||
            contains(overridden, "rangedStyles")) {
          for(auto& run : text->m_runs) {
            member(run.m_style) = value;
          }
        }
      });
  }

  Field make_paragraph_field(std::string_view name, auto member) {
    return Field(ChangeMask::TEXT, {name}, false,
      [=] (const Node& node, const Node& source, Names& names) {
        auto text = node.get_text();
        auto source_text = source.get_text();
        if(text && source_text && !is_same(member(text->m_paragraph_style),
            member(source_text->m_paragraph_style))) {
          add_name(names, name);
        }
      },
      [=] (Node& node, const Node& source, const Names&) {
        auto source_text = source.get_text();
        if(!source_text || !node.get_text()) {
          return;
        }
        member(edit_text(node)->m_paragraph_style) =
          member(source_text->m_paragraph_style);
      });
  }

  std::vector<Field> make_fields() {
    auto fields = std::vector<Field>();
    fields.push_back(make_header_field(ChangeMask::NAME, "name", true,
      [] (auto& header) -> auto& { return header.m_name; }));
    fields.push_back(make_header_field(ChangeMask::VISIBILITY, "visible", false,
      [] (auto& header) -> auto& { return header.m_is_visible; }));
    fields.push_back(make_header_field(ChangeMask::LOCK, "locked", false,
      [] (auto& header) -> auto& { return header.m_is_locked; }));
    fields.push_back(Field(ChangeMask::TRANSFORM, {"transform"}, true,
      [] (const Node& node, const Node& source, Names& names) {
        if(node.m_header.m_transform != source.m_header.m_transform ||
            node.m_header.m_transform_3d != source.m_header.m_transform_3d) {
          add_name(names, "transform");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        node.m_header.m_transform = source.m_header.m_transform;
        node.m_header.m_transform_3d = source.m_header.m_transform_3d;
      }));
    fields.push_back(make_header_field(ChangeMask::OPACITY, "opacity", false,
      [] (auto& header) -> auto& { return header.m_opacity; }));
    fields.push_back(Field(ChangeMask::BLEND_MODE | ChangeMask::ISOLATION,
      {"blendMode"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        if(node.m_header.m_blend_mode != source.m_header.m_blend_mode ||
            node.m_header.m_is_isolated != source.m_header.m_is_isolated) {
          add_name(names, "blendMode");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        node.m_header.m_blend_mode = source.m_header.m_blend_mode;
        node.m_header.m_is_isolated = source.m_header.m_is_isolated;
      }));
    fields.push_back(
      make_constraint_field("constraintsDisabled", &Constraints::m_is_enabled));
    fields.push_back(
      make_constraint_field("customConstraints", &Constraints::m_is_custom));
    fields.push_back(
      make_constraint_field("constraintLeft", &Constraints::m_left));
    fields.push_back(
      make_constraint_field("constraintRight", &Constraints::m_right));
    fields.push_back(
      make_constraint_field("constraintTop", &Constraints::m_top));
    fields.push_back(
      make_constraint_field("constraintBottom", &Constraints::m_bottom));
    fields.push_back(
      make_constraint_field("constraintWidth", &Constraints::m_width));
    fields.push_back(
      make_constraint_field("constraintHeight", &Constraints::m_height));
    fields.push_back(make_header_field(ChangeMask::EXPORT, "markedForExport",
      false, [] (auto& header) -> auto& {
        return header.m_is_marked_for_export;
      }));
    fields.push_back(Field(ChangeMask::STYLE, {"fill"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto style = get_style(node);
        auto source_style = get_style(source);
        if(style && source_style && style->m_fill != source_style->m_fill) {
          add_name(names, "fill");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto source_style = get_style(source);
        if(!source_style || !get_style(node)) {
          return;
        }
        edit_style(node)->m_fill = source_style->m_fill;
        if(auto solid = std::get_if<SolidFill>(&source_style->m_fill)) {
          if(auto text = edit_text(node)) {
            text->m_base_style.m_color = solid->m_color;
            for(auto& run : text->m_runs) {
              run.m_style.m_color = solid->m_color;
            }
          }
        }
      }));
    fields.push_back(Field(ChangeMask::STYLE, {"stroke"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto style = get_style(node);
        auto source_style = get_style(source);
        if(!style || !source_style) {
          return;
        }
        auto& stroke = style->m_stroke;
        auto& source_stroke = source_style->m_stroke;
        if(!stroke != !source_stroke || stroke && (
            stroke->m_color != source_stroke->m_color ||
            stroke->m_cap != source_stroke->m_cap ||
            stroke->m_join != source_stroke->m_join ||
            !equals(stroke->m_miter_limit, source_stroke->m_miter_limit))) {
          add_name(names, "stroke");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto source_style = get_style(source);
        if(!source_style || !get_style(node)) {
          return;
        }
        auto& stroke = edit_style(node)->m_stroke;
        auto& source_stroke = source_style->m_stroke;
        if(!stroke || !source_stroke) {
          stroke = source_stroke;
          return;
        }
        stroke->m_color = source_stroke->m_color;
        stroke->m_cap = source_stroke->m_cap;
        stroke->m_join = source_stroke->m_join;
        stroke->m_miter_limit = source_stroke->m_miter_limit;
      }));
    fields.push_back(make_stroke_field("strokeEnabled",
      [] (auto& stroke) -> auto& { return stroke.m_is_enabled; }));
    fields.push_back(make_stroke_field("strokeWidth",
      [] (auto& stroke) -> auto& { return stroke.m_width; }));
    fields.push_back(make_stroke_field("strokePosition",
      [] (auto& stroke) -> auto& { return stroke.m_alignment; }));
    fields.push_back(make_stroke_field("dashArray",
      [] (auto& stroke) -> auto& { return stroke.m_dashes; }));
    fields.push_back(make_effect_field("shadow", 0));
    fields.push_back(make_effect_field("innerShadow", 1));
    fields.push_back(make_effect_field("blur", 2));
    fields.push_back(make_geometry_field<RectangleShape>("width",
      [] (auto& rectangle) {
        return std::tie(rectangle.m_bounds.m_x, rectangle.m_bounds.m_width);
      }));
    fields.push_back(make_geometry_field<RectangleShape>("height",
      [] (auto& rectangle) {
        return std::tie(rectangle.m_bounds.m_y, rectangle.m_bounds.m_height);
      }));
    fields.push_back(make_geometry_field<RectangleShape>("topLeftCornerRadius",
      [] (auto& rectangle) -> auto& { return rectangle.m_radii.m_top_left; }));
    fields.push_back(make_geometry_field<RectangleShape>("topRightCornerRadius",
      [] (auto& rectangle) -> auto& { return rectangle.m_radii.m_top_right; }));
    fields.push_back(make_geometry_field<RectangleShape>(
      "bottomRightCornerRadius", [] (auto& rectangle) -> auto& {
        return rectangle.m_radii.m_bottom_right;
      }));
    fields.push_back(make_geometry_field<RectangleShape>(
      "bottomLeftCornerRadius", [] (auto& rectangle) -> auto& {
        return rectangle.m_radii.m_bottom_left;
      }));
    fields.push_back(make_geometry_field<EllipseShape>("radiusX",
      [] (auto& ellipse) {
        return std::tie(ellipse.m_center.m_x, ellipse.m_x_radius);
      }));
    fields.push_back(make_geometry_field<EllipseShape>("radiusY",
      [] (auto& ellipse) {
        return std::tie(ellipse.m_center.m_y, ellipse.m_y_radius);
      }));
    fields.push_back(make_geometry_field<LineShape>("startX",
      [] (auto& line) -> auto& { return line.m_start.m_x; }));
    fields.push_back(make_geometry_field<LineShape>("startY",
      [] (auto& line) -> auto& { return line.m_start.m_y; }));
    fields.push_back(make_geometry_field<LineShape>("endX",
      [] (auto& line) -> auto& { return line.m_end.m_x; }));
    fields.push_back(make_geometry_field<LineShape>("endY",
      [] (auto& line) -> auto& { return line.m_end.m_y; }));
    fields.push_back(Field(ChangeMask::GEOMETRY, {"pathData"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto shape = node.get_shape();
        auto source_shape = source.get_shape();
        if(!shape || !source_shape) {
          return;
        }
        if(shape->m_geometry.index() != source_shape->m_geometry.index() ||
            !has_fine_geometry(shape->m_geometry) &&
            shape->m_geometry != source_shape->m_geometry) {
          add_name(names, "pathData");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto source_shape = source.get_shape();
        if(!source_shape || !node.get_shape()) {
          return;
        }
        auto& geometry = node.get_shape()->m_geometry;
        if(geometry.index() != source_shape->m_geometry.index() ||
            !has_fine_geometry(geometry)) {
          edit_shape(node)->m_geometry = source_shape->m_geometry;
        }
      }));
    fields.push_back(Field(ChangeMask::GEOMETRY, {"textFrame"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto text = node.get_text();
        auto source_text = source.get_text();
        if(text && source_text && text->m_frame != source_text->m_frame) {
          add_name(names, "textFrame");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto source_text = source.get_text();
        if(source_text && node.get_text()) {
          edit_text(node)->m_frame = source_text->m_frame;
        }
      }));
    fields.push_back(Field(ChangeMask::TEXT, {"text", "rangedStyles"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto text = node.get_text();
        auto source_text = source.get_text();
        if(!text || !source_text) {
          return;
        }
        if(*text->m_content != *source_text->m_content) {
          add_name(names, "text");
        }
        if(text->m_runs != source_text->m_runs) {
          add_name(names, "rangedStyles");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto source_text = source.get_text();
        if(!source_text || !node.get_text()) {
          return;
        }
        auto text = edit_text(node);
        text->m_content = source_text->m_content;
        text->m_runs = source_text->m_runs;
        text->m_layout = source_text->m_layout;
      }));
    fields.push_back(make_text_style_field("fontFamily",
      [] (auto& style) -> auto& { return style.m_font.m_family; }));
    fields.push_back(make_text_style_field("fontStyle",
      [] (auto& style) -> auto& { return style.m_font.m_style; }));
    fields.push_back(make_text_style_field("postscriptName",
      [] (auto& style) -> auto& { return style.m_font.m_postscript_name; }));
    fields.push_back(make_text_style_field("fontSize",
      [] (auto& style) -> auto& { return style.m_font.m_size; }));
    fields.push_back(make_text_style_field("charSpacing",
      [] (auto& style) -> auto& { return style.m_letter_spacing; }));
    fields.push_back(make_text_style_field("underline",
      [] (auto& style) -> auto& { return style.m_is_underlined; }));
    fields.push_back(make_text_style_field("strikethrough",
      [] (auto& style) -> auto& { return style.m_is_struck_through; }));
    fields.push_back(make_text_style_field("textTransform",
      [] (auto& style) -> auto& { return style.m_transform; }));
    fields.push_back(make_text_style_field("textScript",
      [] (auto& style) -> auto& { return style.m_script; }));
    fields.push_back(make_paragraph_field("textAlign",
      [] (auto& style) -> auto& { return style.m_alignment; }));
    fields.push_back(make_paragraph_field("lineSpacing",
      [] (auto& style) -> auto& { return style.m_line_height; }));
    fields.push_back(make_paragraph_field("paragraphSpacing",
      [] (auto& style) -> auto& { return style.m_paragraph_spacing; }));
    fields.push_back(Field(ChangeMask::ROLES, {"contentStackMarginAfter"},
      false, [] (const Node&, const Node&, Names&) {},
      [] (Node& node, const Node& source, const Names&) {
        auto group = std::get_if<Group>(&node.m_payload);
        auto source_group = source.get_group();
        if(!group || !source_group) {
          return;
        }
        auto stack = source_group->m_stack;
        if(stack) {
          auto spacing = std::map<NodeId, double>();
          for(auto& entry : stack->m_spacing) {
            auto position = std::find(source.m_children.begin(),
              source.m_children.end(), entry.first);
            auto index =
              static_cast<std::size_t>(position - source.m_children.begin());
            if(index < node.m_children.size()) {
              spacing.emplace(node.m_children[index], entry.second);
            }
          }
          stack->m_spacing = std::move(spacing);
          if(group->m_stack) {
            stack->m_padding = group->m_stack->m_padding;
            stack->m_has_background = group->m_stack->m_has_background;
          }
        }
        auto padding = source_group->m_padding;
        if(padding && group->m_padding) {
          padding->m_padding = group->m_padding->m_padding;
          padding->m_has_background = group->m_padding->m_has_background;
        }
        if(group->m_stack != stack) {
          group->m_stack = std::move(stack);
        }
        if(group->m_padding != padding) {
          group->m_padding = std::move(padding);
        }
      }));
    fields.push_back(Field(ChangeMask::ROLES, {"contentPadding"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto group = node.get_group();
        auto source_group = source.get_group();
        if(group && source_group &&
            get_padding(*group) != get_padding(*source_group)) {
          add_name(names, "contentPadding");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto group = std::get_if<Group>(&node.m_payload);
        auto source_group = source.get_group();
        if(!group || !source_group) {
          return;
        }
        if(group->m_stack && source_group->m_stack &&
            group->m_stack->m_padding != source_group->m_stack->m_padding) {
          group->m_stack->m_padding = source_group->m_stack->m_padding;
        }
        if(group->m_padding && source_group->m_padding &&
            group->m_padding->m_padding !=
              source_group->m_padding->m_padding) {
          group->m_padding->m_padding = source_group->m_padding->m_padding;
        }
      }));
    fields.push_back(Field(ChangeMask::ROLES, {"hasContentBackground"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto group = node.get_group();
        auto source_group = source.get_group();
        if(group && source_group &&
            has_background(*group) != has_background(*source_group)) {
          add_name(names, "hasContentBackground");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto group = std::get_if<Group>(&node.m_payload);
        auto source_group = source.get_group();
        if(!group || !source_group) {
          return;
        }
        if(group->m_stack && source_group->m_stack &&
            group->m_stack->m_has_background !=
              source_group->m_stack->m_has_background) {
          group->m_stack->m_has_background =
            source_group->m_stack->m_has_background;
        }
        if(group->m_padding && source_group->m_padding &&
            group->m_padding->m_has_background !=
              source_group->m_padding->m_has_background) {
          group->m_padding->m_has_background =
            source_group->m_padding->m_has_background;
        }
      }));
    fields.push_back(Field(ChangeMask::ROLES, {"repeatGridStructure"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto group = node.get_group();
        auto source_group = source.get_group();
        if(group && source_group &&
            group->m_repeat_grid != source_group->m_repeat_grid) {
          add_name(names, "repeatGridStructure");
        }
      },
      [] (Node& node, const Node& source, const Names&) {
        auto group = std::get_if<Group>(&node.m_payload);
        auto source_group = source.get_group();
        if(group && source_group &&
            group->m_repeat_grid != source_group->m_repeat_grid) {
          group->m_repeat_grid = source_group->m_repeat_grid;
        }
      }));
    fields.push_back(Field(ChangeMask::ROLES, {"stateId"}, false,
      [] (const Node& node, const Node& source, Names& names) {
        auto instance = get_instance_role(node);
        auto source_instance = get_instance_role(source);
        if(instance && source_instance &&
            instance->m_state != source_instance->m_state) {
          add_name(names, "stateId");
        }
      },
      [] (Node&, const Node&, const Names&) {}));
    return fields;
  }

  const std::vector<Field>& get_fields() {
    static const auto fields = make_fields();
    return fields;
  }

  bool is_overridden(const Field& field, const Names& overridden) {
    return std::any_of(field.m_names.begin(), field.m_names.end(),
      [&] (auto name) { return contains(overridden, name); });
  }

  Names compare(const Node& node, const Node& source, ChangeMask mask,
      bool is_root) {
    auto names = Names();
    for(auto& field : get_fields()) {
      if(is_set(mask, field.m_mask) && !(is_root && field.m_is_placement)) {
        field.m_compare(node, source, names);
      }
    }
    return names;
  }

  bool assign(Node& node, const Node& source, ChangeMask mask, bool is_root,
      const Names& overridden, bool is_override_copy) {
    auto header = node.m_header;
    auto payload = node.m_payload;
    for(auto& field : get_fields()) {
      if(!is_set(mask, field.m_mask) || is_root && field.m_is_placement) {
        continue;
      }
      if(is_overridden(field, overridden) == is_override_copy) {
        field.m_assign(node, source, overridden);
      }
    }
    return node.m_header != header || node.m_payload != payload;
  }

  std::optional<Node> resolve(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node) {
      return std::nullopt;
    }
    if(!node->get_reference()) {
      return *node;
    }
    auto resolution = resolve_reference(document.get_nodes(), id);
    if(!resolution) {
      return std::nullopt;
    }
    auto source = document.find(resolution->m_source);
    if(!source) {
      return std::nullopt;
    }
    auto resolved = *source;
    resolved.m_header.m_transform = resolution->m_transform;
    return resolved;
  }

  const std::vector<NodeId>* get_source_children(
      const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node) {
      return nullptr;
    }
    if(auto reference = node->get_reference();
        reference && !reference->m_has_children) {
      auto resolution = resolve_reference(document.get_nodes(), id);
      if(!resolution) {
        return nullptr;
      }
      if(auto source = document.find(resolution->m_child_source)) {
        return &source->m_children;
      }
      return nullptr;
    }
    return &node->m_children;
  }

  void write(const Document& document, Transaction& transaction,
      const NodeId& id, const Node& node) {
    auto& current = document.get(id);
    auto is_header_changed = current.m_header != node.m_header;
    auto is_payload_changed = current.m_payload != node.m_payload;
    if(is_header_changed) {
      transaction.replace_header(node.m_header);
    }
    if(is_payload_changed) {
      transaction.replace_payload(id, node.m_payload);
    }
  }

  void edit_role(const Document& document, Transaction& transaction,
      const NodeId& id, auto edit) {
    auto node = document.find(id);
    if(!node || !get_instance_role(*node)) {
      return;
    }
    auto group = *node->get_group();
    auto role = *group.m_component_instance;
    edit(role);
    if(role != *group.m_component_instance) {
      group.m_component_instance = std::move(role);
      transaction.replace_payload(id, std::move(group));
    }
  }

  void edit_state(const Document& document, Transaction& transaction,
      const ComponentId& component, const StateId& state, auto edit) {
    auto& components = document.get_resources().m_components;
    auto entry = components.find(component);
    if(entry == components.end()) {
      return;
    }
    auto copy = entry->second;
    for(auto& candidate : copy.m_states) {
      if(candidate.m_id == state) {
        edit(candidate);
      }
    }
    if(copy != entry->second) {
      transaction.replace_component(std::move(copy));
    }
  }

  void remap(NodeId& id, const std::unordered_map<NodeId, NodeId>& mapping) {
    if(auto entry = mapping.find(id); entry != mapping.end()) {
      id = entry->second;
    }
  }

  template<typename V>
  void remap_keys(std::map<NodeId, V>& map,
      const std::unordered_map<NodeId, NodeId>& mapping) {
    auto remapped = std::map<NodeId, V>();
    for(auto& entry : map) {
      auto key = entry.first;
      remap(key, mapping);
      remapped.emplace(key, std::move(entry.second));
    }
    map = std::move(remapped);
  }

  void remap_copy(Payload& payload,
      const std::unordered_map<NodeId, NodeId>& mapping) {
    auto group = std::get_if<Group>(&payload);
    if(!group) {
      return;
    }
    if(group->m_stack) {
      remap_keys(group->m_stack->m_spacing, mapping);
    }
    if(auto& role = group->m_component_instance) {
      remap_keys(role->m_sync_map, mapping);
      remap_keys(role->m_overrides.m_properties, mapping);
      for(auto& added : role->m_overrides.m_added) {
        remap(added, mapping);
      }
      remap_keys(role->m_overrides.m_reordered, mapping);
      for(auto& entry : role->m_overrides.m_reordered) {
        remap(entry.second, mapping);
      }
      role->m_states.clear();
      role->m_is_main = false;
    }
  }

  void collect_subtree(const Document& document, const NodeId& id,
      std::vector<NodeId>& nodes) {
    nodes.push_back(id);
    for(auto& child : document.get(id).m_children) {
      collect_subtree(document, child, nodes);
    }
  }
}

namespace {
  const auto NUMBER_SEPARATOR = std::string_view(" \xE2\x80\x93 ");

  std::optional<int> get_instance_number(
      std::string_view name, std::string_view base) {
    if(!name.starts_with(base)) {
      return std::nullopt;
    }
    name.remove_prefix(base.size());
    if(!name.starts_with(NUMBER_SEPARATOR)) {
      return std::nullopt;
    }
    name.remove_prefix(NUMBER_SEPARATOR.size());
    auto number = 0;
    auto result = std::from_chars(name.data(), name.data() + name.size(),
      number);
    if(result.ec != std::errc() || result.ptr != name.data() + name.size() ||
        number <= 0) {
      return std::nullopt;
    }
    return number;
  }

  NodeId clone(const Document& document, Transaction& transaction,
      const NodeId& id, const std::optional<std::pair<NodeId, std::size_t>>&
        position, std::unordered_map<NodeId, NodeId>& mapping) {
    auto nodes = std::vector<NodeId>();
    collect_subtree(document, id, nodes);
    for(auto& node : nodes) {
      mapping.emplace(node, generate_node_id());
    }
    auto copies = std::vector<std::pair<NodeHeader, Payload>>();
    auto parents = std::vector<NodeId>();
    for(auto& node : nodes) {
      auto& original = document.get(node);
      auto header = original.m_header;
      header.m_id = mapping.at(node);
      auto payload = original.m_payload;
      remap_copy(payload, mapping);
      copies.emplace_back(std::move(header), std::move(payload));
      parents.push_back(original.m_parent);
    }
    auto root = NodeId();
    for(auto i = std::size_t(0); i != nodes.size(); ++i) {
      auto& copy = copies[i];
      if(i == 0) {
        if(position) {
          root = transaction.insert(position->first, position->second,
            std::move(copy.first), std::move(copy.second));
        } else {
          root = transaction.insert_root(
            std::move(copy.first), std::move(copy.second));
        }
      } else {
        transaction.append(mapping.at(parents[i]), std::move(copy.first),
          std::move(copy.second));
      }
    }
    return root;
  }
}

NodeId Rellu::clone_subtree(const Document& document,
    Transaction& transaction, const NodeId& id, const NodeId& parent,
    std::size_t index, std::unordered_map<NodeId, NodeId>& mapping) {
  return clone(document, transaction, id, std::pair(parent, index), mapping);
}

NodeId Rellu::clone_tree(const Document& document, Transaction& transaction,
    const NodeId& id, std::unordered_map<NodeId, NodeId>& mapping) {
  return clone(document, transaction, id, std::nullopt, mapping);
}

const ComponentInstance* Rellu::get_instance_role(const Node& node) {
  if(auto group = node.get_group()) {
    if(auto& role = group->m_component_instance) {
      return &*role;
    }
  }
  return nullptr;
}

NodeId Rellu::find_instance(const Document& document, const NodeId& id) {
  auto current = id;
  for(auto depth = 0; depth != MAXIMUM_DEPTH && !current.is_nil(); ++depth) {
    auto node = document.find(current);
    if(!node) {
      return NodeId();
    }
    if(get_instance_role(*node)) {
      return current;
    }
    current = node->m_parent;
  }
  return NodeId();
}

std::vector<NodeId> Rellu::find_instances(
    const Document& document, const ComponentId& component) {
  auto instances = std::vector<NodeId>();
  document.get_nodes().for_each([&] (const Node& node) {
    if(auto role = get_instance_role(node);
        role && role->m_component == component) {
      instances.push_back(node.get_id());
    }
  });
  return instances;
}

NodeId Rellu::find_main_component(
    const Document& document, const ComponentId& component) {
  auto main = NodeId();
  document.get_nodes().for_each([&] (const Node& node) {
    if(auto role = get_instance_role(node); role && role->m_is_main &&
        role->m_component == component && main.is_nil()) {
      main = node.get_id();
    }
  });
  return main;
}

NodeId Rellu::get_source_tree(
    const Document& document, const ComponentInstance& instance) {
  auto& components = document.get_resources().m_components;
  auto component = components.find(instance.m_component);
  if(component == components.end()) {
    return NodeId();
  }
  if(!instance.m_state.is_nil() &&
      instance.m_state != component->second.m_default_state) {
    for(auto& state : component->second.m_states) {
      if(state.m_id == instance.m_state && document.find(state.m_tree)) {
        return state.m_tree;
      }
    }
  }
  return component->second.m_master;
}

bool Rellu::is_linked(const Document& document, const ComponentId& component) {
  auto& components = document.get_resources().m_components;
  auto entry = components.find(component);
  return entry != components.end() &&
    static_cast<bool>(entry->second.m_link);
}

bool Rellu::copy_properties(
    Node& node, const Node& source, const std::vector<std::string>& names) {
  return assign(node, source, PROPERTY_CHANGES, false, names, true);
}

bool Rellu::update_properties(Node& node, const Node& source, bool is_root,
    const std::vector<std::string>& names) {
  return assign(node, source, PROPERTY_CHANGES, is_root, names, false);
}

std::vector<std::string> Rellu::find_differences(
    const Document& document, const NodeId& node, const NodeId& source) {
  auto resolved = resolve(document, node);
  auto resolved_source = resolve(document, source);
  if(!resolved || !resolved_source) {
    return {};
  }
  return compare(*resolved, *resolved_source, PROPERTY_CHANGES, false);
}

std::vector<NodeId> Rellu::materialize_reference(
    const Document& document, Transaction& transaction, const NodeId& id) {
  auto node = document.find(id);
  if(!node || !node->get_reference()) {
    return {};
  }
  auto resolution = resolve_reference(document.get_nodes(), id);
  if(!resolution) {
    return {};
  }
  auto reference = *node->get_reference();
  auto own = reference.m_has_children ? node->m_children :
    std::vector<NodeId>();
  auto parent = node->m_parent;
  auto index = get_index(document, id);
  auto source = document.get(resolution->m_source);
  auto is_compound = source.get_shape() &&
    std::holds_alternative<CompoundShape>(source.get_shape()->m_geometry);
  if(is_compound) {
    for(auto& child : own) {
      materialize_reference(document, transaction, child);
    }
  }
  auto sources = std::vector<NodeId>();
  if(!reference.m_has_children) {
    if(auto child_source = document.find(resolution->m_child_source)) {
      sources = child_source->m_children;
    }
  }
  auto children = std::vector<NodeId>();
  for(auto i = std::size_t(0); i != sources.size(); ++i) {
    children.push_back(generate_node_id());
  }
  auto header = source.m_header;
  header.m_id = id;
  header.m_transform = resolution->m_transform;
  auto payload = source.m_payload;
  if(auto group = std::get_if<Group>(&payload)) {
    auto mapping = std::unordered_map<NodeId, NodeId>();
    for(auto i = std::size_t(0); i != sources.size(); ++i) {
      mapping.emplace(sources[i], children[i]);
    }
    if(group->m_stack) {
      remap_keys(group->m_stack->m_spacing, mapping);
    }
    if(auto& role = group->m_component_instance) {
      auto sync_map = std::map<NodeId, NodeId>();
      for(auto i = std::size_t(0); i != sources.size(); ++i) {
        auto mirrored = sources[i];
        if(auto entry = role->m_sync_map.find(sources[i]);
            entry != role->m_sync_map.end()) {
          mirrored = entry->second;
        }
        sync_map.emplace(children[i], mirrored);
      }
      role->m_sync_map = std::move(sync_map);
      remap_keys(role->m_overrides.m_properties, mapping);
      role->m_overrides.m_added.clear();
      role->m_overrides.m_reordered.clear();
      role->m_states.clear();
      role->m_is_main = false;
    }
  }
  auto holder = NodeId();
  if(!own.empty()) {
    holder = transaction.insert(parent, index + 1, NodeHeader(), Group());
    for(auto i = std::size_t(0); i != own.size(); ++i) {
      transaction.move(own[i], holder, i);
    }
  }
  transaction.remove(id);
  transaction.insert(parent, index, std::move(header), std::move(payload));
  if(!holder.is_nil()) {
    for(auto i = std::size_t(0); i != own.size(); ++i) {
      transaction.move(own[i], id, i);
    }
    transaction.remove(holder);
  }
  auto mapping = std::unordered_map<NodeId, NodeId>();
  for(auto i = std::size_t(0); i != sources.size(); ++i) {
    if(is_compound) {
      auto copies = std::unordered_map<NodeId, NodeId>();
      children[i] =
        clone_subtree(document, transaction, sources[i], id, i, copies);
    } else {
      auto child_header = NodeHeader();
      child_header.m_id = children[i];
      transaction.append(id, std::move(child_header), Reference(sources[i]));
    }
    mapping.emplace(children[i], sources[i]);
  }
  if(!mapping.empty()) {
    for(auto ancestor = find_instance(document, parent); !ancestor.is_nil();
        ancestor = find_instance(
          document, document.get(ancestor).m_parent)) {
      edit_role(document, transaction, ancestor, [&] (auto& role) {
        for(auto& entry : mapping) {
          role.m_sync_map.insert_or_assign(entry.first, entry.second);
        }
      });
    }
  }
  return children;
}

void Rellu::materialize_references(const Document& document,
    Transaction& transaction, const std::vector<NodeId>& nodes) {
  auto pending = std::deque<NodeId>(nodes.begin(), nodes.end());
  auto visited = std::unordered_set<NodeId>();
  while(!pending.empty()) {
    auto id = pending.front();
    pending.pop_front();
    if(!visited.insert(id).second) {
      continue;
    }
    auto node = document.find(id);
    if(!node || node->get_artboard()) {
      continue;
    }
    if(node->get_reference() &&
        !find_instance(document, node->m_parent).is_nil()) {
      materialize_reference(document, transaction, id);
      node = document.find(id);
      if(!node) {
        continue;
      }
    }
    if(node->get_group() || node->get_reference()) {
      pending.insert(
        pending.end(), node->m_children.begin(), node->m_children.end());
    }
  }
}

namespace {
  bool has_instance_references(const Document& document, const NodeId& id,
      bool is_inside, int depth) {
    auto node = document.find(id);
    if(!node || depth == MAXIMUM_DEPTH || node->get_artboard()) {
      return false;
    }
    if(node->get_reference() && is_inside) {
      return true;
    }
    if(!node->get_group() && !node->get_reference()) {
      return false;
    }
    is_inside = is_inside || get_instance_role(*node);
    return std::any_of(node->m_children.begin(), node->m_children.end(),
      [&] (const auto& child) {
        return has_instance_references(document, child, is_inside, depth + 1);
      });
  }

  bool handles_references(const Command& command) {
    if(auto composite = dynamic_cast<const CompositeCommand*>(&command)) {
      auto& commands = composite->get_commands();
      return std::all_of(commands.begin(), commands.end(),
        [] (const auto& child) { return handles_references(*child); });
    }
    return dynamic_cast<const TranslateCommand*>(&command) ||
      dynamic_cast<const TransformCommand*>(&command) ||
      dynamic_cast<const ArrangeCommand*>(&command) ||
      dynamic_cast<const ReparentCommand*>(&command) ||
      dynamic_cast<const GroupCommand*>(&command) ||
      dynamic_cast<const UngroupCommand*>(&command) ||
      dynamic_cast<const DeleteCommand*>(&command) ||
      dynamic_cast<const DuplicateCommand*>(&command) ||
      dynamic_cast<const InsertNodeCommand*>(&command) ||
      dynamic_cast<const MakeComponentCommand*>(&command) ||
      dynamic_cast<const UngroupComponentCommand*>(&command) ||
      dynamic_cast<const ResetOverridesCommand*>(&command) ||
      dynamic_cast<const SetInstanceStateCommand*>(&command) ||
      dynamic_cast<const SwapComponentCommand*>(&command) ||
      dynamic_cast<const InsertInstanceCommand*>(&command) ||
      dynamic_cast<const RestoreMainComponentCommand*>(&command) ||
      dynamic_cast<const AddStateCommand*>(&command) ||
      dynamic_cast<const RemoveStateCommand*>(&command) ||
      dynamic_cast<const RenameStateCommand*>(&command) ||
      dynamic_cast<const RenameComponentCommand*>(&command) ||
      dynamic_cast<const MakeEditableCommand*>(&command);
  }

  class InstanceEditCommand : public Command {
    public:
      InstanceEditCommand(
          std::vector<NodeId> targets, std::unique_ptr<Command> command)
        : m_targets(std::move(targets)),
          m_command(std::move(command)) {}

      std::string get_label() const override {
        return m_command->get_label();
      }

      void apply(const Document& document, Transaction& transaction) override {
        materialize_references(document, transaction, m_targets);
        m_command->apply(document, transaction);
      }

      bool merge(const Command& command) override {
        if(auto edit = dynamic_cast<const InstanceEditCommand*>(&command)) {
          return m_command->merge(*edit->m_command);
        }
        return m_command->merge(command);
      }

      std::optional<std::vector<NodeId>> get_selection() const override {
        return m_command->get_selection();
      }

      const Command& get_command() const {
        return *m_command;
      }

    private:
      std::vector<NodeId> m_targets;
      std::unique_ptr<Command> m_command;
  };
}

const Command* Rellu::get_edited_command(const Command* command) {
  if(auto edit = dynamic_cast<const InstanceEditCommand*>(command)) {
    return &edit->get_command();
  }
  return command;
}

bool Rellu::is_text_reference(const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node || !node->get_reference() ||
      find_instance(document, node->m_parent).is_nil()) {
    return false;
  }
  auto resolution = resolve_reference(document.get_nodes(), id);
  if(!resolution) {
    return false;
  }
  auto source = document.find(resolution->m_source);
  return source && source->get_text();
}

std::unique_ptr<Command> Rellu::make_instance_edit(const Document& document,
    const std::vector<NodeId>& targets, std::unique_ptr<Command> command) {
  if(!command || handles_references(*command)) {
    return command;
  }
  auto is_needed = std::any_of(targets.begin(), targets.end(),
    [&] (const auto& id) {
      auto node = document.find(id);
      return node && has_instance_references(document, id,
        !find_instance(document, node->m_parent).is_nil(), 0);
    });
  if(!is_needed) {
    return command;
  }
  return std::make_unique<InstanceEditCommand>(targets, std::move(command));
}

std::string Rellu::make_instance_name(
    const Document& document, const ComponentId& component) {
  auto& components = document.get_resources().m_components;
  auto entry = components.find(component);
  if(entry == components.end()) {
    return std::string();
  }
  auto& name = entry->second.m_name;
  auto highest = std::optional<int>();
  for(auto& instance : find_instances(document, component)) {
    if(auto number =
        get_instance_number(document.get(instance).m_header.m_name, name)) {
      highest = std::max(highest.value_or(0), *number);
    }
  }
  if(!highest) {
    return name;
  }
  return name + std::string(NUMBER_SEPARATOR) + std::to_string(*highest + 1);
}

NodeId Rellu::insert_instance(const Document& document,
    Transaction& transaction, const ComponentId& component,
    const NodeId& parent, std::size_t index, const Matrix& transform,
    const TextMeasurer& measurer) {
  auto& components = document.get_resources().m_components;
  auto entry = components.find(component);
  if(entry == components.end()) {
    throw std::invalid_argument("Unknown component.");
  }
  auto master = document.find(entry->second.m_master);
  if(!master) {
    throw std::invalid_argument("The component has no master.");
  }
  auto header = master->m_header;
  header.m_id = generate_node_id();
  header.m_name = make_instance_name(document, component);
  header.m_transform = transform;
  header.m_transform_3d = std::nullopt;
  header.m_opaque = OpaqueData();
  auto group = master->get_group() ? *master->get_group() : Group();
  auto sources = master->m_children;
  auto mapping = std::unordered_map<NodeId, NodeId>();
  auto role = ComponentInstance();
  role.m_component = component;
  if(auto bounds = get_local_bounds(
      document, master->get_id(), BoundsType::GEOMETRY, measurer)) {
    role.m_size = Size(bounds->m_width, bounds->m_height);
  }
  for(auto& source : sources) {
    auto child = generate_node_id();
    mapping.emplace(source, child);
    role.m_sync_map.emplace(child, source);
  }
  if(group.m_stack) {
    remap_keys(group.m_stack->m_spacing, mapping);
  }
  group.m_component_instance = std::move(role);
  auto id = transaction.insert(
    parent, index, std::move(header), std::move(group));
  for(auto& source : sources) {
    auto child_header = NodeHeader();
    child_header.m_id = mapping.at(source);
    transaction.append(id, std::move(child_header), Reference(source));
  }
  return id;
}

namespace {
  enum class ContextKind : unsigned char {
    MASTER,
    STATE,
    MAIN,
    INSTANCE
  };

  struct Context {
    ContextKind m_kind;
    NodeId m_root;
    ComponentId m_component;
    StateId m_state;
  };

  struct Event {
    NodeId m_id;
    ChangeMask m_mask;
  };

  class Refresh {
    public:
      Refresh(Document& document, Transaction& transaction)
          : m_document(&document),
            m_transaction(&transaction) {
        for(auto& entry : document.get_resources().m_components) {
          m_masters.emplace(entry.second.m_master, entry.first);
          for(auto& state : entry.second.m_states) {
            m_states.emplace(state.m_tree, std::pair(entry.first, state.m_id));
          }
        }
      }

      void run(const ChangeSet& changes) {
        deduplicate_mains(changes);
        renumber_copies(changes);
        for(auto& change : changes.m_modified) {
          handle_edit(change.m_id, change.m_mask);
        }
        while(!m_sources.empty()) {
          auto event = m_sources.front();
          m_sources.pop_front();
          propagate(event);
        }
        sync_interactions(changes);
      }

    private:
      Document* m_document;
      Transaction* m_transaction;
      std::unordered_map<NodeId, ComponentId> m_masters;
      std::unordered_map<NodeId, std::pair<ComponentId, StateId>> m_states;
      std::optional<std::unordered_map<ComponentId, std::vector<NodeId>>>
        m_instances;
      std::unordered_map<NodeId,
        std::unordered_map<NodeId, std::vector<NodeId>>> m_inverses;
      std::deque<Event> m_sources;

      const Component* find_component(const ComponentId& id) const {
        auto& components = m_document->get_resources().m_components;
        auto entry = components.find(id);
        if(entry == components.end()) {
          return nullptr;
        }
        return &entry->second;
      }

      const ComponentState* find_state(const Context& context) const {
        auto component = find_component(context.m_component);
        if(!component) {
          return nullptr;
        }
        for(auto& state : component->m_states) {
          if(state.m_id == context.m_state) {
            return &state;
          }
        }
        return nullptr;
      }

      std::optional<Context> find_context(const NodeId& id) const {
        auto current = id;
        for(auto depth = 0; depth != MAXIMUM_DEPTH; ++depth) {
          auto node = m_document->find(current);
          if(!node) {
            return std::nullopt;
          }
          if(auto role = get_instance_role(*node)) {
            return Context(role->m_is_main ? ContextKind::MAIN :
              ContextKind::INSTANCE, current, role->m_component,
              role->m_state);
          }
          if(node->m_parent.is_nil()) {
            if(m_document->is_on_canvas(current)) {
              return std::nullopt;
            }
            if(auto master = m_masters.find(current);
                master != m_masters.end()) {
              return Context(
                ContextKind::MASTER, current, master->second, StateId());
            }
            if(auto state = m_states.find(current); state != m_states.end()) {
              return Context(ContextKind::STATE, current, state->second.first,
                state->second.second);
            }
            return std::nullopt;
          }
          current = node->m_parent;
        }
        return std::nullopt;
      }

      std::optional<Context> find_outer_context(const Context& context) const {
        auto node = m_document->find(context.m_root);
        if(!node || node->m_parent.is_nil()) {
          return std::nullopt;
        }
        return find_context(node->m_parent);
      }

      NodeId get_tree(const Context& context) const {
        if(context.m_kind == ContextKind::STATE) {
          if(auto component = find_component(context.m_component)) {
            return component->m_master;
          }
          return NodeId();
        } else if(context.m_kind == ContextKind::MASTER) {
          return NodeId();
        }
        auto role = get_instance_role(m_document->get(context.m_root));
        return get_source_tree(*m_document, *role);
      }

      const std::map<NodeId, NodeId>* get_sync_map(
          const Context& context) const {
        if(context.m_kind == ContextKind::STATE) {
          if(auto state = find_state(context)) {
            return &state->m_sync_map;
          }
          return nullptr;
        } else if(context.m_kind == ContextKind::MASTER) {
          return nullptr;
        }
        return &get_instance_role(m_document->get(context.m_root))->
          m_sync_map;
      }

      const Overrides* get_overrides(const Context& context) const {
        if(context.m_kind == ContextKind::STATE) {
          if(auto state = find_state(context)) {
            return &state->m_overrides;
          }
          return nullptr;
        } else if(context.m_kind == ContextKind::INSTANCE) {
          return &get_instance_role(m_document->get(context.m_root))->
            m_overrides;
        }
        return nullptr;
      }

      Names get_overridden(const Context& context, const NodeId& id) const {
        if(auto overrides = get_overrides(context)) {
          if(auto names = overrides->m_properties.find(id);
              names != overrides->m_properties.end()) {
            return names->second;
          }
        }
        return Names();
      }

      NodeId get_source(const Context& context, const NodeId& id) const {
        if(id == context.m_root) {
          return get_tree(context);
        }
        if(auto sync_map = get_sync_map(context)) {
          if(auto entry = sync_map->find(id); entry != sync_map->end()) {
            return entry->second;
          }
        }
        if(auto node = m_document->find(id)) {
          if(auto reference = node->get_reference()) {
            return reference->m_source;
          }
        }
        return NodeId();
      }

      void edit_overrides(const Context& context, auto edit) {
        if(context.m_kind == ContextKind::STATE) {
          edit_state(*m_document, *m_transaction, context.m_component,
            context.m_state, [&] (auto& state) {
              edit(state.m_overrides);
            });
        } else if(context.m_kind == ContextKind::INSTANCE) {
          edit_role(*m_document, *m_transaction, context.m_root,
            [&] (auto& role) {
              edit(role.m_overrides);
            });
        }
      }

      void edit_sync_map(const Context& context, auto edit) {
        if(context.m_kind == ContextKind::STATE) {
          edit_state(*m_document, *m_transaction, context.m_component,
            context.m_state, [&] (auto& state) {
              edit(state.m_sync_map);
            });
        } else if(context.m_kind != ContextKind::MASTER) {
          edit_role(*m_document, *m_transaction, context.m_root,
            [&] (auto& role) {
              edit(role.m_sync_map);
            });
        }
        m_inverses.erase(context.m_root);
      }

      void deduplicate_mains(const ChangeSet& changes) {
        auto inserted = std::unordered_set<NodeId>(
          changes.m_inserted.begin(), changes.m_inserted.end());
        auto kept = std::unordered_set<ComponentId>();
        for(auto& id : changes.m_inserted) {
          auto node = m_document->find(id);
          if(!node) {
            continue;
          }
          auto role = get_instance_role(*node);
          if(!role || !role->m_is_main) {
            continue;
          }
          auto component = role->m_component;
          auto is_duplicate = kept.contains(component);
          if(!is_duplicate) {
            for(auto& instance : get_instances(component)) {
              auto other = get_instance_role(m_document->get(instance));
              if(instance != id && other->m_is_main &&
                  !inserted.contains(instance)) {
                is_duplicate = true;
                break;
              }
            }
          }
          if(is_duplicate) {
            edit_role(*m_document, *m_transaction, id, [] (auto& role) {
              role.m_is_main = false;
            });
          } else {
            kept.insert(component);
          }
        }
      }

      bool is_in_copied_artboard(
          const Node& node, const std::unordered_set<NodeId>& inserted) const {
        auto parent = node.m_parent;
        for(auto depth = 0; !parent.is_nil() && depth != 512; ++depth) {
          auto current = m_document->find(parent);
          if(!current) {
            return false;
          }
          if(current->get_artboard()) {
            return inserted.contains(parent);
          }
          parent = current->m_parent;
        }
        return false;
      }

      void renumber_copies(const ChangeSet& changes) {
        auto inserted = std::unordered_set<NodeId>(
          changes.m_inserted.begin(), changes.m_inserted.end());
        for(auto& id : changes.m_inserted) {
          auto node = m_document->find(id);
          auto role = node ? get_instance_role(*node) : nullptr;
          auto component = role ? find_component(role->m_component) : nullptr;
          if(!component || role->m_is_main || !get_instance_number(
              node->m_header.m_name, component->m_name) ||
              is_in_copied_artboard(*node, inserted)) {
            continue;
          }
          auto is_taken = false;
          for(auto& instance : find_instances(*m_document, role->m_component)) {
            if(instance != id && !inserted.contains(instance) &&
                m_document->get(instance).m_header.m_name ==
                  node->m_header.m_name) {
              is_taken = true;
              break;
            }
          }
          if(is_taken) {
            auto header = node->m_header;
            header.m_name = make_instance_name(*m_document, role->m_component);
            m_transaction->replace_header(std::move(header));
          }
        }
      }

      const std::vector<NodeId>& get_instances(const ComponentId& component) {
        if(!m_instances) {
          m_instances.emplace();
          m_document->get_nodes().for_each([&] (const Node& node) {
            if(auto role = get_instance_role(node)) {
              (*m_instances)[role->m_component].push_back(node.get_id());
            }
          });
        }
        static const auto none = std::vector<NodeId>();
        auto entry = m_instances->find(component);
        if(entry == m_instances->end()) {
          return none;
        }
        return entry->second;
      }

      void handle_edit(const NodeId& id, ChangeMask mask) {
        auto context = find_context(id);
        if(!context) {
          return;
        }
        auto recorded = mask;
        while(context && context->m_kind == ContextKind::INSTANCE) {
          record_override(*context, id, recorded);
          recorded = mask & PROPERTY_CHANGES;
          context = find_outer_context(*context);
        }
        if(!context) {
          return;
        }
        if(context->m_kind == ContextKind::MAIN) {
          push_main(*context, id, mask);
        } else if(context->m_kind == ContextKind::MASTER) {
          m_sources.push_back(Event(id, mask));
        } else if(context->m_kind == ContextKind::STATE) {
          record_override(*context, id, mask);
          m_sources.push_back(Event(id, mask));
        }
      }

      void record_override(
          const Context& context, const NodeId& id, ChangeMask mask) {
        auto source = get_source(context, id);
        auto node = m_document->find(id);
        if(source.is_nil() || !node) {
          return;
        }
        if(is_set(mask, PROPERTY_CHANGES) && !node->get_reference()) {
          auto resolved = resolve(*m_document, id);
          auto resolved_source = resolve(*m_document, source);
          if(resolved && resolved_source) {
            auto names = compare(*resolved, *resolved_source,
              mask & PROPERTY_CHANGES, id == context.m_root);
            if(!names.empty()) {
              edit_overrides(context, [&] (auto& overrides) {
                auto& recorded = overrides.m_properties[id];
                for(auto& name : names) {
                  add_name(recorded, name);
                }
              });
            }
          }
        }
        if(is_set(mask, ChangeMask::CHILDREN)) {
          record_structure(context, id, source);
        }
      }

      void record_structure(
          const Context& context, const NodeId& id, const NodeId& source) {
        auto children = m_document->get(id).m_children;
        auto source_children = std::vector<NodeId>();
        if(auto list = get_source_children(*m_document, source)) {
          source_children = *list;
        }
        auto mapped = std::unordered_set<NodeId>();
        auto added = std::vector<NodeId>();
        for(auto& child : children) {
          auto child_source = get_source(context, child);
          if(child_source.is_nil()) {
            added.push_back(child);
          } else {
            mapped.insert(child_source);
          }
        }
        auto removed = std::vector<NodeId>();
        for(auto& child : source_children) {
          if(!mapped.contains(child)) {
            removed.push_back(child);
          }
        }
        edit_overrides(context, [&] (auto& overrides) {
          for(auto& child : added) {
            if(std::find(overrides.m_added.begin(), overrides.m_added.end(),
                child) == overrides.m_added.end()) {
              overrides.m_added.push_back(child);
            }
          }
          for(auto& child : removed) {
            if(std::find(overrides.m_removed.begin(),
                overrides.m_removed.end(), child) ==
                overrides.m_removed.end()) {
              overrides.m_removed.push_back(child);
            }
          }
          std::erase_if(overrides.m_added, [&] (const auto& child) {
            return !m_document->find(child);
          });
        });
      }

      NodeId editable(const NodeId& id) {
        auto node = m_document->find(id);
        if(node && node->get_reference()) {
          materialize_reference(*m_document, *m_transaction, id);
          m_inverses.clear();
        }
        return id;
      }

      void push_main(const Context& context, const NodeId& id,
          ChangeMask mask) {
        auto source = get_source(context, id);
        if(source.is_nil() || !m_document->find(source) ||
            is_linked(*m_document, context.m_component)) {
          return;
        }
        auto is_root = id == context.m_root;
        if(is_root && is_set(mask, ChangeMask::NAME)) {
          rename(context);
        }
        if(is_set(mask, PROPERTY_CHANGES) &&
            !m_document->get(id).get_reference()) {
          auto values = resolve(*m_document, id);
          editable(source);
          auto target = m_document->get(source);
          if(values && assign(target, *values, mask & PROPERTY_CHANGES,
              is_root, Names(), false)) {
            write(*m_document, *m_transaction, source, target);
            m_sources.push_back(Event(source, mask & PROPERTY_CHANGES));
            record_nested(source, mask & PROPERTY_CHANGES);
          }
        }
        if(is_set(mask, ChangeMask::CHILDREN)) {
          push_structure(context, id, source);
        }
      }

      void rename(const Context& context) {
        auto name = m_document->get(context.m_root).m_header.m_name;
        auto component = find_component(context.m_component);
        if(!component || component->m_name == name) {
          return;
        }
        auto copy = *component;
        copy.m_name = name;
        auto master = copy.m_master;
        m_transaction->replace_component(std::move(copy));
        if(auto node = m_document->find(master)) {
          auto header = node->m_header;
          header.m_name = name;
          m_transaction->replace_header(std::move(header));
        }
      }

      void record_nested(const NodeId& id, ChangeMask mask) {
        auto context = find_context(id);
        if(context && (context->m_kind == ContextKind::INSTANCE ||
            context->m_kind == ContextKind::STATE)) {
          record_override(*context, id, mask);
        }
      }

      std::unordered_set<NodeId> get_mapped_sources(const Context& context) {
        auto sources = std::unordered_set<NodeId>();
        auto nodes = std::vector<NodeId>();
        collect_subtree(*m_document, context.m_root, nodes);
        for(auto& node : nodes) {
          auto source = get_source(context, node);
          if(!source.is_nil()) {
            sources.insert(source);
          }
        }
        return sources;
      }

      void push_structure(const Context& context, const NodeId& id,
          const NodeId& source) {
        editable(source);
        auto children = m_document->get(id).m_children;
        auto is_changed = false;
        for(auto& child : children) {
          auto child_source = get_source(context, child);
          auto source_node = child_source.is_nil() ? nullptr :
            m_document->find(child_source);
          if(!source_node) {
            auto mapping = std::unordered_map<NodeId, NodeId>();
            clone_subtree(*m_document, *m_transaction, child, source,
              m_document->get(source).m_children.size(), mapping);
            edit_sync_map(context, [&] (auto& sync_map) {
              for(auto& entry : mapping) {
                sync_map.insert_or_assign(entry.first, entry.second);
              }
            });
            is_changed = true;
          } else if(source_node->m_parent != source &&
              find_tree(child_source) == find_tree(source) &&
              !is_ancestor(*m_document, child_source, source)) {
            m_transaction->move(child_source, source,
              m_document->get(source).m_children.size());
            is_changed = true;
          }
        }
        auto mapped = get_mapped_sources(context);
        for(auto& child : std::vector<NodeId>(
            m_document->get(source).m_children)) {
          if(!mapped.contains(child)) {
            m_transaction->remove(child);
            is_changed = true;
          }
        }
        auto order = std::vector<NodeId>();
        for(auto& child : m_document->get(id).m_children) {
          auto child_source = get_source(context, child);
          if(auto node = m_document->find(child_source);
              node && node->m_parent == source) {
            order.push_back(child_source);
          }
        }
        for(auto i = std::size_t(0); i != order.size(); ++i) {
          if(m_document->get(source).m_children[i] != order[i]) {
            m_transaction->move(order[i], source, i);
            is_changed = true;
          }
        }
        edit_sync_map(context, [&] (auto& sync_map) {
          std::erase_if(sync_map, [&] (const auto& entry) {
            return !m_document->find(entry.first) ||
              !m_document->find(entry.second);
          });
        });
        if(is_changed) {
          m_inverses.clear();
          m_sources.push_back(Event(source, ChangeMask::CHILDREN));
          record_nested(source, ChangeMask::CHILDREN);
        }
      }

      std::vector<Context> get_dependents(const NodeId& tree) {
        auto dependents = std::vector<Context>();
        auto component = ComponentId();
        if(auto master = m_masters.find(tree); master != m_masters.end()) {
          component = master->second;
          if(auto entry = find_component(component)) {
            for(auto& state : entry->m_states) {
              if(m_document->find(state.m_tree)) {
                dependents.push_back(Context(ContextKind::STATE, state.m_tree,
                  component, state.m_id));
              }
            }
          }
        } else if(auto state = m_states.find(tree); state != m_states.end()) {
          component = state->second.first;
        } else {
          return dependents;
        }
        for(auto& instance : get_instances(component)) {
          auto node = m_document->find(instance);
          if(!node) {
            continue;
          }
          auto role = get_instance_role(*node);
          if(get_source_tree(*m_document, *role) == tree) {
            dependents.push_back(Context(role->m_is_main ? ContextKind::MAIN :
              ContextKind::INSTANCE, instance, component, role->m_state));
          }
        }
        return dependents;
      }

      const std::vector<NodeId>& get_copies(
          const Context& context, const NodeId& source) {
        auto entry = m_inverses.find(context.m_root);
        if(entry == m_inverses.end()) {
          auto inverse = std::unordered_map<NodeId, std::vector<NodeId>>();
          auto nodes = std::vector<NodeId>();
          collect_subtree(*m_document, context.m_root, nodes);
          for(auto& node : nodes) {
            auto mirrored = get_source(context, node);
            if(!mirrored.is_nil()) {
              inverse[mirrored].push_back(node);
            }
          }
          entry = m_inverses.emplace(context.m_root, std::move(inverse)).first;
        }
        static const auto none = std::vector<NodeId>();
        auto copies = entry->second.find(source);
        if(copies == entry->second.end()) {
          return none;
        }
        return copies->second;
      }

      NodeId find_tree(const NodeId& id) const {
        auto current = id;
        for(auto depth = 0; depth != MAXIMUM_DEPTH; ++depth) {
          auto node = m_document->find(current);
          if(!node) {
            return NodeId();
          }
          if(node->m_parent.is_nil()) {
            return current;
          }
          current = node->m_parent;
        }
        return NodeId();
      }

      void propagate(const Event& event) {
        auto tree = find_tree(event.m_id);
        if(tree.is_nil()) {
          return;
        }
        for(auto& dependent : get_dependents(tree)) {
          auto copies = get_copies(dependent, event.m_id);
          for(auto& copy : copies) {
            sync_copy(dependent, copy, event.m_id, event.m_mask);
          }
        }
      }

      void sync_copy(const Context& context, const NodeId& id,
          const NodeId& source, ChangeMask mask) {
        auto node = m_document->find(id);
        if(!node || !m_document->find(source)) {
          return;
        }
        auto is_changed = false;
        if(is_set(mask, PROPERTY_CHANGES) && !node->get_reference()) {
          auto values = resolve(*m_document, source);
          auto target = *node;
          if(values && assign(target, *values, mask & PROPERTY_CHANGES,
              id == context.m_root, get_overridden(context, id), false)) {
            write(*m_document, *m_transaction, id, target);
            is_changed = true;
          }
        }
        if(is_set(mask, ChangeMask::CHILDREN)) {
          is_changed = sync_structure(context, id, source) || is_changed;
        }
        if(is_changed && context.m_kind == ContextKind::STATE) {
          m_sources.push_back(Event(id, mask));
        }
      }

      bool sync_structure(const Context& context, const NodeId& id,
          const NodeId& source) {
        auto node = m_document->find(id);
        if(!node) {
          return false;
        }
        if(auto reference = node->get_reference();
            reference && !reference->m_has_children) {
          return false;
        }
        auto list = get_source_children(*m_document, source);
        if(!list) {
          return false;
        }
        auto source_children = *list;
        auto overrides = Overrides();
        if(auto recorded = get_overrides(context)) {
          overrides = *recorded;
        }
        auto removed = std::unordered_set<NodeId>(
          overrides.m_removed.begin(), overrides.m_removed.end());
        auto added = std::unordered_set<NodeId>(
          overrides.m_added.begin(), overrides.m_added.end());
        auto is_changed = false;
        auto sources = std::unordered_set<NodeId>(
          source_children.begin(), source_children.end());
        auto tree = find_tree(source);
        for(auto& child : std::vector<NodeId>(node->m_children)) {
          auto child_source = get_source(context, child);
          if(!child_source.is_nil() && !sources.contains(child_source) &&
              !added.contains(child) && (!m_document->find(child_source) ||
                find_tree(child_source) == tree)) {
            m_transaction->remove(child);
            is_changed = true;
          }
        }
        auto copies = std::unordered_map<NodeId, NodeId>();
        for(auto& child : m_document->get(id).m_children) {
          auto child_source = get_source(context, child);
          if(!child_source.is_nil()) {
            copies.emplace(child_source, child);
          }
        }
        auto inserted = std::map<NodeId, NodeId>();
        auto previous = NodeId();
        for(auto& child : source_children) {
          if(auto copy = copies.find(child); copy != copies.end()) {
            previous = copy->second;
            continue;
          }
          if(removed.contains(child)) {
            continue;
          }
          auto& children = m_document->get(id).m_children;
          auto index = std::size_t(0);
          if(!previous.is_nil()) {
            index = static_cast<std::size_t>(std::find(
              children.begin(), children.end(), previous) -
              children.begin()) + 1;
          }
          auto copy = NodeId();
          if(can_contain(m_document->get(id).m_payload, NodeKind::REFERENCE)) {
            auto header = NodeHeader();
            header.m_id = generate_node_id();
            copy = m_transaction->insert(
              id, index, std::move(header), Reference(child));
          } else {
            auto mapping = std::unordered_map<NodeId, NodeId>();
            copy = clone_subtree(
              *m_document, *m_transaction, child, id, index, mapping);
          }
          inserted.emplace(copy, child);
          previous = copy;
          is_changed = true;
        }
        if(!inserted.empty() || is_changed) {
          edit_sync_map(context, [&] (auto& sync_map) {
            for(auto& entry : inserted) {
              sync_map.insert_or_assign(entry.first, entry.second);
            }
            std::erase_if(sync_map, [&] (const auto& entry) {
              return !m_document->find(entry.first);
            });
          });
        }
        if(overrides.m_reordered.empty()) {
          is_changed = reorder(context, id, source_children) || is_changed;
        }
        if(is_changed) {
          m_inverses.erase(context.m_root);
        }
        return is_changed;
      }

      bool reorder(const Context& context, const NodeId& id,
          const std::vector<NodeId>& source_children) {
        auto children = m_document->get(id).m_children;
        auto ranks = std::unordered_map<NodeId, std::size_t>();
        for(auto i = std::size_t(0); i != source_children.size(); ++i) {
          ranks.emplace(source_children[i], i);
        }
        auto slots = std::vector<std::size_t>();
        auto mapped = std::vector<NodeId>();
        for(auto i = std::size_t(0); i != children.size(); ++i) {
          auto source = get_source(context, children[i]);
          if(ranks.contains(source)) {
            slots.push_back(i);
            mapped.push_back(children[i]);
          }
        }
        auto sorted = mapped;
        std::stable_sort(sorted.begin(), sorted.end(),
          [&] (const auto& left, const auto& right) {
            return ranks.at(get_source(context, left)) <
              ranks.at(get_source(context, right));
          });
        if(sorted == mapped) {
          return false;
        }
        auto order = children;
        for(auto i = std::size_t(0); i != slots.size(); ++i) {
          order[slots[i]] = sorted[i];
        }
        for(auto i = std::size_t(0); i != order.size(); ++i) {
          if(m_document->get(id).m_children[i] != order[i]) {
            m_transaction->move(order[i], id, i);
          }
        }
        return true;
      }

      std::vector<NodeId> get_trees(const ComponentId& id) const {
        auto trees = std::vector<NodeId>();
        if(auto component = find_component(id)) {
          trees.push_back(component->m_master);
          for(auto& state : component->m_states) {
            trees.push_back(state.m_tree);
          }
        }
        return trees;
      }

      std::optional<ComponentId> find_tree_component(
          const NodeId& root) const {
        if(auto master = m_masters.find(root); master != m_masters.end()) {
          return master->second;
        } else if(auto state = m_states.find(root); state != m_states.end()) {
          return state->second.first;
        }
        return std::nullopt;
      }

      std::vector<Interaction> get_owned(const NodeId& owner) const {
        auto owned = std::vector<Interaction>();
        for(auto& interaction : m_document->get_prototype().m_interactions) {
          if(interaction.m_owner == owner) {
            owned.push_back(interaction);
          }
        }
        return owned;
      }

      const Interaction* find_interaction(const InteractionId& id) const {
        for(auto& interaction : m_document->get_prototype().m_interactions) {
          if(interaction.m_id == id) {
            return &interaction;
          }
        }
        return nullptr;
      }

      bool push_main_interaction(const InteractionId& id,
          std::unordered_set<ComponentId>& components) {
        auto interaction = find_interaction(id);
        if(!interaction) {
          return false;
        }
        auto copy = *interaction;
        auto node = m_document->find(copy.m_owner);
        auto role = node ? get_instance_role(*node) : nullptr;
        if(!role || !role->m_is_main ||
            is_linked(*m_document, role->m_component)) {
          return false;
        }
        auto tree = get_source_tree(*m_document, *role);
        if(tree.is_nil() || !m_document->find(tree)) {
          return false;
        }
        components.insert(role->m_component);
        for(auto& source : get_owned(tree)) {
          if(derive_inherited_id(source.m_id, copy.m_owner) != copy.m_id) {
            continue;
          }
          if(source.m_trigger != copy.m_trigger ||
              source.m_action != copy.m_action) {
            source.m_trigger = copy.m_trigger;
            source.m_action = copy.m_action;
            m_transaction->replace_interaction(std::move(source));
          }
          return true;
        }
        for(auto& other : get_trees(role->m_component)) {
          for(auto& source : get_owned(other)) {
            if(derive_inherited_id(source.m_id, copy.m_owner) == copy.m_id) {
              return true;
            }
          }
        }
        auto source = copy;
        source.m_id = generate_node_id();
        source.m_owner = tree;
        m_transaction->remove_interaction(copy.m_id);
        m_transaction->add_interaction(std::move(source));
        return true;
      }

      std::optional<InteractionId> remove_main_interaction(
          const InteractionId& id,
          std::unordered_set<ComponentId>& components) {
        for(auto& entry : *m_instances) {
          for(auto& instance : entry.second) {
            auto node = m_document->find(instance);
            auto role = node ? get_instance_role(*node) : nullptr;
            if(!role || !role->m_is_main ||
                is_linked(*m_document, role->m_component)) {
              continue;
            }
            auto tree = get_source_tree(*m_document, *role);
            for(auto& source : get_owned(tree)) {
              if(derive_inherited_id(source.m_id, instance) == id) {
                m_transaction->remove_interaction(source.m_id);
                components.insert(role->m_component);
                return source.m_id;
              }
            }
          }
        }
        return std::nullopt;
      }

      void inherit_interactions(const NodeId& instance) {
        auto node = m_document->find(instance);
        auto role = node ? get_instance_role(*node) : nullptr;
        if(!role) {
          return;
        }
        auto tree = get_source_tree(*m_document, *role);
        auto kept = std::unordered_set<InteractionId>();
        for(auto& source : get_owned(tree)) {
          auto copy = source;
          copy.m_id = derive_inherited_id(source.m_id, instance);
          copy.m_owner = instance;
          kept.insert(copy.m_id);
          if(auto existing = find_interaction(copy.m_id)) {
            copy.m_is_enabled = existing->m_is_enabled;
            if(*existing != copy) {
              m_transaction->replace_interaction(std::move(copy));
            }
          } else {
            m_transaction->add_interaction(std::move(copy));
          }
        }
        for(auto& other : get_trees(role->m_component)) {
          if(other == tree) {
            continue;
          }
          for(auto& source : get_owned(other)) {
            auto id = derive_inherited_id(source.m_id, instance);
            if(!kept.contains(id) && find_interaction(id)) {
              m_transaction->remove_interaction(id);
            }
          }
        }
      }

      void sync_interactions(const ChangeSet& changes) {
        auto components = std::unordered_set<ComponentId>();
        auto instances = std::vector<NodeId>();
        for(auto& id : changes.m_inserted) {
          if(auto node = m_document->find(id);
              node && get_instance_role(*node)) {
            instances.push_back(id);
          }
        }
        for(auto& change : changes.m_modified) {
          if(is_set(change.m_mask, ChangeMask::ROLES)) {
            if(auto node = m_document->find(change.m_id);
                node && get_instance_role(*node)) {
              instances.push_back(change.m_id);
            }
          }
        }
        if(changes.m_interactions.empty() && instances.empty()) {
          return;
        }
        get_instances(ComponentId());
        auto removed = std::vector<InteractionId>();
        for(auto& id : changes.m_interactions) {
          if(find_interaction(id)) {
            if(!push_main_interaction(id, components)) {
              if(auto interaction = find_interaction(id)) {
                if(auto component =
                    find_tree_component(interaction->m_owner)) {
                  components.insert(*component);
                }
              }
            }
          } else {
            removed.push_back(id);
            if(auto source = remove_main_interaction(id, components)) {
              removed.push_back(*source);
            }
          }
        }
        if(!removed.empty()) {
          for(auto& entry : *m_instances) {
            for(auto& instance : entry.second) {
              for(auto& id : removed) {
                auto copy = derive_inherited_id(id, instance);
                if(find_interaction(copy)) {
                  m_transaction->remove_interaction(copy);
                }
              }
            }
          }
        }
        for(auto& component : components) {
          auto& list = get_instances(component);
          instances.insert(instances.end(), list.begin(), list.end());
        }
        auto visited = std::unordered_set<NodeId>();
        for(auto& instance : instances) {
          if(visited.insert(instance).second) {
            inherit_interactions(instance);
          }
        }
      }
  };
}

InteractionId Rellu::derive_inherited_id(
    const InteractionId& source, const NodeId& owner) {
  auto hash = [] (std::string_view text, std::uint64_t seed) {
    for(auto character : text) {
      seed ^= static_cast<unsigned char>(character);
      seed *= 0x100000001B3ULL;
    }
    return seed;
  };
  auto text = to_string(source);
  auto qualifier = to_string(owner);
  auto high = hash(qualifier, hash(text, 0xCBF29CE484222325ULL) ^ 0x2F);
  auto low = hash(qualifier, hash(text, 0x84222325CBF29CE4ULL) ^ 0x5D);
  auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
  for(auto i = std::size_t(0); i != 8; ++i) {
    bytes[i] = static_cast<std::uint8_t>(high >> (56 - 8 * i));
    bytes[i + 8] = static_cast<std::uint8_t>(low >> (56 - 8 * i));
  }
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x80);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
  return NodeId(bytes);
}

bool Rellu::refresh_components(Document& document, ChangeSet& changes) {
  if(document.get_resources().m_components.empty() ||
      changes.m_modified.empty() && changes.m_inserted.empty() &&
        changes.m_interactions.empty()) {
    return false;
  }
  auto captured = std::optional<ChangeSet>();
  {
    auto transaction = Transaction(document);
    auto refresh = Refresh(document, transaction);
    refresh.run(changes);
    auto subscription = document.subscribe([&] (const ChangeSet& published) {
      captured = published;
    });
    transaction.amend();
  }
  if(!captured || captured->is_empty()) {
    return false;
  }
  changes.m_inserted.insert(changes.m_inserted.end(),
    captured->m_inserted.begin(), captured->m_inserted.end());
  changes.m_removed.insert(changes.m_removed.end(),
    captured->m_removed.begin(), captured->m_removed.end());
  changes.m_moved.insert(changes.m_moved.end(), captured->m_moved.begin(),
    captured->m_moved.end());
  changes.m_modified.insert(changes.m_modified.end(),
    captured->m_modified.begin(), captured->m_modified.end());
  changes.m_components.insert(changes.m_components.end(),
    captured->m_components.begin(), captured->m_components.end());
  changes.m_interactions.insert(changes.m_interactions.end(),
    captured->m_interactions.begin(), captured->m_interactions.end());
  return true;
}

bool Rellu::refresh_component_sizes(Document& document,
    const ChangeSet& changes, const TextMeasurer& measurer) {
  auto& components = document.get_resources().m_components;
  if(components.empty()) {
    return false;
  }
  auto masters = std::unordered_map<NodeId, ComponentId>();
  for(auto& entry : components) {
    masters.emplace(entry.second.m_master, entry.first);
  }
  auto touched = std::vector<NodeId>(
    changes.m_inserted.begin(), changes.m_inserted.end());
  touched.insert(touched.end(), changes.m_moved.begin(), changes.m_moved.end());
  for(auto& change : changes.m_modified) {
    touched.push_back(change.m_id);
  }
  auto visited = std::unordered_set<NodeId>();
  auto instances = std::vector<NodeId>();
  auto roots = std::vector<NodeId>();
  for(auto& id : touched) {
    auto current = id;
    for(auto depth = 0; depth != MAXIMUM_DEPTH && !current.is_nil(); ++depth) {
      if(!visited.insert(current).second) {
        break;
      }
      auto node = document.find(current);
      if(!node) {
        break;
      }
      if(get_instance_role(*node)) {
        instances.push_back(current);
      }
      if(node->m_parent.is_nil() && masters.contains(current)) {
        roots.push_back(current);
      }
      current = node->m_parent;
    }
  }
  if(instances.empty() && roots.empty()) {
    return false;
  }
  auto is_changed = false;
  {
    auto transaction = Transaction(document);
    auto measure = [&] (const NodeId& id) -> std::optional<Size> {
      if(auto bounds =
          get_local_bounds(document, id, BoundsType::GEOMETRY, measurer)) {
        return Size(bounds->m_width, bounds->m_height);
      }
      return std::nullopt;
    };
    for(auto& id : instances) {
      auto size = measure(id);
      auto& node = document.get(id);
      if(!size || *size == get_instance_role(node)->m_size) {
        continue;
      }
      auto group = *node.get_group();
      group.m_component_instance->m_size = *size;
      transaction.replace_payload(id, std::move(group));
      is_changed = true;
    }
    for(auto& id : roots) {
      auto size = measure(id);
      auto component = components.at(masters.at(id));
      if(!size || *size == component.m_size) {
        continue;
      }
      component.m_size = *size;
      transaction.replace_component(std::move(component));
      is_changed = true;
    }
    if(is_changed) {
      transaction.amend();
    }
  }
  return is_changed;
}

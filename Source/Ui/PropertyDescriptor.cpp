#include <algorithm>
#include <cmath>
#include <concepts>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"

using namespace Rellu;

namespace {
  constexpr auto CONTENT_LENGTH = 80;

  const auto BLEND_MODES = QStringList({"Normal", "Multiply", "Screen",
    "Overlay", "Darken", "Lighten", "Color Dodge", "Color Burn", "Hard Light",
    "Soft Light", "Difference", "Exclusion", "Hue", "Saturation", "Color",
    "Luminosity"});
  const auto FILL_TYPES = QStringList({"None", "Solid", "Gradient", "Image"});
  const auto GRADIENT_TYPES = QStringList({"Linear", "Radial"});
  const auto GRADIENT_UNITS =
    QStringList({"Object Bounding Box", "User Space"});
  const auto SCALE_BEHAVIORS = QStringList({"Fill", "Cover"});
  const auto STROKE_ALIGNMENTS = QStringList({"Center", "Inside", "Outside"});
  const auto STROKE_CAPS = QStringList({"Butt", "Round", "Square"});
  const auto STROKE_JOINS = QStringList({"Miter", "Round", "Bevel"});
  const auto TEXT_ALIGNMENTS = QStringList({"Left", "Center", "Right"});
  const auto TEXT_TRANSFORMS =
    QStringList({"None", "Uppercase", "Lowercase", "Title Case"});
  const auto TEXT_SCRIPTS = QStringList({"Normal", "Superscript", "Subscript"});
  const auto TEXT_FRAMES = QStringList({"Positioned", "Area", "Auto Height"});
  const auto SCROLL_DIRECTIONS =
    QStringList({"Horizontal", "Vertical", "Panning"});
  const auto ORIENTATIONS = QStringList({"Horizontal", "Vertical"});
  const auto GRID_TYPES = QStringList({"Square", "Layout"});
  const auto COMPOUND_OPERATIONS =
    QStringList({"Union", "Subtract", "Intersect", "Exclude"});
  const auto FILL_RULES = QStringList({"Non Zero", "Even Odd"});

  QString format_number(double value) {
    if(std::abs(value) < 0.005) {
      return "0";
    }
    auto text = QString::number(value, 'f', 2);
    while(text.endsWith('0')) {
      text.chop(1);
    }
    if(text.endsWith('.')) {
      text.chop(1);
    }
    return text;
  }

  Choice make_choice(auto value, const QStringList& options) {
    return Choice(static_cast<int>(value), options);
  }

  PropertyDescriptor make_property(
      std::string path, QString label, PropertyValue value = PropertyValue()) {
    return PropertyDescriptor(std::move(path), std::move(label),
      std::move(value), std::vector<PropertyDescriptor>());
  }

  void add(PropertyDescriptor& parent, const std::string& name, QString label,
      PropertyValue value = PropertyValue()) {
    parent.m_children.push_back(make_property(
      parent.m_path + '.' + name, std::move(label), std::move(value)));
  }

  PropertyDescriptor& add_heading(
      PropertyDescriptor& parent, const std::string& name, QString label,
      PropertyValue value = PropertyValue()) {
    add(parent, name, std::move(label), std::move(value));
    return parent.m_children.back();
  }

  QString to_qstring(const std::string& value) {
    return QString::fromStdString(value);
  }

  QString format_margins(const Margins& margins) {
    return QString("%1, %2, %3, %4").arg(format_number(margins.m_top),
      format_number(margins.m_right), format_number(margins.m_bottom),
      format_number(margins.m_left));
  }

  QString format_font(const Font& font) {
    if(font.m_style.empty()) {
      return to_qstring(font.m_family);
    }
    return QString("%1 %2").arg(
      to_qstring(font.m_family), to_qstring(font.m_style));
  }

  struct Placement {
    const Node* m_node;
    Matrix m_transform;
    const Node* m_children;
  };

  std::optional<Placement> get_placement(
      const Document& document, const Node& node) {
    if(node.get_kind() != NodeKind::REFERENCE) {
      return Placement(&node, node.m_header.m_transform, &node);
    }
    auto resolution = resolve_reference(document.get_nodes(), node.get_id());
    if(!resolution) {
      return std::nullopt;
    }
    auto source = document.find(resolution->m_source);
    auto children = document.find(resolution->m_child_source);
    if(!source || !children) {
      return std::nullopt;
    }
    return Placement(source, resolution->m_transform, children);
  }

  PropertyDescriptor make_layer(
      const Document& document, const Node& node, const Node& displayed) {
    auto section = make_property("layer", "Layer");
    add(section, "name", "Name", get_display_name(document, node));
    auto kind = get_type_name(displayed);
    if(node.get_kind() == NodeKind::REFERENCE) {
      kind += " (reference)";
    }
    add(section, "kind", "Kind", kind);
    add(section, "id", "Id", to_qstring(to_string(node.get_id())));
    add(section, "visible", "Visible", displayed.m_header.m_is_visible);
    add(section, "locked", "Locked", displayed.m_header.m_is_locked);
    add(section, "export", "Marked for Export",
      displayed.m_header.m_is_marked_for_export);
    if(!node.m_children.empty() || displayed.get_group() ||
        displayed.get_artboard()) {
      add(section, "children", "Children",
        static_cast<int>(node.m_children.size()));
    }
    return section;
  }

  std::optional<Rect> get_node_frame(
      const Document& document, const Node& node, const Matrix& transform,
      const TextMeasurer& measurer) {
    if(auto artboard = node.get_artboard()) {
      return artboard->m_frame;
    }
    auto bounds = Rellu::get_local_bounds(
      document, node.get_id(), BoundsType::GEOMETRY, measurer);
    if(!bounds) {
      return std::nullopt;
    }
    auto origin = transform.map(bounds->get_top_left());
    return Rect(origin.m_x, origin.m_y, bounds->m_width, bounds->m_height);
  }

  PropertyDescriptor make_transform(const Document& document,
      const Node& node, const Placement& placement,
      const TextMeasurer& measurer) {
    auto section = make_property("transform", "Transform");
    auto& transform = placement.m_transform;
    auto frame = get_node_frame(document, node, transform, measurer);
    if(frame && node.get_artboard()) {
      add(section, "x", "X", frame->m_x);
      add(section, "y", "Y", frame->m_y);
      add(section, "width", "Width", frame->m_width);
      add(section, "height", "Height", frame->m_height);
      return section;
    }
    auto origin = frame ? frame->get_top_left() : transform.map(Point(0, 0));
    add(section, "x", "X", origin.m_x);
    add(section, "y", "Y", origin.m_y);
    if(frame) {
      add(section, "width", "Width", frame->m_width);
      add(section, "height", "Height", frame->m_height);
    }
    auto scale_x = std::hypot(transform.m_a, transform.m_b);
    auto rotation = std::atan2(transform.m_b, transform.m_a) *
      180 / std::numbers::pi;
    add(section, "rotation", "Rotation", rotation == 0 ? 0.0 : rotation);
    auto scale_y = scale_x == 0 ? 0.0 :
      transform.get_determinant() / scale_x;
    if(scale_x != 1 || scale_y != 1) {
      add(section, "scale", "Scale", Point(scale_x, scale_y));
    }
    return section;
  }

  PropertyDescriptor make_appearance(const Node& node) {
    auto section = make_property("appearance", "Appearance");
    add(section, "opacity", "Opacity", node.m_header.m_opacity);
    add(section, "blend_mode", "Blend Mode",
      make_choice(node.m_header.m_blend_mode, BLEND_MODES));
    add(section, "isolated", "Isolated", node.m_header.m_is_isolated);
    return section;
  }

  PropertyDescriptor make_fill(
      const Document& document, const Fill& fill, QString label) {
    auto section = make_property("fill", std::move(label),
      make_choice(fill.index(), FILL_TYPES));
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      add(section, "color", "Color", solid->m_color);
      if(!solid->m_swatch.is_nil()) {
        auto& swatches = document.get_resources().m_swatches;
        auto swatch = std::find_if(swatches.begin(), swatches.end(),
          [&] (const auto& swatch) {
            return swatch.m_id == solid->m_swatch;
          });
        if(swatch != swatches.end()) {
          add(section, "swatch", "Swatch", to_qstring(swatch->m_name));
        }
      }
    } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
      add(section, "gradient_type", "Gradient",
        make_choice(gradient->m_geometry.index(), GRADIENT_TYPES));
      add(section, "units", "Units",
        make_choice(gradient->m_units, GRADIENT_UNITS));
      auto& stops = add_heading(section, "stops", "Stops",
        static_cast<int>(gradient->m_stops.size()));
      for(auto i = std::size_t(0); i != gradient->m_stops.size(); ++i) {
        auto& stop = gradient->m_stops[i];
        add(stops, std::to_string(i),
          QString("At %1").arg(format_number(stop.m_offset)), stop.m_color);
      }
    } else if(auto image = std::get_if<ImageFill>(&fill)) {
      add(section, "image", "Image", to_qstring(to_string(image->m_image)));
      add(section, "size", "Size", image->m_size);
      add(section, "scale_behavior", "Scaling",
        make_choice(image->m_scale_behavior, SCALE_BEHAVIORS));
    }
    return section;
  }

  PropertyDescriptor make_stroke(const std::optional<Stroke>& stroke) {
    if(!stroke) {
      return make_property("stroke", "Stroke", QString("None"));
    }
    auto section = make_property("stroke", "Stroke");
    add(section, "enabled", "Enabled", stroke->m_is_enabled);
    add(section, "color", "Color", stroke->m_color);
    add(section, "width", "Width", stroke->m_width);
    add(section, "alignment", "Alignment",
      make_choice(stroke->m_alignment, STROKE_ALIGNMENTS));
    add(section, "cap", "Cap", make_choice(stroke->m_cap, STROKE_CAPS));
    add(section, "join", "Join", make_choice(stroke->m_join, STROKE_JOINS));
    add(section, "miter_limit", "Miter Limit", stroke->m_miter_limit);
    if(!stroke->m_dashes.empty()) {
      auto dashes = QStringList();
      for(auto dash : stroke->m_dashes) {
        dashes.push_back(format_number(dash));
      }
      add(section, "dashes", "Dashes", dashes.join(", "));
    }
    return section;
  }

  void add_shadow(PropertyDescriptor& effect, const auto& shadow) {
    add(effect, "visible", "Visible", shadow.m_is_visible);
    add(effect, "offset", "Offset", shadow.m_offset);
    add(effect, "blur", "Blur", shadow.m_blur);
    add(effect, "color", "Color", shadow.m_color);
  }

  PropertyDescriptor make_effects(const std::vector<Effect>& effects) {
    auto section = make_property(
      "effects", "Effects", static_cast<int>(effects.size()));
    for(auto i = std::size_t(0); i != effects.size(); ++i) {
      auto name = std::to_string(i);
      if(auto shadow = std::get_if<DropShadow>(&effects[i])) {
        add_shadow(add_heading(section, name, "Drop Shadow"), *shadow);
      } else if(auto shadow = std::get_if<InnerShadow>(&effects[i])) {
        add_shadow(add_heading(section, name, "Inner Shadow"), *shadow);
      } else if(auto blur = std::get_if<Blur>(&effects[i])) {
        auto& effect = add_heading(section, name,
          blur->m_is_background ? "Background Blur" : "Object Blur");
        add(effect, "visible", "Visible", blur->m_is_visible);
        add(effect, "amount", "Amount", blur->m_amount);
        add(effect, "brightness", "Brightness", blur->m_brightness);
        if(blur->m_is_background) {
          add(effect, "fill_opacity", "Fill Opacity", blur->m_fill_opacity);
        }
      }
    }
    return section;
  }

  PropertyDescriptor make_geometry(const Shape& shape) {
    auto section = make_property("geometry", "Geometry");
    std::visit([&] <typename T> (const T& geometry) {
      if constexpr(std::same_as<T, RectangleShape>) {
        auto& radii = geometry.m_radii;
        auto& corners = add_heading(section, "radii", "Corner Radii");
        add(corners, "top_left", "Top Left", radii.m_top_left);
        add(corners, "top_right", "Top Right", radii.m_top_right);
        add(corners, "bottom_right", "Bottom Right", radii.m_bottom_right);
        add(corners, "bottom_left", "Bottom Left", radii.m_bottom_left);
      } else if constexpr(std::same_as<T, EllipseShape>) {
        add(section, "center", "Center", geometry.m_center);
        add(section, "radii", "Radii",
          Size(geometry.m_x_radius, geometry.m_y_radius));
      } else if constexpr(std::same_as<T, LineShape>) {
        add(section, "start", "Start", geometry.m_start);
        add(section, "end", "End", geometry.m_end);
      } else if constexpr(std::same_as<T, PolygonShape>) {
        add(section, "corners", "Corners", geometry.m_corner_count);
        add(section, "corner_radius", "Corner Radius",
          geometry.m_corner_radius);
      } else if constexpr(std::same_as<T, PathShape>) {
        add(section, "fill_rule", "Fill Rule",
          make_choice(geometry.m_fill_rule, FILL_RULES));
      } else if constexpr(std::same_as<T, CompoundShape>) {
        add(section, "operation", "Operation",
          make_choice(geometry.m_operation, COMPOUND_OPERATIONS));
      }
    }, shape.m_geometry);
    return section;
  }

  bool is_same_font(const TextStyle& left, const TextStyle& right) {
    return left.m_font.m_family == right.m_font.m_family &&
      left.m_font.m_style == right.m_font.m_style &&
      left.m_font.m_size == right.m_font.m_size;
  }

  PropertyDescriptor make_text(const Text& text) {
    auto section = make_property("text", "Text");
    auto content = QString::fromStdU16String(*text.m_content);
    auto length = static_cast<int>(content.size());
    if(content.size() > CONTENT_LENGTH) {
      content.truncate(CONTENT_LENGTH);
      content += "...";
    }
    content.replace(QChar('\r'), QChar(' ')).replace(QChar('\n'), QChar(' '));
    add(section, "content", "Content", content);
    add(section, "length", "Characters", length);
    auto& style = text.m_runs.empty() ? text.m_base_style :
      text.m_runs.front().m_style;
    auto is_mixed = std::any_of(text.m_runs.begin(), text.m_runs.end(),
      [&] (const auto& run) {
        return !is_same_font(run.m_style, style);
      });
    auto font = format_font(style.m_font);
    if(is_mixed) {
      font += " (mixed)";
    }
    add(section, "font", "Font", font);
    add(section, "size", "Size", style.m_font.m_size);
    add(section, "color", "Color", style.m_color);
    add(section, "letter_spacing", "Letter Spacing", style.m_letter_spacing);
    add(section, "transform", "Case",
      make_choice(style.m_transform, TEXT_TRANSFORMS));
    add(section, "script", "Script", make_choice(style.m_script, TEXT_SCRIPTS));
    add(section, "runs", "Styled Runs", static_cast<int>(text.m_runs.size()));
    auto& paragraph = text.m_paragraph_style;
    add(section, "alignment", "Alignment",
      make_choice(paragraph.m_alignment, TEXT_ALIGNMENTS));
    if(paragraph.m_line_height) {
      add(section, "line_height", "Line Height", *paragraph.m_line_height);
    } else {
      add(section, "line_height", "Line Height", QString("Auto"));
    }
    add(section, "paragraph_spacing", "Paragraph Spacing",
      paragraph.m_paragraph_spacing);
    add(section, "frame", "Frame", make_choice(text.m_frame.index(),
      TEXT_FRAMES));
    if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
      add(section, "frame_size", "Frame Size", area->m_size);
    } else if(auto auto_height = std::get_if<AutoHeightFrame>(&text.m_frame)) {
      add(section, "frame_width", "Frame Width", auto_height->m_width);
    }
    add(section, "stored_layout", "Stored Layout",
      static_cast<bool>(text.m_layout));
    return section;
  }

  PropertyDescriptor make_artboard(const Artboard& artboard) {
    auto section = make_property("artboard", "Artboard");
    if(artboard.m_viewport_height) {
      add(section, "viewport_height", "Viewport Height",
        *artboard.m_viewport_height);
    }
    if(auto& grid = artboard.m_layout_grid) {
      auto& heading = add_heading(section, "grid", "Grid",
        make_choice(grid->m_type, GRID_TYPES));
      add(heading, "visible", "Visible", grid->m_is_visible);
      if(grid->m_type == GridType::LAYOUT) {
        add(heading, "columns", "Columns", grid->m_column_count);
        add(heading, "gutter", "Gutter", grid->m_gutter);
        add(heading, "margins", "Margins", format_margins(grid->m_margins));
      } else {
        add(heading, "size", "Size", grid->m_square_size);
      }
    }
    add(section, "guides", "Guides",
      static_cast<int>(artboard.m_guides.size()));
    return section;
  }

  PropertyDescriptor make_roles(const Document& document, const Group& group) {
    auto section = make_property("roles", "Roles");
    if(auto& instance = group.m_component_instance) {
      auto& heading = add_heading(section, "component", "Component");
      auto& components = document.get_resources().m_components;
      auto component = components.find(instance->m_component);
      if(component != components.end()) {
        add(heading, "name", "Name", to_qstring(component->second.m_name));
        auto state = QString("Default");
        for(auto& candidate : component->second.m_states) {
          if(candidate.m_id == instance->m_state) {
            state = to_qstring(candidate.m_name);
          }
        }
        add(heading, "state", "State", state);
      } else {
        add(heading, "name", "Name", QString("Missing"));
      }
      add(heading, "size", "Size", instance->m_size);
    }
    if(auto& grid = group.m_repeat_grid) {
      auto& heading = add_heading(section, "repeat_grid", "Repeat Grid");
      add(heading, "columns", "Columns", grid->m_column_count);
      add(heading, "rows", "Rows", grid->m_row_count);
      add(heading, "cell_size", "Cell Size", grid->m_cell_size);
      add(heading, "column_spacing", "Column Spacing", grid->m_column_spacing);
      add(heading, "row_spacing", "Row Spacing", grid->m_row_spacing);
    }
    if(auto& scroll = group.m_scroll_group) {
      auto& heading = add_heading(section, "scroll_group", "Scroll Group");
      add(heading, "direction", "Direction",
        make_choice(scroll->m_direction, SCROLL_DIRECTIONS));
      add(heading, "viewport", "Viewport", scroll->m_viewport);
      add(heading, "offset", "Offset", scroll->m_offset);
    }
    if(auto& stack = group.m_stack) {
      auto& heading = add_heading(section, "stack", "Stack");
      add(heading, "orientation", "Orientation",
        make_choice(stack->m_orientation, ORIENTATIONS));
      add(heading, "padding", "Padding", format_margins(stack->m_padding));
      add(heading, "background", "Background", stack->m_has_background);
    }
    if(auto& padding = group.m_padding) {
      auto& heading = add_heading(section, "padding", "Padding");
      add(heading, "padding", "Padding", format_margins(padding->m_padding));
      add(heading, "content_aware", "Content Aware",
        padding->m_is_content_aware);
      add(heading, "background", "Background", padding->m_has_background);
    }
    if(auto& mask = group.m_mask) {
      auto& heading = add_heading(section, "mask", "Mask");
      add(heading, "shapes", "Shapes", static_cast<int>(mask->m_shapes.size()));
    }
    if(section.m_children.empty()) {
      section.m_value = QString("None");
    }
    return section;
  }

  PropertyDescriptor make_reference(
      const Document& document, const Node& node, const Node* displayed) {
    auto& reference = *node.get_reference();
    auto section = make_property("reference", "Reference");
    if(auto source = document.find(reference.m_source)) {
      add(section, "source", "Source", get_display_name(document, *source));
    } else {
      add(section, "source", "Source", QString("Missing"));
    }
    add(section, "source_id", "Source Id",
      to_qstring(to_string(reference.m_source)));
    add(section, "resolved", "Resolved", static_cast<bool>(displayed));
    add(section, "transform", "Overrides Transform",
      static_cast<bool>(reference.m_transform));
    add(section, "children", "Own Children", reference.m_has_children);
    return section;
  }

  int to_byte(double component) {
    return std::clamp(static_cast<int>(std::lround(component * 255)), 0, 255);
  }

  void intersect_properties(std::vector<PropertyDescriptor>& properties,
      const std::vector<PropertyDescriptor>& others) {
    std::erase_if(properties, [&] (auto& property) {
      auto other = std::find_if(others.begin(), others.end(),
        [&] (const auto& other) {
          return other.m_path == property.m_path;
        });
      if(other == others.end()) {
        return true;
      }
      if(other->m_is_mixed || !(property.m_value == other->m_value)) {
        property.m_is_mixed = true;
      }
      intersect_properties(property.m_children, other->m_children);
      return false;
    });
  }

  bool is_frame_property(std::string_view path) {
    return path == "transform.x" || path == "transform.y" ||
      path == "transform.width" || path == "transform.height";
  }

  double get_frame_value(const Rect& frame, std::string_view path) {
    if(path == "transform.x") {
      return frame.m_x;
    } else if(path == "transform.y") {
      return frame.m_y;
    } else if(path == "transform.width") {
      return frame.m_width;
    }
    return frame.m_height;
  }

  void set_frame(
      PropertyDescriptor& section, const std::optional<Rect>& frame) {
    std::erase_if(section.m_children, [&] (auto& property) {
      if(!is_frame_property(property.m_path)) {
        return false;
      } else if(!frame) {
        return true;
      }
      property.m_value = get_frame_value(*frame, property.m_path);
      property.m_is_mixed = false;
      return false;
    });
  }
}

std::vector<PropertyDescriptor> Rellu::extract_properties(
    const Document& document, const NodeId& id, const TextMeasurer& measurer) {
  auto sections = std::vector<PropertyDescriptor>();
  auto node = document.find(id);
  if(!node) {
    return sections;
  }
  auto placement = get_placement(document, *node);
  auto displayed = placement ? placement->m_node : node;
  sections.push_back(make_layer(document, *node, *displayed));
  if(placement) {
    sections.push_back(
      make_transform(document, *node, *placement, measurer));
  }
  sections.push_back(make_appearance(*displayed));
  if(auto shape = displayed->get_shape()) {
    sections.push_back(make_geometry(*shape));
    sections.push_back(make_fill(document, shape->m_style.m_fill, "Fill"));
    sections.push_back(make_stroke(shape->m_style.m_stroke));
    sections.push_back(make_effects(shape->m_style.m_effects));
  } else if(auto text = displayed->get_text()) {
    sections.push_back(make_text(*text));
    sections.push_back(make_fill(document, text->m_style.m_fill, "Fill"));
    sections.push_back(make_stroke(text->m_style.m_stroke));
    sections.push_back(make_effects(text->m_style.m_effects));
  } else if(auto artboard = displayed->get_artboard()) {
    sections.push_back(make_artboard(*artboard));
    sections.push_back(
      make_fill(document, artboard->m_background, "Background"));
  } else if(auto group = displayed->get_group()) {
    sections.push_back(make_roles(document, *group));
  }
  if(node->get_kind() == NodeKind::REFERENCE) {
    sections.push_back(make_reference(
      document, *node, placement ? placement->m_node : nullptr));
  }
  return sections;
}

std::vector<PropertyDescriptor> Rellu::extract_common_properties(
    const Document& document, const std::vector<NodeId>& nodes,
    const TextMeasurer& measurer) {
  if(nodes.empty()) {
    return {};
  }
  auto sections = extract_properties(document, nodes.front(), measurer);
  if(nodes.size() == 1) {
    return sections;
  }
  for(auto i = std::size_t(1); i != nodes.size(); ++i) {
    intersect_properties(
      sections, extract_properties(document, nodes[i], measurer));
  }
  auto transform = std::find_if(sections.begin(), sections.end(),
    [] (const auto& section) {
      return section.m_path == "transform";
    });
  if(transform != sections.end()) {
    set_frame(*transform, get_frame(document, nodes, measurer));
  }
  return sections;
}

std::optional<Rect> Rellu::get_frame(const Document& document,
    const std::vector<NodeId>& nodes, const TextMeasurer& measurer) {
  if(nodes.size() == 1) {
    auto node = document.find(nodes.front());
    if(!node) {
      return std::nullopt;
    }
    return get_node_frame(document, *node,
      get_transform(document, node->get_id()), measurer);
  }
  auto bounds = get_world_bounds(
    document, nodes, BoundsType::GEOMETRY, measurer);
  if(!bounds || nodes.empty()) {
    return bounds;
  }
  auto artboard = find_artboard(document, nodes.front());
  if(artboard.is_nil()) {
    return bounds;
  }
  for(auto& id : nodes) {
    if(id == artboard || find_artboard(document, id) != artboard) {
      return bounds;
    }
  }
  auto& frame = document.get(artboard).get_artboard()->m_frame;
  return Rect(bounds->m_x - frame.m_x, bounds->m_y - frame.m_y,
    bounds->m_width, bounds->m_height);
}

const PropertyDescriptor* Rellu::find_property(
    const std::vector<PropertyDescriptor>& properties, std::string_view path) {
  for(auto& property : properties) {
    if(property.m_path == path) {
      return &property;
    }
    if(path.starts_with(property.m_path) &&
        path.size() > property.m_path.size() &&
        path[property.m_path.size()] == '.') {
      return find_property(property.m_children, path);
    }
  }
  return nullptr;
}

QString Rellu::to_text(const PropertyValue& value) {
  return std::visit([] <typename T> (const T& value) -> QString {
    if constexpr(std::same_as<T, std::monostate>) {
      return QString();
    } else if constexpr(std::same_as<T, bool>) {
      return value ? "Yes" : "No";
    } else if constexpr(std::same_as<T, int>) {
      return QString::number(value);
    } else if constexpr(std::same_as<T, double>) {
      return format_number(value);
    } else if constexpr(std::same_as<T, QString>) {
      return value;
    } else if constexpr(std::same_as<T, Color>) {
      auto text = QString("#%1%2%3").arg(to_byte(value.m_red), 2, 16,
        QChar('0')).arg(to_byte(value.m_green), 2, 16, QChar('0')).arg(
        to_byte(value.m_blue), 2, 16, QChar('0')).toUpper();
      if(value.m_alpha < 1) {
        text += QString(" %1%").arg(format_number(100 * value.m_alpha));
      }
      return text;
    } else if constexpr(std::same_as<T, Point>) {
      return QString("%1, %2").arg(
        format_number(value.m_x), format_number(value.m_y));
    } else if constexpr(std::same_as<T, Size>) {
      return QString("%1 x %2").arg(
        format_number(value.m_width), format_number(value.m_height));
    } else {
      if(value.m_index >= 0 && value.m_index < value.m_options.size()) {
        return value.m_options[value.m_index];
      }
      return QString::number(value.m_index);
    }
  }, value);
}

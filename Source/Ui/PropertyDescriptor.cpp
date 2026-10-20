#include <algorithm>
#include <cmath>
#include <concepts>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Ui/LayoutProperties.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  const auto PASS_THROUGH = QString("Pass Through");
  const auto BLEND_MODE_ORDER = std::to_array({BlendMode::NORMAL,
    BlendMode::DARKEN, BlendMode::MULTIPLY, BlendMode::COLOR_BURN,
    BlendMode::LIGHTEN, BlendMode::SCREEN, BlendMode::COLOR_DODGE,
    BlendMode::OVERLAY, BlendMode::SOFT_LIGHT, BlendMode::HARD_LIGHT,
    BlendMode::DIFFERENCE, BlendMode::EXCLUSION, BlendMode::HUE,
    BlendMode::SATURATION, BlendMode::COLOR, BlendMode::LUMINOSITY});
  const auto BLEND_MODES = QStringList({"Normal", "Darken", "Multiply",
    "Color Burn", "Lighten", "Screen", "Color Dodge", "Overlay", "Soft Light",
    "Hard Light", "Difference", "Exclusion", "Hue", "Saturation", "Color",
    "Luminosity"});
  const auto FILL_TYPES = QStringList({"None", "Solid", "Gradient", "Image"});
  const auto STROKE_ALIGNMENTS = QStringList({"Center", "Inner", "Outer"});
  const auto STROKE_CAPS = QStringList({"Butt", "Round", "Projecting"});
  const auto STROKE_JOINS = QStringList({"Miter", "Round", "Bevel"});
  const auto TEXT_ALIGNMENTS = QStringList({"Left", "Center", "Right"});
  const auto TEXT_TRANSFORMS =
    QStringList({"None", "Uppercase", "Lowercase", "Title Case"});
  const auto TEXT_SCRIPTS = QStringList({"Normal", "Superscript", "Subscript"});
  const auto TEXT_FRAMES =
    QStringList({"Auto Width", "Auto Height", "Fixed Size"});
  const auto SCROLL_DIRECTIONS = QStringList(
    {"None", "Horizontal", "Vertical", "Horizontal & Vertical"});
  const auto ARTBOARD_SCROLLING = QStringList({"None", "Vertical"});
  const auto RESIZE_MODES = QStringList({"Auto", "Manual"});
  const auto ORIENTATIONS = QStringList({"Portrait", "Landscape", "Square"});
  const auto GRID_TYPES = QStringList({"Layout", "Square"});
  const auto BOOLEAN_OPERATIONS =
    QStringList({"Add", "Subtract", "Intersect", "Exclude Overlap"});
  const auto CORNER_MODES = QStringList(
    {"Same radius for all corners", "Different radius for each corner"});
  const auto BLUR_TYPES = QStringList({"Background Blur", "Object Blur"});
  const auto DEFAULT_STROKE_COLOR = make_color(0x70, 0x70, 0x70);
  const auto DEFAULT_SHADOW_COLOR = make_color(0, 0, 0, 41 / 255.0);

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

  PropertyDescriptor& add(PropertyDescriptor& parent, const std::string& name,
      QString label, PropertyValue value = PropertyValue()) {
    parent.m_children.push_back(make_property(
      parent.m_path + '.' + name, std::move(label), std::move(value)));
    return parent.m_children.back();
  }

  QString to_qstring(const std::string& value) {
    return QString::fromStdString(value);
  }

  void set_read_only(PropertyDescriptor& property) {
    property.m_is_read_only = true;
    for(auto& child : property.m_children) {
      set_read_only(child);
    }
  }

  struct Placement {
    const Node* m_node;
    Matrix m_transform;
  };

  std::optional<Placement> get_placement(
      const Document& document, const Node& node) {
    if(node.get_kind() != NodeKind::REFERENCE) {
      return Placement(&node, node.m_header.m_transform);
    }
    auto resolution = resolve_reference(document.get_nodes(), node.get_id());
    if(!resolution) {
      return std::nullopt;
    }
    auto source = document.find(resolution->m_source);
    if(!source) {
      return std::nullopt;
    }
    return Placement(source, resolution->m_transform);
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

  bool is_line(const Node& node) {
    auto shape = node.get_shape();
    return shape && std::holds_alternative<LineShape>(shape->m_geometry);
  }

  std::pair<bool, bool> get_fixed_dimensions(const Node& node) {
    if(auto text = node.get_text()) {
      if(std::holds_alternative<PositionedFrame>(text->m_frame)) {
        return std::pair(true, true);
      }
      return std::pair(
        false, std::holds_alternative<AutoHeightFrame>(text->m_frame));
    }
    auto group = node.get_group();
    if(!group) {
      return std::pair(false, false);
    }
    auto& scroll = group->m_scroll_group;
    if(!scroll) {
      return std::pair(false, false);
    }
    return std::pair(scroll->m_direction == ScrollDirection::VERTICAL,
      scroll->m_direction == ScrollDirection::HORIZONTAL);
  }

  PropertyDescriptor make_boolean(const Node& node) {
    auto section = make_property("boolean", "Boolean");
    if(auto shape = node.get_shape()) {
      if(auto compound = std::get_if<CompoundShape>(&shape->m_geometry)) {
        add(section, "operation", "Operation",
          make_choice(compound->m_operation, BOOLEAN_OPERATIONS));
      }
    }
    return section;
  }

  PropertyDescriptor make_component(
      const Document& document, const Node& node) {
    auto section = make_property("component", "Component");
    auto group = node.get_group();
    if(!group || !group->m_component_instance) {
      return section;
    }
    auto& instance = *group->m_component_instance;
    section.m_label = instance.m_is_main ? "Component (Main)" :
      "Component (Instance)";
    auto& components = document.get_resources().m_components;
    auto component = components.find(instance.m_component);
    if(component == components.end()) {
      add(section, "name", "Name", QString("Missing")).m_is_read_only = true;
      return section;
    }
    auto& definition = component->second;
    auto is_linked = static_cast<bool>(definition.m_link);
    add(section, "name", "Name", to_qstring(definition.m_name)).
      m_is_read_only = !instance.m_is_main || is_linked;
    auto states = QStringList({translate("inspector", "Default State")});
    auto index = 0;
    for(auto& state : definition.m_states) {
      if(state.m_id == instance.m_state &&
          state.m_id != definition.m_default_state) {
        index = static_cast<int>(states.size());
      }
      states.push_back(to_qstring(state.m_name));
    }
    add(section, "state", "State", Choice(index, states));
    if(instance.m_is_main) {
      add(section, "main", "Main Component", true).m_is_read_only = true;
      if(index != 0 && !is_linked) {
        add(section, "state_name", "State Name", states[index]);
      }
    }
    if(is_linked) {
      add(section, "linked", "Linked", true).m_is_read_only = true;
    }
    return section;
  }

  PropertyDescriptor make_transform(const Document& document,
      const Node& node, const Node& displayed, const Placement& placement,
      const TextMeasurer& measurer) {
    auto section = make_property("transform", "Transform");
    auto& transform = placement.m_transform;
    auto frame = get_node_frame(document, node, transform, measurer);
    if(frame && node.get_artboard()) {
      add(section, "width", "W", frame->m_width);
      add(section, "height", "H", frame->m_height);
      add(section, "lock_aspect", "Lock Aspect",
        static_cast<bool>(node.m_header.m_aspect_lock));
      add(section, "x", "X", frame->m_x);
      add(section, "y", "Y", frame->m_y);
      add(section, "orientation", "Orientation", make_choice(
        get_orientation(*node.get_artboard()), ORIENTATIONS));
      return section;
    }
    auto origin = frame ? frame->get_top_left() : transform.map(Point(0, 0));
    if(frame) {
      auto [is_width_fixed, is_height_fixed] =
        get_fixed_dimensions(displayed);
      add(section, "width", "W", frame->m_width).m_is_read_only =
        is_width_fixed;
      add(section, "height", "H", frame->m_height).m_is_read_only =
        is_height_fixed;
    }
    add(section, "lock_aspect", "Lock Aspect",
      static_cast<bool>(node.m_header.m_aspect_lock));
    auto stack = get_stack_direction(document, node.get_id());
    add(section, "x", "X", origin.m_x).m_is_read_only =
      stack == Orientation::HORIZONTAL;
    add(section, "y", "Y", origin.m_y).m_is_read_only =
      stack == Orientation::VERTICAL;
    auto rotation = std::atan2(transform.m_b, transform.m_a) *
      180 / std::numbers::pi;
    add(section, "rotation", "Rotation", rotation == 0 ? 0.0 : rotation);
    auto scroll = 0;
    if(auto group = displayed.get_group()) {
      if(auto& scroll_group = group->m_scroll_group) {
        scroll = static_cast<int>(scroll_group->m_direction) + 1;
      }
    }
    add(section, "scroll", "Scroll", Choice(scroll, SCROLL_DIRECTIONS));
    if(!find_artboard(document, node.get_id()).is_nil()) {
      add(section, "fixed_position", "Fix Position When Scrolling",
        node.m_header.m_is_fixed_position);
    }
    return section;
  }

  PropertyDescriptor make_layout(const Node& node, const Node& displayed) {
    auto section = make_property("layout", "Layout");
    auto& constraints = node.m_header.m_constraints;
    auto& responsive = add(section, "responsive", "Responsive Resize",
      constraints.m_is_enabled);
    if(constraints.m_is_enabled && !displayed.get_artboard()) {
      add(responsive, "mode", "Resize",
        Choice(constraints.m_is_custom ? 1 : 0, RESIZE_MODES));
      for(auto [name, label, value] : {
          std::tuple("top", "Pin Top Edge", constraints.m_top),
          std::tuple("right", "Pin Right Edge", constraints.m_right),
          std::tuple("bottom", "Pin Bottom Edge", constraints.m_bottom),
          std::tuple("left", "Pin Left Edge", constraints.m_left),
          std::tuple("width", "Fix Width", constraints.m_width),
          std::tuple("height", "Fix Height", constraints.m_height)}) {
        add(responsive, name, label, value).m_is_read_only =
          !constraints.m_is_custom;
      }
    }
    add_layout_properties(displayed, section);
    if(auto artboard = displayed.get_artboard()) {
      add(section, "scrolling", "Scrolling",
        Choice(artboard->m_viewport_height ? 1 : 0, ARTBOARD_SCROLLING));
      if(artboard->m_viewport_height) {
        add(section, "viewport_height", "Viewport Height",
          *artboard->m_viewport_height);
      }
    }
    return section;
  }

  bool is_same_font(const Font& left, const Font& right) {
    return left.m_family == right.m_family && left.m_style == right.m_style;
  }

  double get_natural_line_height(
      const TextStyle& style, const TextMeasurer& measurer) {
    auto line = Text();
    line.m_content = Shared(std::u16string());
    line.m_base_style = style;
    return std::round(measurer(line).m_height);
  }

  std::vector<TextRun> get_described_runs(
      const Text& text, const TextSelection* selection) {
    if(!selection) {
      return get_runs(text);
    }
    auto size = text.m_content->size();
    auto range = selection->get_range();
    range.m_start = std::min(range.m_start, size);
    range.m_end = std::min(range.m_end, size);
    if(range.m_start == range.m_end) {
      auto run = get_typing_run(text, range.m_start);
      if(auto& pending = selection->get_pending_style()) {
        run.m_style = *pending;
      }
      if(auto& hyperlink = selection->get_pending_hyperlink()) {
        run.m_hyperlink = *hyperlink;
      }
      return {run};
    }
    auto runs = std::vector<TextRun>();
    auto start = std::size_t(0);
    for(auto& run : get_runs(text)) {
      auto end = start + run.m_length;
      if(start < range.m_end && end > range.m_start) {
        runs.push_back(run);
      }
      start = end;
    }
    return runs;
  }

  PropertyDescriptor make_text(const Text& text, const TextMeasurer& measurer,
      const TextSelection* selection) {
    auto section = make_property("text", "Text");
    auto described = get_described_runs(text, selection);
    if(described.empty()) {
      described.push_back(TextRun(0, text.m_base_style));
    }
    auto& run = described.front();
    auto& style = run.m_style;
    auto is_run_mixed = [&] (auto compare) {
      return std::any_of(described.begin(), described.end(),
        [&] (const auto& other) {
          return !compare(other, run);
        });
    };
    auto is_mixed = [&] (auto compare) {
      return is_run_mixed([&] (const auto& left, const auto& right) {
        return compare(left.m_style, right.m_style);
      });
    };
    add(section, "family", "Font", to_qstring(style.m_font.m_family)).
      m_is_mixed = is_mixed([] (const auto& left, const auto& right) {
        return left.m_font.m_family == right.m_font.m_family;
      });
    add(section, "size", "Font Size", style.m_font.m_size).m_is_mixed =
      is_mixed([] (const auto& left, const auto& right) {
        return left.m_font.m_size == right.m_font.m_size;
      });
    auto weight = style.m_font.m_style.empty() ? QString("Regular") :
      to_qstring(style.m_font.m_style);
    add(section, "style", "Font Style", weight).m_is_mixed =
      is_mixed([] (const auto& left, const auto& right) {
        return is_same_font(left.m_font, right.m_font);
      });
    add(section, "letter_spacing", "Character Spacing",
      style.m_letter_spacing).m_is_mixed = is_mixed(
        [] (const auto& left, const auto& right) {
          return left.m_letter_spacing == right.m_letter_spacing;
        });
    auto& paragraph = text.m_paragraph_style;
    auto line_height = paragraph.m_line_height.value_or(
      get_natural_line_height(style, measurer));
    add(section, "line_height", "Line Spacing", line_height);
    add(section, "paragraph_spacing", "Paragraph Spacing",
      paragraph.m_paragraph_spacing);
    add(section, "alignment", "Alignment",
      make_choice(paragraph.m_alignment, TEXT_ALIGNMENTS));
    auto frame = 0;
    if(std::holds_alternative<AutoHeightFrame>(text.m_frame)) {
      frame = 1;
    } else if(std::holds_alternative<AreaFrame>(text.m_frame)) {
      frame = 2;
    }
    add(section, "frame", "Frame", Choice(frame, TEXT_FRAMES));
    add(section, "transform", "Case",
      make_choice(style.m_transform, TEXT_TRANSFORMS)).m_is_mixed =
        is_mixed([] (const auto& left, const auto& right) {
          return left.m_transform == right.m_transform;
        });
    add(section, "script", "Script",
      make_choice(style.m_script, TEXT_SCRIPTS)).m_is_mixed =
        is_mixed([] (const auto& left, const auto& right) {
          return left.m_script == right.m_script;
        });
    add(section, "underline", "Underline", style.m_is_underlined).
      m_is_mixed = is_mixed([] (const auto& left, const auto& right) {
        return left.m_is_underlined == right.m_is_underlined;
      });
    add(section, "strikethrough", "Strikethrough",
      style.m_is_struck_through).m_is_mixed =
        is_mixed([] (const auto& left, const auto& right) {
          return left.m_is_struck_through == right.m_is_struck_through;
        });
    auto& hyperlink = add(section, "hyperlink", "Insert URL here",
      to_qstring(run.m_hyperlink));
    hyperlink.m_is_mixed =
      is_run_mixed([] (const auto& left, const auto& right) {
        return left.m_hyperlink == right.m_hyperlink;
      });
    return section;
  }

  PropertyDescriptor make_appearance(const Node& node) {
    auto section = make_property("appearance", "Appearance");
    if(node.get_artboard()) {
      return section;
    }
    add(section, "opacity", "Opacity", node.m_header.m_opacity);
    auto& header = node.m_header;
    auto index = static_cast<int>(std::find(BLEND_MODE_ORDER.begin(),
      BLEND_MODE_ORDER.end(), header.m_blend_mode) - BLEND_MODE_ORDER.begin());
    if(node.get_group()) {
      auto options = BLEND_MODES;
      options.prepend(PASS_THROUGH);
      if(header.m_blend_mode == BlendMode::NORMAL && !header.m_is_isolated) {
        index = 0;
      } else {
        ++index;
      }
      add(section, "blend_mode", "Blend Mode", Choice(index, options));
    } else {
      add(section, "blend_mode", "Blend Mode", Choice(index, BLEND_MODES));
    }
    return section;
  }

  std::optional<PropertyDescriptor> make_geometry(const Shape& shape) {
    auto section = make_property("geometry", "Geometry");
    if(auto rectangle = std::get_if<RectangleShape>(&shape.m_geometry)) {
      auto& radii = rectangle->m_radii;
      auto is_uniform = radii.m_top_left == radii.m_top_right &&
        radii.m_top_left == radii.m_bottom_right &&
        radii.m_top_left == radii.m_bottom_left;
      auto& corners = add(section, "radii", "Corner Radius",
        Choice(is_uniform ? 0 : 1, CORNER_MODES));
      add(corners, "all", "Corner Radius", radii.m_top_left);
      add(corners, "top_left", "Top Left Corner Radius", radii.m_top_left);
      add(corners, "top_right", "Top Right Corner Radius",
        radii.m_top_right);
      add(corners, "bottom_right", "Bottom Right Corner Radius",
        radii.m_bottom_right);
      add(corners, "bottom_left", "Bottom Left Corner Radius",
        radii.m_bottom_left);
      return section;
    } else if(auto polygon = std::get_if<PolygonShape>(&shape.m_geometry)) {
      add(section, "corners", "Corner Count", polygon->m_corner_count);
      add(section, "corner_radius", "Corner Radius", polygon->m_corner_radius);
      add(section, "star_ratio", "Star Ratio", polygon->m_star_ratio / 100);
      return section;
    }
    return std::nullopt;
  }

  PropertyDescriptor make_fill(const Fill& fill) {
    auto paint = enable_fill(fill);
    auto section =
      make_property("fill", "Fill", make_choice(paint.index(), FILL_TYPES));
    add(section, "enabled", "Apply Fill",
      !std::holds_alternative<NoFill>(fill));
    add(section, "paint", "Fill", paint);
    if(auto solid = std::get_if<SolidFill>(&paint)) {
      add(section, "color", "Color", solid->m_color);
    } else if(auto gradient = std::get_if<GradientFill>(&paint)) {
      auto& stops = add(section, "stops", "Stops",
        static_cast<int>(gradient->m_stops.size()));
      for(auto i = std::size_t(0); i != gradient->m_stops.size(); ++i) {
        auto& stop = gradient->m_stops[i];
        add(stops, std::to_string(i),
          QString("At %1").arg(format_number(stop.m_offset)), stop.m_color);
      }
    } else if(auto image = std::get_if<ImageFill>(&paint)) {
      add(section, "image", "Image", to_qstring(to_string(image->m_image)));
    }
    return section;
  }

  PropertyDescriptor make_stroke(
      const std::optional<Stroke>& stroke, const Node& node) {
    auto section = make_property("stroke", "Border");
    add(section, "enabled", "Apply Border",
      stroke && stroke->m_is_enabled);
    add(section, "color", "Color",
      stroke ? stroke->m_color : DEFAULT_STROKE_COLOR);
    if(node.get_text()) {
      return section;
    }
    add(section, "width", "Size", stroke ? stroke->m_width : 1.0);
    auto dash = 0.0;
    auto gap = 0.0;
    if(stroke && !stroke->m_dashes.empty()) {
      dash = stroke->m_dashes[0];
      gap = stroke->m_dashes.size() > 1 ? stroke->m_dashes[1] : dash;
    }
    add(section, "dash", "Dash", dash);
    add(section, "gap", "Gap", gap);
    add(section, "alignment", "Alignment",
      make_choice(stroke ? stroke->m_alignment : StrokeAlignment::CENTER,
        STROKE_ALIGNMENTS)).m_is_read_only = is_line(node);
    add(section, "cap", "Cap",
      make_choice(stroke ? stroke->m_cap : StrokeCap::BUTT, STROKE_CAPS));
    add(section, "join", "Join",
      make_choice(stroke ? stroke->m_join : StrokeJoin::MITER, STROKE_JOINS)).
        m_is_read_only = is_line(node);
    return section;
  }

  void add_shadow(PropertyDescriptor& section, const std::string& name,
      const QString& label, const auto* shadow) {
    auto& row = add(section, name, label,
      static_cast<bool>(shadow) && shadow->m_is_visible);
    add(row, "color", "Color", shadow ? shadow->m_color :
      DEFAULT_SHADOW_COLOR);
    if(shadow) {
      add(row, "x", "X", shadow->m_offset.m_x);
      add(row, "y", "Y", shadow->m_offset.m_y);
      add(row, "blur", "B", shadow->m_blur);
    }
  }

  PropertyDescriptor make_effects(
      const std::vector<Effect>& effects, const Node& node) {
    auto section = make_property("effects", "Effects");
    auto inner = static_cast<const InnerShadow*>(nullptr);
    auto drop = static_cast<const DropShadow*>(nullptr);
    auto blur = static_cast<const Blur*>(nullptr);
    for(auto& effect : effects) {
      if(auto shadow = std::get_if<InnerShadow>(&effect); shadow && !inner) {
        inner = shadow;
      } else if(auto shadow = std::get_if<DropShadow>(&effect);
          shadow && !drop) {
        drop = shadow;
      } else if(auto candidate = std::get_if<Blur>(&effect);
          candidate && !blur) {
        blur = candidate;
      }
    }
    if(!is_line(node)) {
      add_shadow(section, "inner_shadow", "Inner Shadow", inner);
    }
    add_shadow(section, "drop_shadow", "Drop Shadow", drop);
    auto& row = add(section, "blur", "Blur",
      static_cast<bool>(blur) && blur->m_is_visible);
    add(row, "type", "Type",
      Choice(!blur || blur->m_is_background ? 0 : 1, BLUR_TYPES));
    if(blur) {
      add(row, "amount", "Blur Amount", blur->m_amount);
      if(blur->m_is_background) {
        add(row, "brightness", "Brightness", blur->m_brightness);
        add(row, "opacity", "Effect Opacity", blur->m_fill_opacity);
      }
    }
    return section;
  }

  PropertyDescriptor make_grid(const Artboard& artboard) {
    auto section = make_property("grid", "Grid");
    auto& grid = artboard.m_layout_grid;
    add(section, "visible", "Show Grid", grid && grid->m_is_visible);
    auto is_square = grid && grid->m_type == GridType::SQUARE;
    add(section, "type", "Grid Type", Choice(is_square ? 1 : 0, GRID_TYPES));
    if(!grid) {
      return section;
    }
    if(is_square) {
      add(section, "size", "Size", grid->m_square_size.m_width);
      add(section, "color", "Color", grid->m_square_column_color);
      return section;
    }
    auto& margins = grid->m_margins;
    add(section, "columns", "Columns", grid->m_column_count);
    add(section, "column_color", "Column Color", grid->m_column_color);
    add(section, "gutter", "Gutter Width", grid->m_gutter);
    add(section, "column_width", "Column Width",
      get_column_width(*grid, artboard.m_frame.m_width));
    auto is_linked = margins.m_left == margins.m_right &&
      margins.m_top == margins.m_bottom;
    add(section, "linked_margins", "Linked Margins", is_linked);
    if(is_linked) {
      add(section, "margin_linked", "Margins", margins.m_left);
    }
    add(section, "margin_top", "Top Margin", margins.m_top);
    add(section, "margin_right", "Right Margin", margins.m_right);
    add(section, "margin_bottom", "Bottom Margin", margins.m_bottom);
    add(section, "margin_left", "Left Margin", margins.m_left);
    return section;
  }

  PropertyDescriptor make_export(const Node& node) {
    auto section = make_property("export", "Export");
    add(section, "marked", "Mark for Export",
      node.m_header.m_is_marked_for_export);
    return section;
  }

  void collect_leaves(const Document& document, const Node& node,
      std::vector<const Node*>& leaves) {
    for(auto& id : node.m_children) {
      auto child = document.find(id);
      if(!child) {
        continue;
      }
      auto displayed = get_displayed_node(document, *child);
      if(!displayed) {
        continue;
      }
      if(displayed->get_shape() || displayed->get_text()) {
        leaves.push_back(displayed);
      } else if(displayed->get_group()) {
        collect_leaves(document, *displayed, leaves);
      }
    }
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
      property.m_is_read_only |= other->m_is_read_only;
      intersect_properties(property.m_children, other->m_children);
      return false;
    });
  }

  std::vector<PropertyDescriptor> make_leaf_style(const Node& leaf) {
    auto sections = std::vector<PropertyDescriptor>();
    auto& style = leaf.get_shape() ? leaf.get_shape()->m_style :
      leaf.get_text()->m_style;
    sections.push_back(make_fill(style.m_fill));
    sections.push_back(make_stroke(style.m_stroke, leaf));
    return sections;
  }

  void add_group_style(const Document& document, const Node& group,
      std::vector<PropertyDescriptor>& sections) {
    auto leaves = std::vector<const Node*>();
    collect_leaves(document, group, leaves);
    auto style = std::vector<PropertyDescriptor>();
    if(leaves.empty()) {
      style.push_back(make_fill(NoFill()));
      style.push_back(make_stroke(std::nullopt, group));
    } else {
      style = make_leaf_style(*leaves.front());
      for(auto i = std::size_t(1); i != leaves.size(); ++i) {
        intersect_properties(style, make_leaf_style(*leaves[i]));
      }
    }
    for(auto& section : style) {
      if(leaves.empty()) {
        set_read_only(section);
      }
      sections.push_back(std::move(section));
    }
  }

  void set_position(
      PropertyDescriptor& section, const std::optional<Rect>& frame) {
    std::erase_if(section.m_children, [&] (auto& property) {
      if(property.m_path != "transform.x" &&
          property.m_path != "transform.y") {
        return false;
      } else if(!frame) {
        return true;
      }
      property.m_value =
        property.m_path == "transform.x" ? frame->m_x : frame->m_y;
      property.m_is_mixed = false;
      return false;
    });
  }

  std::optional<double> get_shown_number(
      const std::vector<PropertyDescriptor>& sections, std::string_view path) {
    auto property = find_property(sections, path);
    if(!property) {
      return std::nullopt;
    }
    auto value = std::get_if<double>(&property->m_value);
    if(!value) {
      return std::nullopt;
    }
    return std::round(*value * 100) / 100;
  }

  void compare_shown(PropertyDescriptor& section,
      const std::vector<std::vector<PropertyDescriptor>>& nodes) {
    for(auto& property : section.m_children) {
      if(property.m_path != "transform.width" &&
          property.m_path != "transform.height" &&
          property.m_path != "transform.rotation") {
        continue;
      }
      auto last = find_property(nodes.back(), property.m_path);
      auto shown = get_shown_number(nodes.back(), property.m_path);
      if(!last || !shown) {
        continue;
      }
      property.m_value = last->m_value;
      property.m_is_mixed = std::any_of(nodes.begin(), nodes.end(),
        [&] (const auto& sections) {
          return get_shown_number(sections, property.m_path) != shown;
        });
    }
  }

  void limit_size(
      std::vector<PropertyDescriptor>& sections, const StackLimits& limits) {
    auto transform = std::find_if(sections.begin(), sections.end(),
      [] (const auto& section) {
        return section.m_path == "transform";
      });
    if(transform == sections.end()) {
      return;
    }
    for(auto& property : transform->m_children) {
      if(property.m_path == "transform.width") {
        property.m_is_read_only |= limits.m_is_width_fixed;
      } else if(property.m_path == "transform.height") {
        property.m_is_read_only |= limits.m_is_height_fixed;
      }
    }
  }

  std::vector<PropertyDescriptor> extract_node_properties(
      const Document& document, const NodeId& id,
      const TextMeasurer& measurer, const TextSelection* selection) {
    auto sections = std::vector<PropertyDescriptor>();
    auto node = document.find(id);
    if(!node) {
      return sections;
    }
    auto placement = get_placement(document, *node);
    auto& displayed = placement ? *placement->m_node : *node;
    auto artboard = displayed.get_artboard();
    auto shape = displayed.get_shape();
    auto text = displayed.get_text();
    auto group = displayed.get_group();
    sections.push_back(make_property("align", "Align"));
    sections.push_back(make_boolean(displayed));
    sections.push_back(make_component(document, displayed));
    if(placement) {
      sections.push_back(
        make_transform(document, *node, displayed, *placement, measurer));
    }
    sections.push_back(make_layout(*node, displayed));
    if(text) {
      if(selection && (!selection->is_editing() ||
          selection->get_node() != id)) {
        selection = nullptr;
      }
      sections.push_back(make_text(*text, measurer, selection));
    }
    sections.push_back(make_appearance(displayed));
    if(shape) {
      if(auto geometry = make_geometry(*shape)) {
        sections.push_back(std::move(*geometry));
      }
      if(!is_line(displayed)) {
        sections.push_back(make_fill(shape->m_style.m_fill));
      }
      sections.push_back(make_stroke(shape->m_style.m_stroke, displayed));
      sections.push_back(make_effects(shape->m_style.m_effects, displayed));
    } else if(text) {
      sections.push_back(make_fill(text->m_style.m_fill));
      sections.push_back(make_stroke(text->m_style.m_stroke, displayed));
      sections.push_back(make_effects(text->m_style.m_effects, displayed));
    } else if(artboard) {
      sections.push_back(make_fill(artboard->m_background));
      sections.push_back(make_grid(*artboard));
      return sections;
    } else if(group) {
      add_group_style(document, displayed, sections);
    }
    sections.push_back(make_export(displayed));
    return sections;
  }
}

std::vector<PropertyDescriptor> Rellu::extract_properties(
    const Document& document, const NodeId& id, const TextMeasurer& measurer,
    const TextSelection* selection) {
  auto sections = extract_node_properties(document, id, measurer, selection);
  limit_size(sections, get_stack_limits(document, std::vector{id}));
  return sections;
}

std::vector<PropertyDescriptor> Rellu::extract_common_properties(
    const Document& document, const std::vector<NodeId>& nodes,
    const TextMeasurer& measurer, const TextSelection* selection) {
  if(nodes.empty()) {
    return {};
  } else if(nodes.size() == 1) {
    return extract_properties(document, nodes.front(), measurer, selection);
  }
  auto described = std::vector<std::vector<PropertyDescriptor>>();
  for(auto& id : nodes) {
    described.push_back(
      extract_node_properties(document, id, measurer, selection));
  }
  auto sections = described.front();
  for(auto i = std::size_t(1); i != described.size(); ++i) {
    intersect_properties(sections, described[i]);
  }
  auto transform = std::find_if(sections.begin(), sections.end(),
    [] (const auto& section) {
      return section.m_path == "transform";
    });
  if(transform != sections.end()) {
    set_position(*transform, get_frame(document, nodes, measurer));
    compare_shown(*transform, described);
  }
  limit_size(sections, get_stack_limits(document, nodes));
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
      auto to_byte = [] (double component) {
        return std::clamp(
          static_cast<int>(std::lround(component * 255)), 0, 255);
      };
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
    } else if constexpr(std::same_as<T, Fill>) {
      if(value.index() < static_cast<std::size_t>(FILL_TYPES.size())) {
        return FILL_TYPES[static_cast<int>(value.index())];
      }
      return QString();
    } else {
      if(value.m_index >= 0 && value.m_index < value.m_options.size()) {
        return value.m_options[value.m_index];
      }
      return QString::number(value.m_index);
    }
  }, value);
}

std::optional<BlendMode> Rellu::get_blend_mode(const Choice& choice) {
  if(choice.m_index < 0 || choice.m_index >= choice.m_options.size()) {
    return std::nullopt;
  }
  auto& name = choice.m_options[choice.m_index];
  if(name == PASS_THROUGH) {
    return BlendMode::NORMAL;
  }
  auto i = BLEND_MODES.indexOf(name);
  if(i < 0) {
    return std::nullopt;
  }
  return BLEND_MODE_ORDER[i];
}

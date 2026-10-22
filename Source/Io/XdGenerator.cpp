#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <QString>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Localization/Localization.hpp"

using namespace Rellu;

namespace {
  const auto RESOURCES_HREF =
    std::string_view("/resources/graphics/graphicContent.agc");

  std::string to_id(const NodeId& id) {
    return Rellu::to_string(id);
  }

  const TextStyle& find_text_style(const Text& text, std::size_t index) {
    auto start = std::size_t(0);
    for(auto& run : text.m_runs) {
      if(index < start + run.m_length) {
        return run.m_style;
      }
      start += run.m_length;
    }
    if(!text.m_runs.empty()) {
      return text.m_runs.back().m_style;
    }
    return text.m_base_style;
  }

  double to_channel(double value) {
    auto channel = value * 255;
    auto rounded = std::round(channel);
    if(std::abs(channel - rounded) < 1e-7) {
      return rounded;
    }
    return channel;
  }

  XdValue make_json_color(const Color& color) {
    auto json = XdValue::make_object();
    json.set("mode", "RGB");
    auto& value = json.set("value", XdValue::make_object());
    value.set("r", to_channel(color.m_red));
    value.set("g", to_channel(color.m_green));
    value.set("b", to_channel(color.m_blue));
    if(color.m_alpha != 1) {
      json.set("alpha", color.m_alpha);
    }
    return json;
  }

  double to_argb(const Color& color) {
    auto to_byte = [] (double value) {
      return static_cast<std::uint32_t>(
        std::clamp(std::round(value * 255), 0.0, 255.0));
    };
    auto argb = (to_byte(color.m_alpha) << 24) |
      (to_byte(color.m_red) << 16) | (to_byte(color.m_green) << 8) |
      to_byte(color.m_blue);
    return static_cast<double>(argb);
  }

  XdValue make_grid_color(const Color& color) {
    auto json = XdValue::make_object();
    json.set("value", to_argb(color));
    return json;
  }

  XdValue make_grid_defaults(const GridDefaults& defaults) {
    auto json = XdValue::make_object();
    if(auto& grid = defaults.m_grid) {
      auto& entry = json.set("defaultGrid", XdValue::make_object());
      entry.set("rowStroke", make_grid_color(grid->m_square_row_color));
      entry.set("columnStroke", make_grid_color(grid->m_square_column_color));
      entry.set("rowSpacing", grid->m_square_size.m_height);
      entry.set("columnSpacing", grid->m_square_size.m_width);
      entry.set("defaultLayoutWidth", grid->m_default_width);
      entry.set("columns", grid->m_column_count);
      entry.set("gutter", grid->m_gutter);
      entry.set("marginLeft", grid->m_margins.m_left);
      entry.set("marginTop", grid->m_margins.m_top);
      entry.set("marginRight", grid->m_margins.m_right);
      entry.set("marginBottom", grid->m_margins.m_bottom);
      entry.set("layoutRowStroke", make_grid_color(grid->m_row_color));
      entry.set("layoutColumnStroke", make_grid_color(grid->m_column_color));
    } else {
      json.set("defaultGrid", XdValue());
    }
    if(auto& overrides = defaults.m_overrides) {
      auto& list = json.set("layoutOverrides", XdValue::make_array());
      for(auto& grid : *overrides) {
        auto& entry = list.append(XdValue::make_object());
        entry.set("rowStroke", make_grid_color(grid.m_row_color));
        entry.set("columnStroke", make_grid_color(grid.m_column_color));
        entry.set("columns", grid.m_column_count);
        entry.set("gutter", grid.m_gutter);
        entry.set("marginTop", grid.m_margins.m_top);
        entry.set("marginLeft", grid.m_margins.m_left);
        entry.set("marginBottom", grid.m_margins.m_bottom);
        entry.set("marginRight", grid.m_margins.m_right);
        if(grid.m_maximum_width) {
          entry.set("media", XdValue::make_object()).set(
            "maxWidth", *grid.m_maximum_width);
        }
      }
    } else {
      json.set("layoutOverrides", XdValue());
    }
    return json;
  }

  XdValue make_matrix(const Matrix& matrix) {
    auto json = XdValue::make_object();
    json.set("a", matrix.m_a);
    json.set("b", matrix.m_b);
    json.set("c", matrix.m_c);
    json.set("d", matrix.m_d);
    json.set("tx", matrix.m_tx);
    json.set("ty", matrix.m_ty);
    return json;
  }

  XdValue make_matrix(const Transform3d& transform, const Matrix& planar) {
    auto matrix = transform.m_matrix;
    matrix[0] = planar.m_a;
    matrix[1] = planar.m_c;
    matrix[3] = planar.m_tx;
    matrix[4] = planar.m_b;
    matrix[5] = planar.m_d;
    matrix[7] = planar.m_ty;
    auto json = XdValue::make_object();
    for(auto row = 0; row != 4; ++row) {
      auto& entries = json.set(std::to_string(row), XdValue::make_array());
      for(auto column = 0; column != 4; ++column) {
        entries.append(matrix[4 * row + column]);
      }
    }
    return json;
  }

  XdValue make_placeholders(std::size_t count) {
    auto array = XdValue::make_array();
    array.get_elements().resize(count);
    return array;
  }

  const char* to_name(BlendMode mode) {
    switch(mode) {
      case BlendMode::MULTIPLY:
        return "multiply";
      case BlendMode::SCREEN:
        return "screen";
      case BlendMode::OVERLAY:
        return "overlay";
      case BlendMode::DARKEN:
        return "darken";
      case BlendMode::LIGHTEN:
        return "lighten";
      case BlendMode::COLOR_DODGE:
        return "color-dodge";
      case BlendMode::COLOR_BURN:
        return "color-burn";
      case BlendMode::HARD_LIGHT:
        return "hard-light";
      case BlendMode::SOFT_LIGHT:
        return "soft-light";
      case BlendMode::DIFFERENCE:
        return "difference";
      case BlendMode::EXCLUSION:
        return "exclusion";
      case BlendMode::HUE:
        return "hue";
      case BlendMode::SATURATION:
        return "saturation";
      case BlendMode::COLOR:
        return "color";
      case BlendMode::LUMINOSITY:
        return "luminosity";
      default:
        return "normal";
    }
  }

  const char* to_name(TextTransform transform) {
    switch(transform) {
      case TextTransform::UPPERCASE:
        return "uppercase";
      case TextTransform::LOWERCASE:
        return "lowercase";
      case TextTransform::TITLE_CASE:
        return "titlecase";
      default:
        return "none";
    }
  }

  const char* to_name(TextScript script) {
    switch(script) {
      case TextScript::SUPERSCRIPT:
        return "superscript";
      case TextScript::SUBSCRIPT:
        return "subscript";
      default:
        return "none";
    }
  }

  const char* to_name(Transition transition) {
    switch(transition) {
      case Transition::DISSOLVE:
        return "dissolve";
      case Transition::SLIDE_LEFT:
        return "slide-left";
      case Transition::SLIDE_RIGHT:
        return "slide-right";
      case Transition::SLIDE_UP:
        return "slide-up";
      case Transition::SLIDE_DOWN:
        return "slide-down";
      case Transition::PUSH_LEFT:
        return "push-left";
      case Transition::PUSH_RIGHT:
        return "push-right";
      case Transition::PUSH_UP:
        return "push-up";
      case Transition::PUSH_DOWN:
        return "push-down";
      case Transition::AUTO_ANIMATE:
        return "auto-animate";
      default:
        return "none";
    }
  }

  const char* to_name(Easing easing) {
    switch(easing) {
      case Easing::LINEAR:
        return "linear";
      case Easing::EASE_IN:
        return "ease-in";
      case Easing::EASE_IN_OUT:
        return "ease-in-out";
      case Easing::WIND_UP:
        return "wind-up";
      case Easing::BOUNCE:
        return "bounce";
      case Easing::SNAP:
        return "snap";
      default:
        return "ease-out";
    }
  }

  std::string to_utf8(const std::u16string& text) {
    return QString::fromUtf16(text.data(),
      static_cast<qsizetype>(text.size())).toStdString();
  }

  void append_path_number(double value, std::string& out) {
    auto buffer = std::array<char, 64>();
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
      value, std::chars_format::general, 16);
    out.append(buffer.data(), result.ptr);
  }

  XdValue make_geometry(const ShapeGeometry& geometry, std::size_t children) {
    auto json = XdValue::make_object();
    if(auto rectangle = std::get_if<RectangleShape>(&geometry)) {
      json.set("type", "rect");
      json.set("x", rectangle->m_bounds.m_x);
      json.set("y", rectangle->m_bounds.m_y);
      json.set("width", rectangle->m_bounds.m_width);
      json.set("height", rectangle->m_bounds.m_height);
      auto& radii = rectangle->m_radii;
      if(radii != CornerRadii(0, 0, 0, 0)) {
        auto& r = json.set("r", XdValue::make_array());
        r.append(radii.m_top_left);
        r.append(radii.m_top_right);
        r.append(radii.m_bottom_right);
        r.append(radii.m_bottom_left);
      }
    } else if(auto ellipse = std::get_if<EllipseShape>(&geometry)) {
      if(ellipse->m_x_radius == ellipse->m_y_radius) {
        json.set("type", "circle");
        json.set("cx", ellipse->m_center.m_x);
        json.set("cy", ellipse->m_center.m_y);
        json.set("r", ellipse->m_x_radius);
      } else {
        json.set("type", "ellipse");
        json.set("cx", ellipse->m_center.m_x);
        json.set("cy", ellipse->m_center.m_y);
        json.set("rx", ellipse->m_x_radius);
        json.set("ry", ellipse->m_y_radius);
      }
    } else if(auto line = std::get_if<LineShape>(&geometry)) {
      json.set("type", "line");
      json.set("x1", line->m_start.m_x);
      json.set("y1", line->m_start.m_y);
      json.set("x2", line->m_end.m_x);
      json.set("y2", line->m_end.m_y);
    } else if(auto polygon = std::get_if<PolygonShape>(&geometry)) {
      json.set("type", "polygon");
      json.set("uxdesign#cornerCount", polygon->m_corner_count);
      auto& points = json.set("points", XdValue::make_array());
      auto vertices = polygon->m_vertices.empty() ? make_polygon_points(
        polygon->m_corner_count, polygon->m_size, polygon->m_star_ratio) :
        polygon->m_vertices;
      for(auto& vertex : vertices) {
        auto& point = points.append(XdValue::make_object());
        point.set("x", vertex.m_x);
        point.set("y", vertex.m_y);
      }
      json.set("uxdesign#width", polygon->m_size.m_width);
      json.set("uxdesign#height", polygon->m_size.m_height);
      if(polygon->m_corner_radius != 0) {
        json.set("uxdesign#cornerRadius", polygon->m_corner_radius);
      }
      if(polygon->m_star_ratio != 100) {
        json.set("uxdesign#starRatio", polygon->m_star_ratio);
      }
    } else if(auto path = std::get_if<PathShape>(&geometry)) {
      json.set("type", "path");
      json.set("path", to_svg_path(*path->m_path));
      if(path->m_fill_rule == FillRule::EVEN_ODD) {
        json.set("winding", "evenodd");
      }
    } else if(auto compound = std::get_if<CompoundShape>(&geometry)) {
      json.set("type", "compound");
      switch(compound->m_operation) {
        case CompoundOperation::SUBTRACT:
          json.set("operation", "subtract");
          break;
        case CompoundOperation::INTERSECT:
          json.set("operation", "intersect");
          break;
        case CompoundOperation::EXCLUDE:
          json.set("operation", "exclude");
          break;
        default:
          json.set("operation", "add");
          break;
      }
      json.set("path", to_svg_path(*compound->m_path));
      json.set("children", make_placeholders(children));
    }
    return json;
  }

  XdValue make_fill(const Fill& fill) {
    if(auto none = std::get_if<NoFill>(&fill); none && none->m_paint) {
      auto json = std::visit([] (const auto& paint) {
        return make_fill(Fill(paint));
      }, *none->m_paint);
      auto type = std::string(json.find("type")->to_string());
      json.set("uxdesign#type", std::move(type));
      json.set("type", "none");
      return json;
    }
    auto json = XdValue::make_object();
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      json.set("type", "solid");
      auto& color = json.set("color", make_json_color(solid->m_color));
      if(!solid->m_swatch.is_nil()) {
        color.set("linkId", to_id(solid->m_swatch));
        json.set("linkId", to_id(solid->m_swatch));
      }
    } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
      json.set("type", "gradient");
      auto& content = json.set("gradient", XdValue::make_object());
      if(gradient->m_units == GradientUnits::USER_SPACE) {
        content.set("units", "userSpaceOnUse");
      } else {
        content.set("units", "objectBoundingBox");
      }
      auto type = "linear";
      if(auto linear = std::get_if<LinearGradient>(&gradient->m_geometry)) {
        content.set("x1", linear->m_start.m_x);
        content.set("y1", linear->m_start.m_y);
        content.set("x2", linear->m_end.m_x);
        content.set("y2", linear->m_end.m_y);
      } else if(auto radial =
          std::get_if<RadialGradient>(&gradient->m_geometry)) {
        type = "radial";
        content.set("cx", radial->m_center.m_x);
        content.set("cy", radial->m_center.m_y);
        content.set("r", radial->m_radius);
        content.set("fx", radial->m_focus.m_x);
        content.set("fy", radial->m_focus.m_y);
      }
      if(!gradient->m_transform.is_identity()) {
        content.set("transform", make_matrix(gradient->m_transform));
      }
      auto angular = std::get_if<AngularGradient>(&gradient->m_geometry);
      if(angular) {
        type = "angular";
      }
      auto& ux = content.set("meta", XdValue::make_object()).set(
        "ux", XdValue::make_object());
      auto& resources = ux.set("gradientResources", XdValue::make_object());
      resources.set("type", type);
      auto& stops = resources.set("stops", XdValue::make_array());
      for(auto& stop : gradient->m_stops) {
        auto& entry = stops.append(XdValue::make_object());
        entry.set("offset", stop.m_offset);
        entry.set("color", make_json_color(stop.m_color));
      }
      if(angular) {
        ux.set("x", angular->m_center.m_x);
        ux.set("y", angular->m_center.m_y);
        ux.set("rotation", angular->m_rotation);
      }
    } else if(auto image = std::get_if<ImageFill>(&fill)) {
      json.set("type", "pattern");
      auto& pattern = json.set("pattern", XdValue::make_object());
      pattern.set("width", image->m_size.m_width);
      pattern.set("height", image->m_size.m_height);
      pattern.set("href", "");
      auto& ux = pattern.set("meta", XdValue::make_object()).set(
        "ux", XdValue::make_object());
      if(image->m_scale_behavior == ScaleBehavior::COVER) {
        ux.set("scaleBehavior", "cover");
      } else {
        ux.set("scaleBehavior", "fill");
      }
      ux.set("uid", to_uid(image->m_image));
      ux.set("hrefLastModifiedDate", 0);
      if(image->m_offset.m_x != 0) {
        ux.set("offsetX", image->m_offset.m_x);
      }
      if(image->m_offset.m_y != 0) {
        ux.set("offsetY", image->m_offset.m_y);
      }
      if(image->m_scale != 1) {
        ux.set("scale", image->m_scale);
      }
      if(image->m_horizontal_scale != 1) {
        ux.set("scaleX", image->m_horizontal_scale);
      }
      if(image->m_vertical_scale != 1) {
        ux.set("scaleY", image->m_vertical_scale);
      }
      if(image->m_is_flipped_horizontally) {
        ux.set("flipX", true);
      }
      if(image->m_is_flipped_vertically) {
        ux.set("flipY", true);
      }
    } else {
      json.set("type", "none");
    }
    return json;
  }

  XdValue make_stroke(const Stroke& stroke) {
    auto json = XdValue::make_object();
    json.set("type", stroke.m_is_enabled ? "solid" : "none");
    json.set("color", make_json_color(stroke.m_color));
    json.set("width", stroke.m_width);
    if(stroke.m_alignment == StrokeAlignment::INSIDE) {
      json.set("align", "inside");
    } else if(stroke.m_alignment == StrokeAlignment::OUTSIDE) {
      json.set("align", "outside");
    }
    if(stroke.m_cap == StrokeCap::ROUND) {
      json.set("cap", "round");
    } else if(stroke.m_cap == StrokeCap::SQUARE) {
      json.set("cap", "square");
    }
    if(stroke.m_join == StrokeJoin::ROUND) {
      json.set("join", "round");
    } else if(stroke.m_join == StrokeJoin::BEVEL) {
      json.set("join", "bevel");
    }
    if(stroke.m_miter_limit != 4) {
      json.set("miterLimit", stroke.m_miter_limit);
    }
    if(!stroke.m_dashes.empty()) {
      auto& dashes = json.set("dash", XdValue::make_array());
      for(auto dash : stroke.m_dashes) {
        dashes.append(dash);
      }
    }
    if(stroke.m_dash_offset != 0) {
      json.set("dashOffset", stroke.m_dash_offset);
    }
    return json;
  }

  XdValue make_shadow(Point offset, double blur, const Color& color) {
    auto json = XdValue::make_object();
    json.set("dx", offset.m_x);
    json.set("dy", offset.m_y);
    json.set("r", blur);
    json.set("color", make_json_color(color));
    return json;
  }

  XdValue make_filters(const std::vector<Effect>& effects) {
    auto filters = XdValue::make_array();
    for(auto& effect : effects) {
      auto& filter = filters.append(XdValue::make_object());
      auto is_visible = std::visit([] (const auto& value) {
        return value.m_is_visible;
      }, effect);
      if(auto shadow = std::get_if<DropShadow>(&effect)) {
        filter.set("type", "dropShadow");
        if(!is_visible) {
          filter.set("visible", false);
        }
        filter.set("global", true);
        filter.set("params", XdValue::make_object()).set(
          "dropShadows", XdValue::make_array()).append(
            make_shadow(shadow->m_offset, shadow->m_blur, shadow->m_color));
      } else if(auto inner = std::get_if<InnerShadow>(&effect)) {
        filter.set("type", "uxdesign#innerShadow");
        if(!is_visible) {
          filter.set("visible", false);
        }
        filter.set("global", true);
        filter.set("params", XdValue::make_object()).set(
          "innerShadows", XdValue::make_array()).append(
            make_shadow(inner->m_offset, inner->m_blur, inner->m_color));
      } else if(auto blur = std::get_if<Blur>(&effect)) {
        filter.set("type", "uxdesign#blur");
        if(!is_visible) {
          filter.set("visible", false);
        }
        filter.set("global", true);
        auto& params = filter.set("params", XdValue::make_object());
        params.set("blurAmount", blur->m_amount);
        params.set("brightnessAmount", blur->m_brightness);
        params.set("fillOpacity", blur->m_fill_opacity);
        params.set("backgroundEffect", blur->m_is_background);
      }
    }
    return filters;
  }

  XdValue make_font(const Font& font) {
    auto json = XdValue::make_object();
    if(!font.m_postscript_name.empty()) {
      json.set("postscriptName", font.m_postscript_name);
    }
    json.set("family", font.m_family);
    json.set("style", font.m_style);
    json.set("size", font.m_size);
    return json;
  }

  XdValue make_range(const TextStyle& style, std::size_t length,
      const CharacterStyleId& link, const std::string& hyperlink,
      const TextStyle& base) {
    auto json = XdValue::make_object();
    json.set("length", length);
    json.set("linkId", link.is_nil() ? std::string() : to_id(link));
    json.set("fontFamily", style.m_font.m_family);
    json.set("fontStyle", style.m_font.m_style);
    auto& name = style.m_font.m_postscript_name;
    if(!name.empty() && (name != base.m_font.m_postscript_name ||
        style.m_font.m_family != base.m_font.m_family ||
        style.m_font.m_style != base.m_font.m_style)) {
      json.set("postscriptName", name);
    }
    json.set("fontSize", style.m_font.m_size);
    json.set("charSpacing", style.m_letter_spacing);
    json.set("underline", style.m_is_underlined);
    json.set("textTransform", to_name(style.m_transform));
    json.set("textScript", to_name(style.m_script));
    json.set("fill", XdValue::make_object()).set(
      "value", to_argb(style.m_color));
    json.set("strikethrough", style.m_is_struck_through);
    json.set("hyperlinkUrl", hyperlink);
    return json;
  }

  XdValue make_body(const Interaction& interaction) {
    auto json = XdValue::make_object();
    auto& properties = json.set("properties", XdValue::make_object());
    auto event = "tap";
    if(std::holds_alternative<HoverTrigger>(interaction.m_trigger)) {
      event = "hover";
    } else if(std::holds_alternative<DragTrigger>(interaction.m_trigger)) {
      event = "drag";
    } else if(auto time =
        std::get_if<TimeTrigger>(&interaction.m_trigger)) {
      event = "time";
      properties.set("delay", time->m_delay);
    } else if(auto key = std::get_if<KeyTrigger>(&interaction.m_trigger)) {
      event = "keys-gamepad";
      if(key->m_device == KeyDevice::GAMEPAD) {
        properties.set("keysGamepadDevice", "gamepad");
      } else {
        properties.set("keysGamepadDevice", "keyboard");
      }
      properties.set("keysGamepad", key->m_keys);
    }
    auto set_animation = [&] (const Animation& animation) {
      properties.set("transition", to_name(animation.m_transition));
      properties.set("duration", animation.m_duration);
      properties.set("easing", to_name(animation.m_easing));
    };
    auto action = "artboard-transition";
    if(auto transition =
        std::get_if<ArtboardTransition>(&interaction.m_action)) {
      if(transition->m_animation.m_transition == Transition::AUTO_ANIMATE) {
        action = "auto-animate";
        auto animation = transition->m_animation;
        animation.m_transition = Transition::DISSOLVE;
        set_animation(animation);
      } else {
        set_animation(transition->m_animation);
      }
      properties.set("preserveScrollPosition", transition->m_preserve_scroll);
      if(!transition->m_destination.is_nil()) {
        properties.set("destination", to_id(transition->m_destination));
      }
    } else if(auto overlay = std::get_if<Overlay>(&interaction.m_action)) {
      action = "overlay-transition";
      set_animation(overlay->m_animation);
      if(!overlay->m_destination.is_nil()) {
        properties.set("destination", to_id(overlay->m_destination));
      }
      auto& position =
        properties.set("overlayPosition", XdValue::make_object());
      position.set("x", overlay->m_position.m_x);
      position.set("y", overlay->m_position.m_y);
    } else if(auto state =
        std::get_if<StateTransition>(&interaction.m_action)) {
      action = "state-transition";
      set_animation(state->m_animation);
      if(!state->m_state.is_nil()) {
        properties.set("destination", to_id(state->m_state));
      }
    } else if(std::holds_alternative<PreviousArtboard>(
        interaction.m_action)) {
      action = "previous-artboard";
    } else if(auto playback =
        std::get_if<VideoPlayback>(&interaction.m_action)) {
      action = "video-playback";
      switch(playback->m_command) {
        case VideoCommand::PAUSE:
          properties.set("subtype", "pause");
          break;
        case VideoCommand::STOP:
          properties.set("subtype", "stop");
          break;
        default:
          properties.set("subtype", "play");
          break;
      }
      if(playback->m_target != interaction.m_owner) {
        properties.set("destination", to_id(playback->m_target));
      }
    } else if(auto link = std::get_if<Hyperlink>(&interaction.m_action)) {
      action = "hyperlink";
      properties.set("hyperlinkUrl", link->m_url);
    } else if(auto scroll =
        std::get_if<ScrollTransition>(&interaction.m_action)) {
      action = "scroll-transition";
      set_animation(scroll->m_animation);
      if(!scroll->m_destination.is_nil()) {
        properties.set("destination", to_id(scroll->m_destination));
      }
      if(scroll->m_offset != 0) {
        properties.set("yOffset", scroll->m_offset);
      }
    }
    properties.set("voiceLocale", "en-us");
    json.set("triggerEvent", event);
    json.set("action", action);
    return json;
  }

  std::uint64_t hash_content(const Interaction& interaction) {
    auto body = make_body(interaction);
    return hash_value(body);
  }

  XdValue make_margins(const Margins& margins) {
    auto json = XdValue::make_object();
    json.set("top", margins.m_top);
    json.set("right", margins.m_right);
    json.set("bottom", margins.m_bottom);
    json.set("left", margins.m_left);
    return json;
  }

  bool is_zero(const Margins& margins) {
    return margins == Margins();
  }

  XdValue make_grid_stroke(const Color& color) {
    auto json = XdValue::make_object();
    json.set("type", "solid");
    json.set("color", make_json_color(color));
    return json;
  }

  XdValue make_grid(const LayoutGrid& grid) {
    auto json = XdValue::make_object();
    json.set("rowStroke", make_grid_stroke(grid.m_square_row_color));
    json.set("columnStroke", make_grid_stroke(grid.m_square_column_color));
    json.set("rowSpacing", grid.m_square_size.m_height);
    json.set("columnSpacing", grid.m_square_size.m_width);
    json.set("defaultLayoutWidth", grid.m_default_width);
    json.set("columns", grid.m_column_count);
    json.set("gutter", grid.m_gutter);
    json.set("marginLeft", grid.m_margins.m_left);
    json.set("marginTop", grid.m_margins.m_top);
    json.set("marginRight", grid.m_margins.m_right);
    json.set("marginBottom", grid.m_margins.m_bottom);
    json.set("layoutRowStroke", make_grid_stroke(grid.m_row_color));
    json.set("layoutColumnStroke", make_grid_stroke(grid.m_column_color));
    if(grid.m_type == GridType::LAYOUT) {
      json.set("type", "layout");
    }
    if(grid.m_is_visible) {
      json.set("visible", true);
    }
    return json;
  }

  XdValue make_overrides(const Overrides& overrides, const NodeId& id,
      const std::function<bool (const NodeId&)>& is_recorded) {
    auto json = XdValue::make_object();
    auto added = XdValue::make_array();
    for(auto& entry : overrides.m_added) {
      if(is_recorded(entry)) {
        added.append(to_id(entry));
      }
    }
    if(!added.get_elements().empty()) {
      json.set("addedGuids", std::move(added));
    }
    auto removed = XdValue::make_array();
    for(auto& entry : overrides.m_removed) {
      if(is_recorded(entry)) {
        removed.append(to_id(entry));
      }
    }
    if(!removed.get_elements().empty()) {
      json.set("removedGuids", std::move(removed));
    }
    auto reordered = XdValue::make_object();
    for(auto& entry : overrides.m_reordered) {
      if(is_recorded(entry.first)) {
        reordered.set(to_id(entry.first), to_id(entry.second));
      }
    }
    if(!reordered.get_members().empty()) {
      json.set("reorderedGuids", std::move(reordered));
    }
    if(auto names = overrides.m_properties.find(id);
        names != overrides.m_properties.end()) {
      auto& properties = json.set("propertyNames", XdValue::make_array());
      for(auto& name : names->second) {
        properties.append(name);
      }
    }
    return json;
  }

  XdValue make_sync_map(const std::map<NodeId, NodeId>& sync_map) {
    auto json = XdValue::make_object();
    auto& members = json.get_members();
    members.reserve(sync_map.size());
    auto indices = std::unordered_map<NodeId, std::size_t>();
    indices.reserve(sync_map.size());
    for(auto& entry : sync_map) {
      auto index = indices.try_emplace(entry.second, members.size());
      if(index.second) {
        members.push_back(XdMember(to_id(entry.second), to_id(entry.first)));
      } else {
        members[index.first->second].m_value = to_id(entry.first);
      }
    }
    return json;
  }

  XdValue make_mask_shape(const MaskShape& shape) {
    auto json = XdValue::make_object();
    json.set("type", "shape");
    if(!shape.m_transform.is_identity()) {
      json.set("transform", make_matrix(shape.m_transform));
    }
    json.set("shape", make_geometry(shape.m_geometry, 0));
    return json;
  }

  bool has_number(std::string_view name, std::string_view prefix) {
    if(!name.starts_with(prefix) || name.size() == prefix.size()) {
      return false;
    }
    return std::all_of(name.begin() + prefix.size(), name.end(),
      [] (auto c) {
        return c >= '0' && c <= '9';
      });
  }

  bool has_default_name(std::string_view name, std::string_view prefix) {
    auto kind = prefix.substr(0, prefix.size() - 1);
    return std::any_of(LANGUAGES.begin(), LANGUAGES.end(),
      [&] (const auto& language) {
        auto localized = localize("name", kind, language);
        return name == localized ||
          has_number(name, std::string(localized) + ' ');
      });
  }

  std::string_view get_shape_name_key(
      const Shape& shape, std::string_view name) {
    auto& geometry = shape.m_geometry;
    if(std::holds_alternative<RectangleShape>(geometry)) {
      if(has_default_name(name, "Image ")) {
        return "SHAPE_IMAGE";
      }
      return "SHAPE_RECTANGLE";
    } else if(std::holds_alternative<EllipseShape>(geometry)) {
      return "SHAPE_ELLIPSE";
    } else if(std::holds_alternative<PolygonShape>(geometry)) {
      return "SHAPE_POLYGON";
    } else if(std::holds_alternative<LineShape>(geometry)) {
      return "SHAPE_LINE";
    } else if(std::holds_alternative<PathShape>(geometry)) {
      return "SHAPE_PATH";
    } else if(auto compound = std::get_if<CompoundShape>(&geometry)) {
      if(compound->m_operation == CompoundOperation::UNION) {
        return "SHAPE_UNION";
      } else if(compound->m_operation == CompoundOperation::SUBTRACT) {
        return "SHAPE_SUBTRACT";
      } else if(compound->m_operation == CompoundOperation::INTERSECT) {
        return "SHAPE_INTERSECT";
      } else if(compound->m_operation == CompoundOperation::EXCLUDE) {
        return "SHAPE_EXCLUDE";
      }
    }
    return {};
  }

  std::string_view get_name_key(const Node& node, bool is_component) {
    auto& name = node.m_header.m_name;
    if(name.empty()) {
      return {};
    } else if(auto shape = node.get_shape()) {
      return get_shape_name_key(*shape, name);
    } else if(node.get_text()) {
      return "SHAPE_GROUP";
    } else if(node.get_artboard()) {
      return "SHAPE_RECTANGLE";
    } else if(auto group = node.get_group()) {
      if(group->m_repeat_grid) {
        return "SHAPE_REPEAT_GRID";
      } else if(group->m_scroll_group) {
        return "SHAPE_SCROLLABLE_GROUP";
      } else if(group->m_mask) {
        if(has_default_name(name, "Group ")) {
          return "SHAPE_GROUP";
        }
        return "SHAPE_MASK";
      } else if(is_component || group->m_component_instance) {
        if(has_default_name(name, "Symbol ")) {
          return "SHAPE_SYMBOL";
        }
        return "SHAPE_COMPONENT";
      }
      return "SHAPE_GROUP";
    }
    return {};
  }

  void add_constraints(
      const NodeHeader& header, bool is_unconstrained, XdValue& ux) {
    if(header.m_is_locked) {
      ux.set("locked", true);
    }
    auto& constraints = header.m_constraints;
    if(!constraints.m_is_enabled && !is_unconstrained) {
      ux.set("constraintsDisabled", true);
    }
    if(constraints.m_is_custom) {
      ux.set("customConstraints", true);
    }
    if(constraints.m_left) {
      ux.set("constraintLeft", true);
    }
    if(constraints.m_right) {
      ux.set("constraintRight", true);
    }
    if(constraints.m_top) {
      ux.set("constraintTop", true);
    }
    if(constraints.m_bottom) {
      ux.set("constraintBottom", true);
    }
    if(constraints.m_width) {
      ux.set("constraintWidth", true);
    }
    if(constraints.m_height) {
      ux.set("constraintHeight", true);
    }
    if(header.m_is_marked_for_export) {
      ux.set("markedForExport", true);
    }
    if(header.m_is_fixed_position) {
      ux.set("fixed", true);
    }
    if(auto& lock = header.m_aspect_lock) {
      auto& size = ux.set("aspectLock", XdValue::make_object());
      size.set("width", lock->m_width);
      size.set("height", lock->m_height);
    }
  }

  XdValue make_character_style(const CharacterStyle& style) {
    auto json = XdValue::make_object();
    auto& text = style.m_style;
    json.set("fontFamily", text.m_font.m_family);
    json.set("fontStyle", text.m_font.m_style);
    if(!text.m_font.m_postscript_name.empty()) {
      json.set("postscriptName", text.m_font.m_postscript_name);
    }
    json.set("fontSize", text.m_font.m_size);
    json.set("fontColor", make_json_color(text.m_color));
    json.set("charSpacing", text.m_letter_spacing);
    json.set("lineSpacing", style.m_line_height.value_or(0));
    json.set("paragraphSpacing", style.m_paragraph_spacing);
    json.set("underline", text.m_is_underlined);
    json.set("strikethrough", text.m_is_struck_through);
    json.set("textTransform", to_name(text.m_transform));
    json.set("textScript", to_name(text.m_script));
    json.set("linkId", "");
    return json;
  }

  XdValue make_element(std::string_view id, std::string_view name,
      std::string_view type, std::string_view representation,
      XdValue content, const std::vector<AssetPlacement>& placements) {
    auto json = XdValue::make_object();
    json.set("id", id);
    json.set("type", type);
    json.set("name", name);
    json.set("displayName", XdValue());
    auto& entry = json.set("representations", XdValue::make_array()).append(
      XdValue::make_object());
    entry.set("id", to_id(derive_node_id(QString::fromUtf8(
      id.data(), static_cast<qsizetype>(id.size())), u"representation")));
    entry.set("type", representation);
    entry.set("relationship", "primary");
    entry.set("content", std::move(content));
    if(!placements.empty()) {
      auto& data = json.set("assetGroupData", XdValue::make_object());
      for(auto& placement : placements) {
        data.set(placement.m_group, placement.m_order);
      }
    }
    return json;
  }

  XdValue make_strings(const std::vector<std::string>& strings) {
    auto json = XdValue::make_array();
    for(auto& entry : strings) {
      json.append(entry);
    }
    return json;
  }

  XdValue make_group_data(const std::vector<AssetGroup>& groups) {
    auto json = XdValue::make_object();
    auto& list = json.set("groups", XdValue::make_array());
    for(auto& group : groups) {
      auto& entry = list.append(XdValue::make_object());
      entry.set("name", group.m_name);
      entry.set("id", group.m_id);
      entry.set("order", group.m_order);
      entry.set("parentId", group.m_parent);
      entry.set("childrenIds", make_strings(group.m_children));
      entry.set("elementIds", make_strings(group.m_elements));
    }
    return json;
  }
}

XdGenerator::XdGenerator(const Document& document, XdTextLayout layout)
    : m_document(&document),
      m_layout(std::move(layout)) {
  for(auto& id : document.get_canvas()) {
    if(auto node = document.find(id)) {
      if(node->get_artboard()) {
        m_artboards.push_back(id);
      } else {
        m_pasteboard.push_back(id);
      }
    }
  }
  for(auto& entry : document.get_resources().m_components) {
    m_masters.emplace(entry.second.m_master, &entry.second);
    for(auto& state : entry.second.m_states) {
      m_states.emplace(state.m_tree, StateInfo(&state, entry.first));
    }
  }
  document.get_nodes().for_each([&] (const Node& node) {
    if(auto group = node.get_group()) {
      if(auto& instance = group->m_component_instance) {
        for(auto& state : instance->m_states) {
          m_states.emplace(state.m_tree,
            StateInfo(&state, instance->m_component));
        }
      }
    }
  });
  for(auto& interaction : document.get_prototype().m_interactions) {
    m_interactions[interaction.m_owner].push_back(&interaction);
  }
  for(auto& flow : document.get_prototype().m_flows) {
    m_flows.emplace(flow.m_artboard, &flow);
  }
  m_legacy_swatches =
    find_legacy_swatches(document.get_resources().m_swatches);
  find_inherited();
}

const Document& XdGenerator::get_document() const {
  return *m_document;
}

XdValue XdGenerator::make_node(const Node& node) const {
  auto json = XdValue::make_object();
  auto& header = node.m_header;
  auto parent = node.m_parent.is_nil() ? nullptr :
    m_document->find(node.m_parent);
  auto origin = std::optional<Point>();
  if(parent) {
    if(auto artboard = parent->get_artboard()) {
      origin = artboard->m_frame.get_top_left();
    }
  }
  auto to_document = [&] (const Matrix& transform) {
    if(origin) {
      return make_translation(origin->m_x, origin->m_y) * transform;
    }
    return transform;
  };
  if(auto reference = node.get_reference()) {
    json.set("type", "syncRef");
    if(!header.m_name.empty()) {
      json.set("name", header.m_name);
    }
    json.set("guid", to_id(header.m_id));
    json.set("syncSourceGuid", to_id(reference->m_source));
    if(reference->m_transform) {
      json.set("transform", make_matrix(to_document(*reference->m_transform)));
    }
    auto ux = make_ux(node);
    if(!ux.get_members().empty()) {
      json.set("meta", XdValue::make_object()).set("ux", std::move(ux));
    }
    auto style = make_style(node, nullptr);
    if(!style.get_members().empty()) {
      json.set("style", std::move(style));
    }
    if(!header.m_is_visible) {
      json.set("visible", false);
    }
    if(reference->m_has_children) {
      auto key = "group";
      auto source = m_document->find(reference->m_source);
      for(auto depth = 0; source && source->get_reference() && depth != 64;
          ++depth) {
        source = m_document->find(source->get_reference()->m_source);
      }
      if(source && source->get_shape()) {
        key = "shape";
      }
      json.set(key, XdValue::make_object()).set(
        "children", make_placeholders(node.m_children.size()));
    }
    return json;
  }
  if(auto artboard = node.get_artboard()) {
    json.set("type", "artboard");
    auto ux = make_ux(node);
    auto is_localized = static_cast<bool>(ux.find("nameL10N"));
    if(!is_localized) {
      json.set("id", to_id(header.m_id));
    }
    json.set("meta", XdValue::make_object()).set("ux", std::move(ux));
    if(is_localized) {
      json.set("id", to_id(header.m_id));
    }
    auto style = make_style(node, nullptr);
    style.set("fill", make_fill(artboard->m_background));
    json.set("style", std::move(style));
    if(!header.m_is_visible) {
      json.set("visible", false);
    }
    auto& content = json.set("artboard", XdValue::make_object());
    content.set("children", make_placeholders(node.m_children.size()));
    content.set("meta", XdValue::make_object()).set(
      "ux", XdValue::make_object()).set("path", to_id(header.m_id));
    content.set("ref", to_id(header.m_id));
    return json;
  }
  if(node.get_shape()) {
    json.set("type", "shape");
  } else if(node.get_text()) {
    json.set("type", "text");
  } else {
    json.set("type", "group");
  }
  if(!header.m_name.empty()) {
    json.set("name", header.m_name);
  }
  json.set("id", to_id(header.m_id));
  if(auto transform = to_document(header.m_transform);
      header.m_transform_3d) {
    json.set("transform", make_matrix(*header.m_transform_3d, transform));
  } else if(!transform.is_identity()) {
    json.set("transform", make_matrix(transform));
  }
  auto& ux = json.set("meta", XdValue::make_object()).set("ux", make_ux(node));
  if(origin) {
    if(auto& transform = header.m_transform_3d) {
      ux.set("localTransform", make_matrix(*transform, header.m_transform));
    } else {
      ux.set("localTransform", make_matrix(header.m_transform));
    }
  }
  if(auto shape = node.get_shape()) {
    if(shape->m_video) {
      if(auto style = make_style(node, nullptr);
          !style.get_members().empty()) {
        json.set("style", std::move(style));
      }
    } else {
      json.set("style", make_style(node, &shape->m_style));
    }
    if(!header.m_is_visible) {
      json.set("visible", false);
    }
    auto& geometry = json.set("shape",
      make_geometry(shape->m_geometry, node.m_children.size()));
    if(auto& video = shape->m_video) {
      auto& ux = geometry.set("meta", XdValue::make_object()).set(
        "ux", XdValue::make_object());
      ux.set("subtype", "video");
      ux.set("mediaFill", make_fill(video->m_poster));
      ux.set("mediaFileGuid", to_uid(video->m_media));
      if(video->m_is_looping) {
        ux.set("loop", true);
      }
    }
  } else if(auto text = node.get_text()) {
    auto style = make_style(node, &text->m_style);
    style.set("font", make_font(text->m_base_style.m_font));
    auto attributes = XdValue::make_object();
    if(text->m_base_style.m_letter_spacing != 0) {
      attributes.set("letterSpacing", text->m_base_style.m_letter_spacing);
    }
    auto& paragraph = text->m_paragraph_style;
    if(paragraph.m_line_height) {
      attributes.set("lineHeight", *paragraph.m_line_height);
    }
    if(paragraph.m_paragraph_spacing != 0) {
      attributes.set("paragraphAfterSpacing", paragraph.m_paragraph_spacing);
    }
    if(paragraph.m_alignment == TextAlignment::CENTER) {
      attributes.set("paragraphAlign", "center");
    } else if(paragraph.m_alignment == TextAlignment::RIGHT) {
      attributes.set("paragraphAlign", "right");
    }
    if(!attributes.get_members().empty()) {
      style.set("textAttributes", std::move(attributes));
    }
    json.set("style", std::move(style));
    if(!header.m_is_visible) {
      json.set("visible", false);
    }
    json.set("text", make_text(node, *text));
  } else {
    auto style = make_style(node, nullptr);
    if(!style.get_members().empty()) {
      json.set("style", std::move(style));
    }
    if(!header.m_is_visible) {
      json.set("visible", false);
    }
    json.set("group", XdValue::make_object()).set(
      "children", make_placeholders(node.m_children.size()));
  }
  return json;
}

std::vector<NodeId> XdGenerator::get_states(const Node& node) const {
  auto states = std::vector<NodeId>();
  if(auto master = m_masters.find(node.get_id()); master != m_masters.end()) {
    for(auto& state : master->second->m_states) {
      states.push_back(state.m_tree);
    }
  } else if(auto group = node.get_group()) {
    if(auto& instance = group->m_component_instance) {
      for(auto& state : instance->m_states) {
        states.push_back(state.m_tree);
      }
    }
  }
  return states;
}

XdValue XdGenerator::make_part(std::size_t child_count) const {
  auto json = XdValue::make_object();
  json.set("version", "1.5.0");
  json.set("children", make_placeholders(child_count));
  json.set("resources", XdValue::make_object()).set("href", RESOURCES_HREF);
  json.set("artboards", XdValue::make_object()).set("href", RESOURCES_HREF);
  return json;
}

XdValue XdGenerator::make_resources(
    const std::vector<ComponentId>& symbols) const {
  auto& resources = m_document->get_resources();
  auto json = XdValue::make_object();
  json.set("version", "1.5.0");
  json.set("children", XdValue::make_array());
  auto& content = json.set("resources", XdValue::make_object());
  auto& ux = content.set("meta", XdValue::make_object()).set(
    "ux", XdValue::make_object());
  auto& swatches = ux.set("colorSwatches", XdValue::make_array());
  auto library = XdValue::make_object();
  library.set("version", 5);
  library.set("isStickerSheet", false);
  library.set("hashedMetadata", XdValue::make_object());
  auto& elements = library.set("elements", XdValue::make_array());
  for(auto& swatch : resources.m_swatches) {
    if(m_legacy_swatches.contains(swatch.m_id)) {
      swatches.append(make_json_color(swatch.m_color));
      continue;
    }
    auto id = to_id(swatch.m_id);
    if(swatch.m_gradient.empty()) {
      elements.append(make_element(id, swatch.m_name,
        "application/vnd.adobe.element.color+dcx",
        "application/vnd.adobe.xdcolor+json", make_json_color(swatch.m_color),
        swatch.m_placements));
    } else {
      auto stops = XdValue::make_array();
      for(auto& stop : swatch.m_gradient) {
        auto& entry = stops.append(XdValue::make_object());
        entry.set("color", make_json_color(stop.m_color));
        entry.set("stop", stop.m_offset);
      }
      elements.append(make_element(id, swatch.m_name,
        "application/vnd.adobe.element.gradient+dcx",
        "application/vnd.adobe.xdlineargradient+json", std::move(stops),
        swatch.m_placements));
    }
  }
  for(auto& style : resources.m_character_styles) {
    elements.append(make_element(to_id(style.m_id), style.m_name,
      "application/vnd.adobe.element.characterstyle+dcx",
      "application/vnd.adobe.characterstyle+json",
      make_character_style(style), style.m_placements));
  }
  library.set("groupData", make_group_data(resources.m_asset_groups));
  ux.set("documentLibrary", std::move(library));
  ux.set("gridDefaults", make_grid_defaults(resources.m_grid_defaults));
  if(resources.m_color_profile == ColorProfile::UNMANAGED) {
    ux.set("colorProfile", "unmanaged");
  } else if(resources.m_color_profile == ColorProfile::SRGB) {
    ux.set("colorProfile", "srgb");
  }
  ux.set("symbols", make_placeholders(symbols.size()));
  ux.set("symbolsMetadata", XdValue::make_object()).set(
    "usingNestedSymbolSyncing", true);
  content.set("gradients", XdValue::make_object());
  content.set("clipPaths", XdValue::make_object());
  auto& artboards = json.set("artboards", XdValue::make_object());
  for(auto& id : m_artboards) {
    auto& node = m_document->get(id);
    auto& artboard = *node.get_artboard();
    auto& frame = artboards.set(to_id(id), XdValue::make_object());
    frame.set("x", artboard.m_frame.m_x);
    frame.set("y", artboard.m_frame.m_y);
    frame.set("width", artboard.m_frame.m_width);
    frame.set("height", artboard.m_frame.m_height);
    frame.set("name", node.m_header.m_name);
    if(artboard.m_viewport_height) {
      frame.set("viewportHeight", *artboard.m_viewport_height);
    }
  }
  return json;
}

XdValue XdGenerator::make_interactions() const {
  auto json = XdValue::make_object();
  json.set("version", "0.24");
  auto owners = XdValue::make_object();
  for(auto& interaction : m_document->get_prototype().m_interactions) {
    if(m_inherited.contains(interaction.m_id)) {
      continue;
    }
    auto key = to_id(interaction.m_owner);
    auto list = owners.find(key);
    if(!list) {
      list = &owners.set(key, XdValue::make_array());
    }
    auto entry = make_body(interaction);
    entry.set("id", to_id(interaction.m_id));
    list->append(std::move(entry));
  }
  if(!owners.get_members().empty()) {
    json.set("interactions", std::move(owners));
  }
  auto& home = m_document->get_prototype().m_home_artboard;
  if(!home.is_nil()) {
    json.set("homeArtboard", to_id(home));
  }
  return json;
}

const std::vector<NodeId>& XdGenerator::get_artboards() const {
  return m_artboards;
}

const std::vector<NodeId>& XdGenerator::get_pasteboard() const {
  return m_pasteboard;
}

std::uint64_t XdGenerator::get_cache_hash(const XdValue& node) {
  auto hash = std::uint64_t(0x7A3B9E2D1C4F5A6BULL);
  for(auto key : {"transform", "shape", "style", "text"}) {
    hash *= 0x100000001B3ULL;
    if(auto member = node.find(key)) {
      hash ^= hash_value(*member);
    }
  }
  return hash;
}

void XdGenerator::find_inherited() {
  auto& interactions = m_document->get_prototype().m_interactions;
  if(interactions.empty()) {
    return;
  }
  auto buckets =
    std::unordered_map<std::uint64_t, std::vector<const Interaction*>>();
  for(auto& interaction : interactions) {
    buckets[hash_content(interaction)].push_back(&interaction);
  }
  for(auto& bucket : buckets) {
    auto& members = bucket.second;
    if(members.size() < 2) {
      continue;
    }
    auto sources = std::vector<const Interaction*>();
    auto texts = std::unordered_map<const Interaction*, QString>();
    auto get_text = [&] (const Interaction* interaction) -> const QString& {
      auto entry = texts.find(interaction);
      if(entry == texts.end()) {
        entry = texts.emplace(interaction,
          QString::fromStdString(to_id(interaction->m_id))).first;
      }
      return entry->second;
    };
    for(auto copy : members) {
      auto owner = QString::fromStdString(to_id(copy->m_owner));
      auto is_found = false;
      auto try_source = [&] (const Interaction* source) {
        if(source == copy || source->m_owner == copy->m_owner) {
          return false;
        }
        if(derive_node_id(get_text(source), owner) != copy->m_id) {
          return false;
        }
        m_inherited.emplace(copy->m_id, source->m_id);
        return true;
      };
      for(auto source : sources) {
        if(try_source(source)) {
          is_found = true;
          break;
        }
      }
      if(is_found) {
        continue;
      }
      for(auto source : members) {
        if(try_source(source)) {
          sources.push_back(source);
          break;
        }
      }
    }
  }
}

const Overrides* XdGenerator::find_scope(const Node& node) const {
  auto parent = node.m_parent;
  auto id = node.get_id();
  for(auto depth = 0; depth != 256; ++depth) {
    if(auto state = m_states.find(id); state != m_states.end() &&
        id != node.get_id()) {
      return &state->second.m_state->m_overrides;
    }
    if(parent.is_nil()) {
      return nullptr;
    }
    auto current = m_document->find(parent);
    if(!current) {
      return nullptr;
    }
    if(auto group = current->get_group()) {
      if(group->m_component_instance) {
        return &group->m_component_instance->m_overrides;
      }
    }
    id = parent;
    parent = current->m_parent;
  }
  return nullptr;
}

std::function<bool (const NodeId&)> XdGenerator::make_root_recorder(
    const Overrides& overrides) const {
  return [this, &overrides] (const NodeId& entry) {
    auto owner = overrides.m_owners.find(entry);
    if(owner == overrides.m_owners.end()) {
      return true;
    }
    auto node = m_document->find(owner->second);
    if(!node || m_masters.contains(owner->second) ||
        m_states.contains(owner->second)) {
      return true;
    }
    if(auto group = node->get_group(); group && group->m_component_instance) {
      return true;
    }
    return find_scope(*node) != &overrides;
  };
}

XdValue XdGenerator::make_ux(const Node& node) const {
  auto ux = XdValue::make_object();
  auto& header = node.m_header;
  auto& id = node.get_id();
  if(header.m_has_custom_name) {
    ux.set("hasCustomName", true);
  } else if(auto key = get_name_key(node, m_masters.contains(id));
      !key.empty()) {
    ux.set("nameL10N", key);
  }
  if(header.m_is_naming_disabled) {
    ux.set("disableNaming", true);
  }
  if(!node.get_reference() && !node.get_artboard() &&
      header.m_rotation != 0) {
    ux.set("rotation", header.m_rotation);
  }
  add_constraints(header,
    node.get_reference() && header.m_constraints == Constraints(), ux);
  if(auto& transform = header.m_transform_3d) {
    if(transform->m_rotation_x != 0) {
      ux.set("rotationX", transform->m_rotation_x);
    }
    if(transform->m_rotation_y != 0) {
      ux.set("rotationY", transform->m_rotation_y);
    }
    auto& center = ux.set("perspectiveCenter", XdValue::make_object());
    center.set("x", transform->m_perspective_center.m_x);
    center.set("y", transform->m_perspective_center.m_y);
  }
  if(!node.m_parent.is_nil()) {
    if(auto parent = m_document->find(node.m_parent)) {
      if(auto group = parent->get_group()) {
        if(group->m_stack) {
          if(auto spacing = group->m_stack->m_spacing.find(id);
              spacing != group->m_stack->m_spacing.end()) {
            ux.set("contentStackMarginAfter", spacing->second);
          }
        }
      }
    }
  }
  if(auto master = m_masters.find(id); master != m_masters.end()) {
    auto& component = *master->second;
    ux.set("symbolId", to_id(component.m_id));
    ux.set("width", component.m_size.m_width);
    ux.set("height", component.m_size.m_height);
    ux.set("componentType", "componentNoBounds");
    if(!component.m_default_state.is_nil()) {
      ux.set("stateId", to_id(component.m_default_state));
    }
    if(!component.m_states.empty()) {
      ux.set("states", make_placeholders(component.m_states.size()));
    }
    if(component.m_link) {
      ux.set("linkedElementRef", component.m_link->m_reference);
      ux.set("resolvedRefName", component.m_link->m_source_name);
      if(component.m_link->m_last_modified != 0) {
        ux.set(
          "linkedElementLastModified", component.m_link->m_last_modified);
      }
    }
  } else if(auto state = m_states.find(id); state != m_states.end()) {
    auto& info = state->second;
    ux.set("symbolId", to_id(info.m_component));
    ux.set("width", info.m_state->m_size.m_width);
    ux.set("height", info.m_state->m_size.m_height);
    ux.set("componentType", "componentNoBounds");
    ux.set("syncMap", make_sync_map(info.m_state->m_sync_map));
    ux.set("stateId", to_id(info.m_state->m_id));
    auto overrides = make_overrides(info.m_state->m_overrides, id,
      make_root_recorder(info.m_state->m_overrides));
    if(!overrides.get_members().empty()) {
      ux.set("symbolOverrides", std::move(overrides));
    }
  } else if(auto group = node.get_group();
      group && group->m_component_instance) {
    auto& instance = *group->m_component_instance;
    ux.set("symbolId", to_id(instance.m_component));
    auto state = instance.m_state;
    if(state.is_nil()) {
      auto& components = m_document->get_resources().m_components;
      if(auto component = components.find(instance.m_component);
          component != components.end()) {
        state = component->second.m_default_state;
      }
    }
    if(!state.is_nil()) {
      ux.set("stateId", to_id(state));
    }
    ux.set("width", instance.m_size.m_width);
    ux.set("height", instance.m_size.m_height);
    ux.set("componentType", "componentNoBounds");
    if(instance.m_is_main) {
      ux.set("isMaster", true);
    }
    ux.set("syncMap", make_sync_map(instance.m_sync_map));
    auto overrides = make_overrides(
      instance.m_overrides, id, make_root_recorder(instance.m_overrides));
    if(!overrides.get_members().empty()) {
      ux.set("symbolOverrides", std::move(overrides));
    }
    if(instance.m_is_name_inherited) {
      ux.set("isCustomNameInherited", true);
    }
    if(!instance.m_states.empty()) {
      ux.set("states", make_placeholders(instance.m_states.size()));
    }
  } else if(auto scope = find_scope(node)) {
    auto overrides = make_overrides(*scope, id, [&] (const NodeId& entry) {
      auto owner = scope->m_owners.find(entry);
      return owner != scope->m_owners.end() && owner->second == id;
    });
    if(!overrides.get_members().empty()) {
      ux.set("symbolOverrides", std::move(overrides));
    }
  }
  if(auto group = node.get_group()) {
    if(group->m_mask) {
      auto& clip = ux.set("clipPathResources", XdValue::make_object());
      clip.set("type", "clipPath");
      auto& children = clip.set("children", XdValue::make_array());
      for(auto& shape : group->m_mask->m_shapes) {
        children.append(make_mask_shape(shape));
      }
    }
    if(auto& grid = group->m_repeat_grid) {
      auto& json = ux.set("repeatGrid", XdValue::make_object());
      json.set("width", grid->m_size.m_width);
      json.set("height", grid->m_size.m_height);
      json.set("paddingX", grid->m_column_spacing);
      json.set("paddingY", grid->m_row_spacing);
      json.set("columns", grid->m_column_count);
      json.set("rows", grid->m_row_count);
      json.set("cellWidth", grid->m_cell_size.m_width);
      json.set("cellHeight", grid->m_cell_size.m_height);
    }
    if(auto& scroll = group->m_scroll_group) {
      auto& viewport = scroll->m_viewport;
      if(scroll->m_direction == ScrollDirection::HORIZONTAL) {
        ux.set("scrollingType", "horizontal");
        ux.set("viewportWidth", viewport.m_width);
        if(viewport.m_height != 0) {
          ux.set("viewportHeight", viewport.m_height);
        }
      } else if(scroll->m_direction == ScrollDirection::VERTICAL) {
        ux.set("scrollingType", "vertical");
        if(viewport.m_width != 0) {
          ux.set("viewportWidth", viewport.m_width);
        }
        ux.set("viewportHeight", viewport.m_height);
      } else {
        ux.set("scrollingType", "panning");
        ux.set("viewportWidth", viewport.m_width);
        ux.set("viewportHeight", viewport.m_height);
      }
      if(scroll->m_offset.m_x != 0) {
        ux.set("offsetX", scroll->m_offset.m_x);
      }
      if(scroll->m_offset.m_y != 0) {
        ux.set("offsetY", scroll->m_offset.m_y);
      }
    }
    if(auto& stack = group->m_stack) {
      ux.set("isContentAware", true);
      ux.set("contentStackType",
        stack->m_orientation == Orientation::HORIZONTAL ? "horizontal" :
          "vertical");
      if(!is_zero(stack->m_padding)) {
        ux.set("contentPadding", make_margins(stack->m_padding));
      }
      if(stack->m_has_background) {
        ux.set("hasContentBackground", true);
      }
    } else if(auto& padding = group->m_padding) {
      ux.set("contentPadding", make_margins(padding->m_padding));
      if(padding->m_is_content_aware) {
        ux.set("isContentAware", true);
      }
      if(padding->m_has_background) {
        ux.set("hasContentBackground", true);
      }
    }
  } else if(auto artboard = node.get_artboard()) {
    if(artboard->m_layout_grid) {
      ux.set("gridStyle", make_grid(*artboard->m_layout_grid));
    }
    if(!artboard->m_guides.empty()) {
      auto& model = ux.set("guidesModel", XdValue::make_object());
      for(auto orientation :
          {Orientation::HORIZONTAL, Orientation::VERTICAL}) {
        auto& list = model.set(orientation == Orientation::HORIZONTAL ?
          "horizontalGuides" : "verticalGuides", XdValue::make_object()).set(
            "guides", XdValue::make_array());
        for(auto& guide : artboard->m_guides) {
          if(guide.m_orientation == orientation) {
            auto& entry = list.append(XdValue::make_object());
            entry.set("position", guide.m_position);
            if(!guide.m_id.empty()) {
              entry.set("guid", guide.m_id);
            }
          }
        }
      }
    }
    if(auto flow = m_flows.find(node.get_id()); flow != m_flows.end()) {
      ux.set("isFlowRoot", true);
      if(!flow->second->m_name.empty()) {
        ux.set("flowName", flow->second->m_name);
      }
    }
  } else if(auto text = node.get_text()) {
    ux.set("singleTextObject", true);
    if(text->m_is_flipped_vertically) {
      ux.set("flipY", true);
    }
    auto& ranges = ux.set("rangedStyles", XdValue::make_array());
    if(text->m_runs.empty()) {
      ranges.append(make_range(text->m_base_style,
        text->m_content->size(), CharacterStyleId(), std::string(),
        text->m_base_style));
    }
    for(auto& run : text->m_runs) {
      ranges.append(make_range(run.m_style, run.m_length,
        run.m_character_style, run.m_hyperlink, text->m_base_style));
    }
    if(!text->m_repeat_data.empty()) {
      auto& data = ux.set("repeatOverrides", XdValue::make_array());
      for(auto& value : text->m_repeat_data) {
        auto& entry = data.append(XdValue::make_object());
        entry.set("overrideType", "text");
        entry.set("data", to_utf8(value));
        entry.set("type", "RepeatOverride");
      }
    }
  }
  if(auto interactions = m_interactions.find(id);
      interactions != m_interactions.end()) {
    auto& list = ux.set("interactions", XdValue::make_array());
    for(auto interaction : interactions->second) {
      list.append(make_node_interaction(*interaction));
    }
  }
  if(auto master = m_masters.find(id);
      master != m_masters.end() && !master->second->m_placements.empty()) {
    auto& data = ux.set("assetGroupData", XdValue::make_object());
    for(auto& placement : master->second->m_placements) {
      data.set(placement.m_group, placement.m_order);
    }
  }
  return ux;
}

XdValue XdGenerator::make_style(const Node& node, const Style* style) const {
  auto json = XdValue::make_object();
  auto& header = node.m_header;
  if(style) {
    json.set("fill", make_fill(style->m_fill));
    if(style->m_stroke) {
      json.set("stroke", make_stroke(*style->m_stroke));
    }
    if(!style->m_effects.empty()) {
      json.set("filters", make_filters(style->m_effects));
    }
  }
  if(header.m_opacity != 1) {
    json.set("opacity", header.m_opacity);
  }
  if(header.m_blend_mode != BlendMode::NORMAL) {
    json.set("blendMode", to_name(header.m_blend_mode));
  }
  if(header.m_is_isolated) {
    json.set("isolation", "isolate");
  }
  return json;
}

XdValue XdGenerator::make_text(const Node& node, const Text& text) const {
  auto json = XdValue::make_object();
  auto& frame = json.set("frame", XdValue::make_object());
  if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
    frame.set("type", "area");
    frame.set("width", area->m_size.m_width);
    frame.set("height", area->m_size.m_height);
  } else if(auto automatic = std::get_if<AutoHeightFrame>(&text.m_frame)) {
    frame.set("type", "autoHeight");
    frame.set("width", automatic->m_width);
  } else {
    frame.set("type", "positioned");
  }
  if(auto paragraphs = make_layout(text); paragraphs.is_array()) {
    json.set("paragraphs", std::move(paragraphs));
  }
  json.set("rawText", to_utf8(*text.m_content));
  return json;
}

XdValue XdGenerator::make_layout(const Text& text) const {
  auto layout = std::optional<StoredLayout>();
  if(text.m_layout) {
    layout = **text.m_layout;
  } else if(m_layout) {
    layout = m_layout(text);
  }
  if(!layout) {
    return XdValue();
  }
  auto paragraphs = XdValue::make_array();
  for(auto& paragraph : layout->m_paragraphs) {
    auto& lines = paragraphs.append(XdValue::make_object()).set(
      "lines", XdValue::make_array());
    for(auto& line : paragraph.m_lines) {
      auto& runs = lines.append(XdValue::make_array());
      for(auto& run : line.m_runs) {
        auto& json = runs.append(XdValue::make_object());
        json.set("x", run.m_position.m_x);
        json.set("y", run.m_position.m_y);
        json.set("from", run.m_from);
        json.set("to", run.m_to);
        auto& covering = find_text_style(text, run.m_from);
        auto shift = get_vertical_align(covering);
        if(run.m_font || run.m_color || shift != 0) {
          auto& style = json.set("style", XdValue::make_object());
          if(run.m_font) {
            style.set("font", make_font(*run.m_font));
          } else if(shift != 0) {
            style.set("font", XdValue::make_object()).set(
              "size", get_script_size(covering));
          }
          if(run.m_color) {
            auto& fill = style.set("fill", XdValue::make_object());
            fill.set("type", "solid");
            fill.set("color", make_json_color(*run.m_color));
          }
          if(shift != 0) {
            style.set("textAttributes", XdValue::make_object()).set(
              "verticalAlign", shift);
          }
        }
      }
    }
  }
  return paragraphs;
}

XdValue XdGenerator::make_node_interaction(
    const Interaction& interaction) const {
  auto json = XdValue::make_object();
  if(auto source = m_inherited.find(interaction.m_id);
      source != m_inherited.end()) {
    json.set("guid", to_id(source->second));
    json.set("inherited", true);
    json.set("enabled", interaction.m_is_enabled);
    json.set("valid", true);
    return json;
  }
  json.set("guid", to_id(interaction.m_id));
  json.set("inherited", false);
  json.set("enabled", interaction.m_is_enabled);
  json.set("valid", true);
  auto& data = json.set("data", XdValue::make_object());
  data.set("interaction", make_body(interaction));
  data.set("version", "0.24");
  return json;
}

std::string Rellu::to_uid(const ImageId& id) {
  static constexpr auto HEX = std::string_view("0123456789abcdef");
  auto uid = std::string();
  uid.reserve(32);
  for(auto byte : id.get_bytes()) {
    uid += HEX[byte >> 4];
    uid += HEX[byte & 0xF];
  }
  return uid;
}

std::string Rellu::to_svg_path(const Path& path) {
  auto out = std::string();
  for(auto& segment : path.get_segments()) {
    if(!out.empty()) {
      out += ' ';
    }
    auto count = 0;
    switch(segment.m_type) {
      case SegmentType::MOVE:
        out += 'M';
        count = 1;
        break;
      case SegmentType::LINE:
        out += 'L';
        count = 1;
        break;
      case SegmentType::QUADRATIC:
        out += 'Q';
        count = 2;
        break;
      case SegmentType::CUBIC:
        out += 'C';
        count = 3;
        break;
      case SegmentType::CLOSE:
        out += 'Z';
        break;
    }
    for(auto i = 0; i != count; ++i) {
      out += ' ';
      append_path_number(segment.m_points[i].m_x, out);
      out += ' ';
      append_path_number(segment.m_points[i].m_y, out);
    }
  }
  return out;
}

std::unordered_map<SwatchId, std::size_t> Rellu::find_legacy_swatches(
    const std::vector<ColorSwatch>& swatches) {
  auto legacy = std::unordered_map<SwatchId, std::size_t>();
  auto index = std::size_t(0);
  for(auto& swatch : swatches) {
    if(swatch.m_id ==
        derive_node_id(u"colorSwatches", QString::number(index))) {
      legacy.emplace(swatch.m_id, index);
      ++index;
    }
  }
  return legacy;
}

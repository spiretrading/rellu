#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  SwatchId read_swatch(XdObject& fill, XdObject& color, XdImport& import) {
    auto link = fill.get_string("linkId"_L1);
    if(auto color_link = color.get_string("linkId"_L1)) {
      link = color_link;
    }
    if(!link || link->isEmpty()) {
      return SwatchId();
    }
    auto id = to_node_id(*link);
    if(auto ids = import.get_states().find<XdResourceIds>()) {
      if(ids->m_swatches.contains(id)) {
        return id;
      }
    }
    return SwatchId();
  }

  std::optional<std::vector<GradientStop>> read_stops(XdArray& stops) {
    auto result = std::vector<GradientStop>();
    result.reserve(stops.get_size());
    for(auto i = std::size_t(0); i != stops.get_size(); ++i) {
      auto stop = stops.get_object(i);
      if(!stop) {
        return std::nullopt;
      }
      auto offset = stop->get_number("offset"_L1);
      auto color_json = stop->get_object("color"_L1);
      if(!offset || !color_json) {
        return std::nullopt;
      }
      auto color = read_color(*color_json);
      if(!color) {
        return std::nullopt;
      }
      result.push_back(GradientStop(*offset, *color));
    }
    return result;
  }

  std::optional<Fill> read_gradient(XdObject& gradient) {
    auto ux = get_ux(gradient);
    if(!ux) {
      return std::nullopt;
    }
    auto resources = ux->get_object("gradientResources"_L1);
    if(!resources) {
      return std::nullopt;
    }
    auto fill = GradientFill();
    auto& json = gradient.get_json();
    auto units = json.value("units"_L1).toString();
    if(units == "objectBoundingBox"_L1) {
      fill.m_units = GradientUnits::OBJECT_BOUNDING_BOX;
    } else if(units == "userSpaceOnUse"_L1) {
      fill.m_units = GradientUnits::USER_SPACE;
    } else if(!units.isEmpty()) {
      return std::nullopt;
    }
    gradient.consume("units"_L1);
    auto type = resources->get_json().value("type"_L1).toString();
    if(type == "linear"_L1) {
      auto x1 = gradient.get_number("x1"_L1);
      auto y1 = gradient.get_number("y1"_L1);
      auto x2 = gradient.get_number("x2"_L1);
      auto y2 = gradient.get_number("y2"_L1);
      if(!x1 || !y1 || !x2 || !y2) {
        return std::nullopt;
      }
      fill.m_geometry = LinearGradient(Point(*x1, *y1), Point(*x2, *y2));
    } else if(type == "radial"_L1) {
      auto cx = gradient.get_number("cx"_L1);
      auto cy = gradient.get_number("cy"_L1);
      auto r = gradient.get_number("r"_L1);
      if(!cx || !cy || !r) {
        return std::nullopt;
      }
      auto fx = gradient.get_number("fx"_L1, *cx);
      auto fy = gradient.get_number("fy"_L1, *cy);
      fill.m_geometry =
        RadialGradient(Point(*cx, *cy), *r, Point(fx, fy));
    } else if(type == "angular"_L1) {
      auto x = ux->get_number("x"_L1);
      auto y = ux->get_number("y"_L1);
      auto rotation = ux->get_number("rotation"_L1);
      if(!x || !y || !rotation) {
        return std::nullopt;
      }
      fill.m_geometry = AngularGradient(Point(*x, *y), *rotation);
    } else {
      return std::nullopt;
    }
    resources->consume("type"_L1);
    if(auto transform = gradient.get_object("transform"_L1)) {
      if(auto matrix = read_matrix(*transform)) {
        fill.m_transform = *matrix;
      }
    }
    if(auto stops = resources->get_array("stops"_L1)) {
      auto result = read_stops(*stops);
      if(!result) {
        return std::nullopt;
      }
      fill.m_stops = std::move(*result);
    }
    return fill;
  }

  std::optional<Fill> read_pattern(XdObject& pattern, XdImport& import) {
    auto ux = get_ux(pattern);
    if(!ux) {
      return std::nullopt;
    }
    auto uid = ux->get_string("uid"_L1);
    if(!uid) {
      return std::nullopt;
    }
    auto fill = ImageFill();
    fill.m_image = to_node_id(*uid);
    auto ids = import.get_states().find<XdResourceIds>();
    if(!ids || !ids->m_images.contains(fill.m_image)) {
      import.warn("Missing image resource");
    }
    fill.m_size = Size(pattern.get_number("width"_L1, 0),
      pattern.get_number("height"_L1, 0));
    pattern.ignore({"href"_L1});
    auto& json = ux->get_json();
    auto behavior = json.value("scaleBehavior"_L1).toString();
    if(behavior == "fill"_L1) {
      fill.m_scale_behavior = ScaleBehavior::FILL;
      ux->consume("scaleBehavior"_L1);
    } else if(behavior == "cover"_L1) {
      fill.m_scale_behavior = ScaleBehavior::COVER;
      ux->consume("scaleBehavior"_L1);
    }
    fill.m_offset = Point(ux->get_number("offsetX"_L1, 0),
      ux->get_number("offsetY"_L1, 0));
    fill.m_scale = ux->get_number("scale"_L1, 1);
    fill.m_horizontal_scale = ux->get_number("scaleX"_L1, 1);
    fill.m_vertical_scale = ux->get_number("scaleY"_L1, 1);
    fill.m_is_flipped_horizontally = ux->get_bool("flipX"_L1, false);
    fill.m_is_flipped_vertically = ux->get_bool("flipY"_L1, false);
    ux->ignore({"hrefLastModifiedDate"_L1});
    return fill;
  }

  std::optional<Effect> read_shadow(XdObject& shadow, bool is_inner,
      bool is_visible) {
    auto dx = shadow.get_number("dx"_L1);
    auto dy = shadow.get_number("dy"_L1);
    auto r = shadow.get_number("r"_L1);
    auto color_json = shadow.get_object("color"_L1);
    if(!dx || !dy || !r || !color_json) {
      return std::nullopt;
    }
    auto color = read_color(*color_json);
    if(!color) {
      return std::nullopt;
    }
    if(is_inner) {
      return InnerShadow(Point(*dx, *dy), *r, *color, is_visible);
    }
    return DropShadow(Point(*dx, *dy), *r, *color, is_visible);
  }

  void read_shadows(XdObject& params, QLatin1StringView key, bool is_inner,
      bool is_visible, std::size_t limit, std::vector<Effect>& effects) {
    auto shadows = params.get_array(key);
    if(!shadows) {
      return;
    }
    for(auto i = std::size_t(0); i != shadows->get_size(); ++i) {
      if(auto shadow = shadows->get_object(i)) {
        if(i >= limit) {
          shadow->consume_all();
        } else if(auto effect = read_shadow(*shadow, is_inner, is_visible)) {
          effects.push_back(std::move(*effect));
        }
      }
    }
  }

  std::optional<Effect> read_blur(XdObject& params, bool is_visible) {
    auto amount = params.get_number("blurAmount"_L1);
    if(!amount) {
      return std::nullopt;
    }
    auto blur = Blur();
    blur.m_amount = *amount;
    blur.m_brightness = params.get_number("brightnessAmount"_L1, 0);
    blur.m_fill_opacity = params.get_number("fillOpacity"_L1, 1);
    blur.m_is_background = params.get_bool("backgroundEffect"_L1, false);
    blur.m_is_visible = is_visible;
    return blur;
  }

  std::optional<Fill> read_paint(
      XdObject& fill, const QString& type, XdImport& import) {
    if(type == "solid"_L1) {
      auto color_json = fill.get_object("color"_L1);
      if(!color_json) {
        return std::nullopt;
      }
      auto color = read_color(*color_json);
      if(!color) {
        return std::nullopt;
      }
      return SolidFill(*color, read_swatch(fill, *color_json, import));
    } else if(type == "gradient"_L1) {
      if(auto gradient = fill.get_object("gradient"_L1)) {
        return read_gradient(*gradient);
      }
    } else if(type == "pattern"_L1) {
      if(auto pattern = fill.get_object("pattern"_L1)) {
        return read_pattern(*pattern, import);
      }
    }
    return std::nullopt;
  }

  void import_style(XdObject& json, Style& style, XdImport& import) {
    if(auto fill_json = json.get_object("fill"_L1)) {
      if(auto fill = read_fill(*fill_json, import)) {
        style.m_fill = std::move(*fill);
      } else {
        import.warn("Unsupported fill");
      }
    }
    if(auto stroke_json = json.get_object("stroke"_L1)) {
      if(auto stroke = read_stroke(*stroke_json)) {
        style.m_stroke = std::move(*stroke);
      } else {
        import.warn("Unsupported stroke");
      }
    }
    if(auto filters = json.get_array("filters"_L1)) {
      style.m_effects = read_effects(*filters);
    }
  }
}

std::optional<Fill> Rellu::read_fill(XdObject& fill, XdImport& import) {
  auto& json = fill.get_json();
  auto type = json.value("type"_L1).toString();
  if(type != "none"_L1) {
    auto paint = read_paint(fill, type, import);
    if(paint) {
      fill.consume("type"_L1);
      fill.ignore({"uxdesign#type"_L1});
    }
    return paint;
  }
  fill.consume("type"_L1);
  auto paint_type = json.value("uxdesign#type"_L1).toString();
  if(json.value("color"_L1).isObject()) {
    paint_type = "solid"_L1;
  }
  if(auto paint = read_paint(fill, paint_type, import)) {
    fill.consume("uxdesign#type"_L1);
    return disable_fill(*paint);
  }
  fill.ignore({"uxdesign#type"_L1, "linkId"_L1, "color"_L1, "gradient"_L1,
    "pattern"_L1});
  return NoFill();
}

std::optional<Stroke> Rellu::read_stroke(XdObject& stroke) {
  auto& json = stroke.get_json();
  auto result = Stroke();
  auto type = json.value("type"_L1).toString();
  if(type == "none"_L1) {
    result.m_is_enabled = false;
  } else if(type != "solid"_L1) {
    return std::nullopt;
  }
  stroke.consume("type"_L1);
  if(auto color_json = stroke.get_object("color"_L1)) {
    if(auto color = read_color(*color_json)) {
      result.m_color = *color;
    }
  }
  result.m_width = stroke.get_number("width"_L1, 1);
  auto align = json.value("align"_L1).toString();
  if(align == "inside"_L1) {
    result.m_alignment = StrokeAlignment::INSIDE;
    stroke.consume("align"_L1);
  } else if(align == "outside"_L1) {
    result.m_alignment = StrokeAlignment::OUTSIDE;
    stroke.consume("align"_L1);
  } else if(align == "center"_L1) {
    stroke.consume("align"_L1);
  }
  auto cap = json.value("cap"_L1).toString();
  if(cap == "butt"_L1) {
    stroke.consume("cap"_L1);
  } else if(cap == "round"_L1) {
    result.m_cap = StrokeCap::ROUND;
    stroke.consume("cap"_L1);
  } else if(cap == "square"_L1) {
    result.m_cap = StrokeCap::SQUARE;
    stroke.consume("cap"_L1);
  }
  auto join = json.value("join"_L1).toString();
  if(join == "miter"_L1) {
    stroke.consume("join"_L1);
  } else if(join == "round"_L1) {
    result.m_join = StrokeJoin::ROUND;
    stroke.consume("join"_L1);
  } else if(join == "bevel"_L1) {
    result.m_join = StrokeJoin::BEVEL;
    stroke.consume("join"_L1);
  }
  result.m_miter_limit = stroke.get_number("miterLimit"_L1, 4);
  if(auto dashes = stroke.get_array("dash"_L1)) {
    for(auto dash : dashes->get_json()) {
      result.m_dashes.push_back(dash.toDouble());
    }
  }
  result.m_dash_offset = stroke.get_number("dashOffset"_L1, 0);
  return result;
}

std::vector<Effect> Rellu::read_effects(XdArray& filters) {
  auto effects = std::vector<Effect>();
  auto has_drop_shadow = false;
  for(auto i = std::size_t(0); i != filters.get_size(); ++i) {
    auto filter = filters.get_object(i);
    if(!filter) {
      continue;
    }
    auto type = filter->get_json().value("type"_L1).toString();
    auto is_visible = filter->get_bool("visible"_L1, true);
    filter->ignore({"global"_L1});
    auto params = filter->get_object("params"_L1);
    if(!params) {
      continue;
    }
    if(type == "dropShadow"_L1) {
      if(!has_drop_shadow) {
        read_shadows(*params, "dropShadows"_L1, false, is_visible, 1, effects);
        has_drop_shadow = true;
        filter->consume("type"_L1);
      } else {
        filter->consume_all();
        params->consume_all();
      }
    } else if(type == "uxdesign#innerShadow"_L1) {
      read_shadows(*params, "innerShadows"_L1, true, is_visible,
        std::numeric_limits<std::size_t>::max(), effects);
      filter->consume("type"_L1);
    } else if(type == "uxdesign#blur"_L1) {
      if(auto blur = read_blur(*params, is_visible)) {
        effects.push_back(std::move(*blur));
        filter->consume("type"_L1);
      }
    }
  }
  return effects;
}

void Rellu::add_style_importer(XdImporter& importer) {
  importer.add_property_importer([] (XdNode& node) {
    if(!node.m_payload) {
      return;
    }
    auto json = node.m_json->get_object("style"_L1);
    if(!json) {
      return;
    }
    auto& import = *node.m_import;
    if(auto shape = std::get_if<Box<Shape>>(&*node.m_payload)) {
      import_style(*json, shape->edit().m_style, import);
    } else if(auto text = std::get_if<Box<Text>>(&*node.m_payload)) {
      import_style(*json, text->edit().m_style, import);
    } else if(auto artboard = std::get_if<Box<Artboard>>(&*node.m_payload)) {
      if(auto fill_json = json->get_object("fill"_L1)) {
        if(auto fill = read_fill(*fill_json, import)) {
          artboard->edit().m_background = std::move(*fill);
        } else {
          import.warn("Unsupported fill");
        }
      }
    }
  });
}

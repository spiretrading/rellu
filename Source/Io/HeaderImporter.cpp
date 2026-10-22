#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Localization/Localization.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_NAME_LENGTH = 256;

  std::optional<BlendMode> to_blend_mode(const QString& name) {
    if(name == "normal"_L1) {
      return BlendMode::NORMAL;
    } else if(name == "multiply"_L1) {
      return BlendMode::MULTIPLY;
    } else if(name == "screen"_L1) {
      return BlendMode::SCREEN;
    } else if(name == "overlay"_L1) {
      return BlendMode::OVERLAY;
    } else if(name == "darken"_L1) {
      return BlendMode::DARKEN;
    } else if(name == "lighten"_L1) {
      return BlendMode::LIGHTEN;
    } else if(name == "color-dodge"_L1) {
      return BlendMode::COLOR_DODGE;
    } else if(name == "color-burn"_L1) {
      return BlendMode::COLOR_BURN;
    } else if(name == "hard-light"_L1) {
      return BlendMode::HARD_LIGHT;
    } else if(name == "soft-light"_L1) {
      return BlendMode::SOFT_LIGHT;
    } else if(name == "difference"_L1) {
      return BlendMode::DIFFERENCE;
    } else if(name == "exclusion"_L1) {
      return BlendMode::EXCLUSION;
    } else if(name == "hue"_L1) {
      return BlendMode::HUE;
    } else if(name == "saturation"_L1) {
      return BlendMode::SATURATION;
    } else if(name == "color"_L1) {
      return BlendMode::COLOR;
    } else if(name == "luminosity"_L1) {
      return BlendMode::LUMINOSITY;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 16>> read_matrix_3d(XdObject& transform) {
    auto& json = transform.get_json();
    for(auto row : {"0"_L1, "1"_L1, "2"_L1, "3"_L1}) {
      auto values = json.value(row);
      if(!values.isArray() || values.toArray().size() != 4) {
        return std::nullopt;
      }
      for(auto value : values.toArray()) {
        if(!value.isDouble()) {
          return std::nullopt;
        }
      }
    }
    auto matrix = std::array<double, 16>();
    auto index = std::size_t(0);
    for(auto row : {"0"_L1, "1"_L1, "2"_L1, "3"_L1}) {
      for(auto value : transform.get_array(row)->get_json()) {
        matrix[index] = value.toDouble();
        ++index;
      }
    }
    return matrix;
  }

  void import_transform_3d(
      XdObject& transform, XdObject* ux, NodeHeader& header) {
    auto matrix = read_matrix_3d(transform);
    if(!matrix) {
      return;
    }
    auto& entries = *matrix;
    header.m_transform = Matrix(entries[0], entries[4], entries[1],
      entries[5], entries[3], entries[7]);
    auto& transform_3d = header.m_transform_3d.emplace();
    transform_3d.m_matrix = entries;
    if(!ux) {
      return;
    }
    transform_3d.m_rotation_x = ux->get_number("rotationX"_L1, 0);
    transform_3d.m_rotation_y = ux->get_number("rotationY"_L1, 0);
    if(auto center = ux->get_object("perspectiveCenter"_L1)) {
      transform_3d.m_perspective_center = Point(
        center->get_number("x"_L1, 0), center->get_number("y"_L1, 0));
    }
  }

  void import_style(XdObject& style, NodeHeader& header) {
    header.m_opacity = style.get_number("opacity"_L1, 1);
    auto& json = style.get_json();
    if(auto mode = to_blend_mode(json.value("blendMode"_L1).toString())) {
      header.m_blend_mode = *mode;
      style.consume("blendMode"_L1);
    }
    if(json.value("isolation"_L1).toString() == "isolate"_L1) {
      header.m_is_isolated = true;
      style.consume("isolation"_L1);
    }
  }

  std::string make_content_name(const std::u16string& content) {
    auto name = QString::fromStdU16String(content);
    name.replace("\r\n"_L1, "\n"_L1);
    name.truncate(MAXIMUM_NAME_LENGTH);
    if(!name.isEmpty() && name.back().isHighSurrogate()) {
      name.chop(1);
    }
    return to_std_string(name);
  }

  void import_ux(XdObject& ux, NodeHeader& header) {
    header.m_is_locked = ux.get_bool("locked"_L1, false);
    header.m_is_marked_for_export = ux.get_bool("markedForExport"_L1, false);
    header.m_is_naming_disabled = ux.get_bool("disableNaming"_L1, false);
    auto& constraints = header.m_constraints;
    constraints.m_is_enabled = !ux.get_bool("constraintsDisabled"_L1, false);
    constraints.m_is_custom = ux.get_bool("customConstraints"_L1, false);
    constraints.m_left = ux.get_bool("constraintLeft"_L1, false);
    constraints.m_right = ux.get_bool("constraintRight"_L1, false);
    constraints.m_top = ux.get_bool("constraintTop"_L1, false);
    constraints.m_bottom = ux.get_bool("constraintBottom"_L1, false);
    constraints.m_width = ux.get_bool("constraintWidth"_L1, false);
    constraints.m_height = ux.get_bool("constraintHeight"_L1, false);
    header.m_is_fixed_position = ux.get_bool("fixed"_L1, false);
    header.m_rotation = ux.get_number("rotation"_L1, 0);
    if(auto lock = ux.get_json().value("aspectLock"_L1).toObject();
        lock.value("width"_L1).isDouble() &&
        lock.value("height"_L1).isDouble()) {
      header.m_aspect_lock = Size(lock.value("width"_L1).toDouble(),
        lock.value("height"_L1).toDouble());
    }
    ux.ignore({"hasCustomName"_L1, "nameL10N"_L1,
      "hasNameFromExternalSource"_L1, "isCustomNameInherited"_L1,
      "disableNaming"_L1, "aspectLock"_L1,
      "assetGroupData"_L1, "modTime"_L1, "listViewPosition"_L1,
      "pluginMetadata"_L1});
  }
}

void Rellu::add_header_importer(XdImporter& importer) {
  importer.add_property_importer([] (XdNode& node) {
    auto& json = *node.m_json;
    auto& header = node.m_header;
    auto is_reference = node.m_type == "syncRef"_L1;
    if(!is_reference) {
      if(auto id = json.get_string("id"_L1)) {
        header.m_id = to_node_id(*id);
      }
      if(auto transform = json.get_object("transform"_L1)) {
        if(auto matrix = read_matrix(*transform)) {
          header.m_transform = *matrix;
        } else {
          import_transform_3d(*transform, get_ux(json), header);
          if(!header.m_transform_3d) {
            node.m_import->warn("Unsupported transform");
          }
        }
      }
    }
    auto is_named = false;
    if(auto name = json.get_string("name"_L1)) {
      header.m_name = to_std_string(*name);
      is_named = !name->isEmpty();
    }
    header.m_is_visible = json.get_bool("visible"_L1, true);
    json.ignore({"visualBounds"_L1});
    if(auto style = json.get_object("style"_L1)) {
      import_style(*style, header);
    }
    header.m_has_custom_name = !is_reference && is_named;
    if(auto ux = get_ux(json)) {
      import_ux(*ux, header);
      if(auto key = ux->get_string("nameL10N"_L1); key && !key->isEmpty()) {
        header.m_has_custom_name = false;
        if(is_named) {
          header.m_name = localize_default_name(
            header.m_name, to_std_string(*key), get_language());
        }
      }
      if(ux->get_bool("hasCustomName"_L1, false) ||
          ux->get_bool("isCustomNameInherited"_L1, false)) {
        header.m_has_custom_name = true;
      }
    }
    if(header.m_has_custom_name || !node.m_payload) {
      return;
    }
    if(auto text = std::get_if<Box<Text>>(&*node.m_payload);
        text && !header.m_is_naming_disabled) {
      header.m_name = make_content_name(*(*text)->m_content);
    }
  });
}

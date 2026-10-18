#include <optional>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
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

  void import_ux(XdObject& ux, NodeHeader& header) {
    header.m_is_locked = ux.get_bool("locked"_L1, false);
    header.m_is_marked_for_export = ux.get_bool("markedForExport"_L1, false);
    auto& constraints = header.m_constraints;
    constraints.m_is_enabled = !ux.get_bool("constraintsDisabled"_L1, false);
    constraints.m_is_custom = ux.get_bool("customConstraints"_L1, false);
    constraints.m_left = ux.get_bool("constraintLeft"_L1, false);
    constraints.m_right = ux.get_bool("constraintRight"_L1, false);
    constraints.m_top = ux.get_bool("constraintTop"_L1, false);
    constraints.m_bottom = ux.get_bool("constraintBottom"_L1, false);
    constraints.m_width = ux.get_bool("constraintWidth"_L1, false);
    constraints.m_height = ux.get_bool("constraintHeight"_L1, false);
    ux.ignore({"hasCustomName"_L1, "nameL10N"_L1,
      "hasNameFromExternalSource"_L1, "isCustomNameInherited"_L1,
      "disableNaming"_L1, "rotation"_L1, "aspectLock"_L1,
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
          node.m_import->warn("Unsupported transform");
        }
      }
    }
    if(auto name = json.get_string("name"_L1)) {
      header.m_name = to_std_string(*name);
    }
    header.m_is_visible = json.get_bool("visible"_L1, true);
    json.ignore({"visualBounds"_L1});
    if(auto style = json.get_object("style"_L1)) {
      import_style(*style, header);
    }
    if(auto ux = get_ux(json)) {
      import_ux(*ux, header);
    }
  });
}

#include <mutex>
#include <optional>
#include <utility>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  struct ArtboardOrigin {
    Point m_origin = Point(0, 0);
  };

  Color read_stroke_color(XdObject& grid, QLatin1StringView key) {
    auto stroke = grid.get_object(key);
    if(!stroke) {
      return Color(0, 0, 0, 0);
    }
    if(stroke->get_json().value("type"_L1).toString() == "solid"_L1) {
      stroke->consume("type"_L1);
    }
    if(auto color_json = stroke->get_object("color"_L1)) {
      if(auto color = read_color(*color_json)) {
        return *color;
      }
    }
    return Color(0, 0, 0, 0);
  }

  LayoutGrid read_grid(XdObject& json) {
    auto grid = LayoutGrid();
    auto type = json.get_json().value("type"_L1).toString();
    if(type == "layout"_L1) {
      grid.m_type = GridType::LAYOUT;
      json.consume("type"_L1);
    } else if(type == "square"_L1) {
      json.consume("type"_L1);
    }
    grid.m_is_visible = json.get_bool("visible"_L1, false);
    grid.m_column_count = static_cast<int>(json.get_number("columns"_L1, 0));
    grid.m_gutter = json.get_number("gutter"_L1, 0);
    grid.m_default_width = json.get_number("defaultLayoutWidth"_L1, 0);
    grid.m_margins = Margins(json.get_number("marginTop"_L1, 0),
      json.get_number("marginRight"_L1, 0),
      json.get_number("marginBottom"_L1, 0),
      json.get_number("marginLeft"_L1, 0));
    grid.m_column_color = read_stroke_color(json, "layoutColumnStroke"_L1);
    grid.m_row_color = read_stroke_color(json, "layoutRowStroke"_L1);
    grid.m_square_size = Size(json.get_number("columnSpacing"_L1, 0),
      json.get_number("rowSpacing"_L1, 0));
    grid.m_square_column_color = read_stroke_color(json, "columnStroke"_L1);
    grid.m_square_row_color = read_stroke_color(json, "rowStroke"_L1);
    return grid;
  }

  void read_guides(XdObject& json, QLatin1StringView key,
      Orientation orientation, std::vector<Guide>& guides) {
    auto model = json.get_object(key);
    if(!model) {
      return;
    }
    auto list = model->get_array("guides"_L1);
    if(!list) {
      return;
    }
    for(auto i = std::size_t(0); i != list->get_size(); ++i) {
      if(auto guide = list->get_object(i)) {
        if(auto position = guide->get_number("position"_L1)) {
          auto id = guide->get_string("guid"_L1);
          guides.push_back(Guide(orientation, *position,
            id ? to_std_string(*id) : std::string()));
        }
      }
    }
  }
}

void Rellu::add_artboard_importer(XdImporter& importer) {
  importer.set_node_importer("artboard"_L1, [] (XdNode& node) {
    auto& import = *node.m_import;
    auto json = node.m_json->get_object("artboard"_L1);
    if(!json) {
      import.warn("Artboard without content");
      return;
    }
    auto artboard = Artboard();
    auto reference = json->get_string("ref"_L1);
    if(!reference) {
      reference = node.m_json->get_json().value("id"_L1).toString();
    }
    {
      auto lock = std::lock_guard(import.get_mutex());
      auto frames = import.get_resources().get_object("artboards"_L1);
      auto frame = frames ? frames->get_object(*reference) : nullptr;
      if(frame) {
        artboard.m_frame = Rect(frame->get_number("x"_L1, 0),
          frame->get_number("y"_L1, 0), frame->get_number("width"_L1, 0),
          frame->get_number("height"_L1, 0));
        if(auto name = frame->get_string("name"_L1)) {
          node.m_header.m_name = to_std_string(*name);
        }
        artboard.m_viewport_height = frame->get_number("viewportHeight"_L1);
      } else {
        import.warn("Artboard without a frame");
      }
    }
    if(auto ux = get_ux(*json)) {
      ux->ignore({"path"_L1});
    }
    if(auto ux = get_ux(*node.m_json)) {
      if(auto grid = ux->get_object("gridStyle"_L1)) {
        artboard.m_layout_grid = read_grid(*grid);
      }
      if(auto guides = ux->get_object("guidesModel"_L1)) {
        read_guides(*guides, "horizontalGuides"_L1, Orientation::HORIZONTAL,
          artboard.m_guides);
        read_guides(*guides, "verticalGuides"_L1, Orientation::VERTICAL,
          artboard.m_guides);
      }
    }
    node.m_children = json->get_array("children"_L1);
    node.m_states.get<ArtboardOrigin>().m_origin =
      artboard.m_frame.get_top_left();
    node.m_payload = std::move(artboard);
  });
  importer.add_property_importer([] (XdNode& node) {
    if(!node.m_parent) {
      return;
    }
    auto origin = node.m_parent->m_states.find<ArtboardOrigin>();
    if(!origin) {
      return;
    }
    auto offset =
      make_translation(-origin->m_origin.m_x, -origin->m_origin.m_y);
    auto reference = node.m_payload ?
      std::get_if<Reference>(&*node.m_payload) : nullptr;
    if(!reference) {
      node.m_header.m_transform = offset * node.m_header.m_transform;
      if(auto& transform = node.m_header.m_transform_3d) {
        transform->m_matrix[3] = node.m_header.m_transform.m_tx;
        transform->m_matrix[7] = node.m_header.m_transform.m_ty;
      }
    } else if(reference->m_transform) {
      reference->m_transform = offset * *reference->m_transform;
    }
    if(auto ux = get_ux(*node.m_json)) {
      ux->ignore({"localTransform"_L1});
    }
  });
}

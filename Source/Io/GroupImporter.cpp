#include <map>
#include <optional>
#include <utility>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  Group* get_group(XdNode& node) {
    if(!node.m_payload) {
      return nullptr;
    }
    return std::get_if<Group>(&*node.m_payload);
  }

  std::optional<MaskShape> read_mask_shape(XdObject& json, XdImport& import) {
    if(json.get_json().value("type"_L1).toString() != "shape"_L1) {
      return std::nullopt;
    }
    auto shape_json = json.get_object("shape"_L1);
    if(!shape_json) {
      return std::nullopt;
    }
    auto geometry = read_geometry(*shape_json, import);
    if(!geometry) {
      return std::nullopt;
    }
    auto shape = MaskShape();
    shape.m_geometry = std::move(*geometry);
    if(auto transform = json.get_object("transform"_L1)) {
      if(auto matrix = read_matrix(*transform)) {
        shape.m_transform = *matrix;
      }
    }
    json.consume("type"_L1);
    json.ignore({"id"_L1, "name"_L1, "style"_L1, "meta"_L1, "visualBounds"_L1});
    return shape;
  }

  std::optional<ScrollGroup> read_scroll_group(const QJsonObject& ux) {
    auto type = ux.value("scrollingType"_L1).toString();
    auto scroll = ScrollGroup();
    if(type == "horizontal"_L1) {
      scroll.m_direction = ScrollDirection::HORIZONTAL;
    } else if(type == "vertical"_L1) {
      scroll.m_direction = ScrollDirection::VERTICAL;
    } else if(type == "panning"_L1) {
      scroll.m_direction = ScrollDirection::PANNING;
    } else {
      return std::nullopt;
    }
    scroll.m_viewport = Size(ux.value("viewportWidth"_L1).toDouble(),
      ux.value("viewportHeight"_L1).toDouble());
    scroll.m_offset = Point(ux.value("offsetX"_L1).toDouble(),
      ux.value("offsetY"_L1).toDouble());
    return scroll;
  }

  std::optional<ScrollGroup> find_scroll_group(
      const Document& document, const NodeId& id) {
    auto node = document.find(id);
    for(auto depth = 0; node && node->get_reference() && depth != 64;
        ++depth) {
      node = document.find(node->get_reference()->m_source);
    }
    if(!node || !node->get_group()) {
      return std::nullopt;
    }
    return node->get_group()->m_scroll_group;
  }

  Margins read_margins(XdObject& margins) {
    return Margins(margins.get_number("top"_L1, 0),
      margins.get_number("right"_L1, 0), margins.get_number("bottom"_L1, 0),
      margins.get_number("left"_L1, 0));
  }

  std::map<NodeId, double> read_spacing(const XdArray* children) {
    auto spacing = std::map<NodeId, double>();
    if(!children) {
      return spacing;
    }
    for(auto child : children->get_json()) {
      auto json = child.toObject();
      auto ux = json.value("meta"_L1).toObject().value("ux"_L1).toObject();
      auto margin = ux.value("contentStackMarginAfter"_L1);
      if(!margin.isDouble()) {
        continue;
      }
      auto id = json.value("id"_L1);
      if(!id.isString()) {
        id = json.value("guid"_L1);
      }
      spacing.emplace(to_node_id(id.toString()), margin.toDouble());
    }
    return spacing;
  }
}

void Rellu::add_group_importer(XdImporter& importer) {
  importer.set_node_importer("group"_L1, [] (XdNode& node) {
    if(auto group = node.m_json->get_object("group"_L1)) {
      node.m_children = group->get_array("children"_L1);
    }
    node.m_payload = Group();
  });
}

void Rellu::add_mask_importer(XdImporter& importer) {
  importer.add_property_importer([] (XdNode& node) {
    auto group = get_group(node);
    if(!group) {
      return;
    }
    auto ux = get_ux(*node.m_json);
    if(!ux) {
      return;
    }
    auto clip = ux->get_object("clipPathResources"_L1);
    if(!clip) {
      return;
    }
    auto& import = *node.m_import;
    if(clip->get_json().value("type"_L1).toString() == "clipPath"_L1) {
      clip->consume("type"_L1);
    }
    auto mask = Mask();
    if(auto children = clip->get_array("children"_L1)) {
      for(auto child : children->get_json()) {
        if(!child.isObject()) {
          continue;
        }
        auto json = import.make_node_json(child.toObject());
        if(auto shape = read_mask_shape(*json, import)) {
          mask.m_shapes.push_back(std::move(*shape));
        } else {
          import.warn("Unsupported mask shape");
        }
      }
    }
    group->m_mask = std::move(mask);
  });
}

void Rellu::add_repeat_grid_importer(XdImporter& importer) {
  importer.add_property_importer([] (XdNode& node) {
    auto group = get_group(node);
    if(!group) {
      return;
    }
    auto ux = get_ux(*node.m_json);
    if(!ux) {
      return;
    }
    auto json = ux->get_object("repeatGrid"_L1);
    if(!json) {
      return;
    }
    auto grid = RepeatGrid();
    grid.m_cell_size = Size(json->get_number("cellWidth"_L1, 0),
      json->get_number("cellHeight"_L1, 0));
    grid.m_column_spacing = json->get_number("paddingX"_L1, 0);
    grid.m_row_spacing = json->get_number("paddingY"_L1, 0);
    grid.m_column_count = static_cast<int>(json->get_number("columns"_L1, 1));
    grid.m_row_count = static_cast<int>(json->get_number("rows"_L1, 1));
    grid.m_size =
      Size(json->get_number("width"_L1, 0), json->get_number("height"_L1, 0));
    group->m_repeat_grid = grid;
  });
}

void Rellu::add_scroll_group_importer(XdImporter& importer) {
  importer.add_property_importer([] (XdNode& node) {
    if(!node.m_payload) {
      return;
    }
    auto group = std::get_if<Group>(&*node.m_payload);
    auto reference = std::get_if<Reference>(&*node.m_payload);
    if(!group && !reference) {
      return;
    }
    auto ux = get_ux(*node.m_json);
    if(!ux) {
      return;
    }
    auto scroll = read_scroll_group(ux->get_json());
    if(!scroll) {
      return;
    }
    if(group) {
      group->m_scroll_group = *scroll;
    } else if(find_scroll_group(node.m_import->get_document(),
        reference->m_source) != scroll) {
      return;
    }
    ux->ignore({"scrollingType"_L1, "viewportWidth"_L1, "viewportHeight"_L1,
      "offsetX"_L1, "offsetY"_L1});
  });
}

void Rellu::add_layout_importer(XdImporter& importer) {
  importer.add_property_importer([] (XdNode& node) {
    auto ux = get_ux(*node.m_json);
    if(!ux) {
      return;
    }
    ux->ignore({"contentStackMarginAfter"_L1});
    auto group = get_group(node);
    if(!group) {
      return;
    }
    auto& json = ux->get_json();
    auto type = json.value("contentStackType"_L1).toString();
    auto padding_json = ux->get_object("contentPadding"_L1);
    auto has_background = ux->get_bool("hasContentBackground"_L1, false);
    auto is_content_aware = ux->get_bool("isContentAware"_L1, false);
    if(type == "horizontal"_L1 || type == "vertical"_L1) {
      ux->consume("contentStackType"_L1);
      auto stack = Stack();
      if(type == "horizontal"_L1) {
        stack.m_orientation = Orientation::HORIZONTAL;
      }
      stack.m_spacing = read_spacing(node.m_children);
      if(padding_json) {
        stack.m_padding = read_margins(*padding_json);
      }
      stack.m_has_background = has_background;
      group->m_stack = std::move(stack);
    } else if(padding_json) {
      group->m_padding =
        Padding(read_margins(*padding_json), is_content_aware, has_background);
    }
  });
}

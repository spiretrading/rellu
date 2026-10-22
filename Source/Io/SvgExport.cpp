#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Io/SvgExport.hpp"
#include "Rellu/Io/SvgWriter.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Text/TextLayout.hpp"

using namespace Rellu;

namespace {
  double get_number(const XdValue* value, double fallback = 0) {
    if(!value || value->get_type() != XdValue::Type::NUMBER) {
      return fallback;
    }
    return value->to_number();
  }

  double get_number(
      const XdValue& object, std::string_view key, double fallback = 0) {
    if(!object.is_object()) {
      return fallback;
    }
    return get_number(object.find(key), fallback);
  }

  const XdValue* find_path(const XdValue* value,
      std::initializer_list<std::string_view> keys) {
    for(auto key : keys) {
      if(!value || !value->is_object()) {
        return nullptr;
      }
      value = value->find(key);
    }
    return value;
  }

  std::string_view get_string(const XdValue* value) {
    if(!value || value->get_type() != XdValue::Type::STRING) {
      return {};
    }
    return value->to_string();
  }

  Matrix to_matrix(const XdValue* value) {
    if(!value || !value->is_object()) {
      return Matrix();
    }
    if(value->find("a")) {
      return Matrix(get_number(*value, "a", 1), get_number(*value, "b"),
        get_number(*value, "c"), get_number(*value, "d", 1),
        get_number(*value, "tx"), get_number(*value, "ty"));
    }
    auto at = [&] (int row, int column) {
      auto entries = value->find(std::to_string(row));
      if(!entries || !entries->is_array() ||
          static_cast<std::size_t>(column) >= entries->get_elements().size()) {
        return 0.0;
      }
      return get_number(&entries->get_elements()[column]);
    };
    return Matrix(at(0, 0), at(0, 1), at(1, 0), at(1, 1), at(3, 0), at(3, 1));
  }

  XdValue to_json(const Matrix& matrix) {
    auto json = XdValue::make_object();
    json.set("a", matrix.m_a);
    json.set("b", matrix.m_b);
    json.set("c", matrix.m_c);
    json.set("d", matrix.m_d);
    json.set("tx", matrix.m_tx);
    json.set("ty", matrix.m_ty);
    return json;
  }

  XdValue to_json(const Rect& rect) {
    auto json = XdValue::make_object();
    json.set("x", rect.m_x);
    json.set("y", rect.m_y);
    json.set("width", rect.m_width);
    json.set("height", rect.m_height);
    return json;
  }

  XdValue make_color(const XdValue& source) {
    if(!source.is_object()) {
      return source;
    }
    auto color = XdValue::make_object();
    color.set("mode", "RGB");
    auto& value = color.set("value", XdValue::make_object());
    auto rgb = source.find("value");
    for(auto channel : {"r", "g", "b"}) {
      value.set(channel, std::round(rgb ? get_number(*rgb, channel) : 0));
    }
    if(auto alpha = source.find("alpha")) {
      auto level = std::round(
        std::clamp(get_number(alpha, 1), 0.0, 1.0) * 255);
      if(level < 255) {
        color.set("alpha", level / 255);
      }
    }
    return color;
  }

  XdValue make_shadow_list(const XdValue& source) {
    auto list = XdValue::make_array();
    if(!source.is_array()) {
      return list;
    }
    for(auto& shadow : source.get_elements()) {
      auto copy = shadow;
      if(auto color = shadow.find("color")) {
        copy.set("color", make_color(*color));
      }
      list.append(std::move(copy));
    }
    return list;
  }

  XdValue make_filters(const XdValue& source) {
    auto filters = XdValue::make_array();
    if(!source.is_array()) {
      return filters;
    }
    for(auto& filter : source.get_elements()) {
      auto copy = filter;
      if(auto params = copy.find("params"); params && params->is_object()) {
        for(auto key : {"dropShadows", "innerShadows"}) {
          if(auto shadows = params->find(key)) {
            params->set(key, make_shadow_list(*shadows));
          }
        }
      }
      filters.append(std::move(copy));
    }
    return filters;
  }

  bool has_drop_shadow(const XdValue* style) {
    auto filters = style ? find_path(style, {"filters"}) : nullptr;
    if(!filters || !filters->is_array()) {
      return false;
    }
    return std::any_of(filters->get_elements().begin(),
      filters->get_elements().end(), [] (const auto& filter) {
        return get_string(filter.find("type")) == "dropShadow";
      });
  }

  std::string get_extension(std::string_view media_type) {
    if(media_type.empty() || media_type == "image/tiff") {
      return ".tif";
    } else if(media_type == "image/png") {
      return ".png";
    } else if(media_type == "image/jpeg") {
      return ".jpg";
    } else if(media_type == "image/gif") {
      return ".gif";
    }
    return "";
  }

  std::string replace_dashes(std::string text) {
    static const auto EN_DASH = std::string("\xE2\x80\x93");
    auto position = std::size_t(0);
    while((position = text.find(EN_DASH, position)) != std::string::npos) {
      text.replace(position, EN_DASH.size(), "-");
      ++position;
    }
    return text;
  }

  const TextStyle& find_style(const Text& text, std::size_t index) {
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

  std::u16string transform_case(const Text& text) {
    auto& content = *text.m_content;
    if(text.m_runs.empty()) {
      return apply_transform(content, text.m_base_style.m_transform);
    }
    auto out = std::u16string();
    auto start = std::size_t(0);
    for(auto& run : text.m_runs) {
      if(start >= content.size()) {
        break;
      }
      auto length = std::min(run.m_length, content.size() - start);
      out += apply_transform(std::u16string_view(content).substr(
        start, length), run.m_style.m_transform);
      start += length;
    }
    if(start < content.size()) {
      out += content.substr(start);
    }
    return out;
  }

  std::string to_utf8(const std::u16string& text) {
    return QString::fromUtf16(text.data(),
      static_cast<qsizetype>(text.size())).toStdString();
  }

  XdValue* get_children(XdValue& node) {
    for(auto key : {"artboard", "group", "shape"}) {
      if(auto container = node.find(key); container &&
          container->is_object()) {
        if(auto children = container->find("children");
            children && children->is_array()) {
          return children;
        }
      }
    }
    return nullptr;
  }

  class Builder {
    public:
      Builder(const Document& document, const ExportOptions& options,
          std::string_view name, const SvgTextServices& services,
          std::vector<SvgLinkedImage>* linked)
          : m_document(&document),
            m_images(options.m_svg_images),
            m_is_outlined(options.m_is_svg_outlined),
            m_name(name),
            m_services(&services),
            m_linked(linked),
            m_gradients(XdValue::make_object()),
            m_clip_paths(XdValue::make_object()),
            m_patterns(XdValue::make_object()),
            m_artboards(XdValue::make_object()) {
        for(auto& entry : document.get_resources().m_images) {
          m_uids.emplace(to_uid(entry.first), &entry.second);
        }
      }

      XdValue build(const std::vector<NodeId>& nodes) {
        auto roots = std::vector<NodeId>();
        auto bounds = std::optional<Rect>();
        for(auto& id : nodes) {
          if(!m_document->find(id)) {
            continue;
          }
          roots.push_back(id);
          if(auto drawn = get_world_bounds(*m_document, id, BoundsType::DRAW,
              m_services->m_measurer)) {
            bounds = bounds ? unite(*bounds, *drawn) : *drawn;
          }
        }
        auto area = bounds.value_or(Rect(0, 0, 0, 0));
        m_origin = make_translation(-area.m_x, -area.m_y);
        auto jsons = fetch(roots);
        load_sources(jsons);
        auto children = XdValue::make_array();
        for(auto i = std::size_t(0); i != jsons.size(); ++i) {
          auto& json = jsons[i];
          auto& id = roots[i];
          if(get_string(json.find("type")) == "artboard") {
            if(auto node = convert_artboard(json, id)) {
              children.append(std::move(*node));
            }
            continue;
          }
          auto transform = m_origin * get_world_transform(*m_document, id);
          if(get_string(json.find("type")) == "syncRef") {
            auto expanded = expand(json);
            if(!expanded) {
              continue;
            }
            json = std::move(*expanded);
          }
          auto nodes = outline(convert(json, transform, transform, false),
            transform);
          if(nodes.size() == 1) {
            children.append(std::move(nodes.front()));
          } else if(nodes.size() > 1) {
            auto group = XdValue::make_object();
            group.set("transform", to_json(Matrix()));
            auto& list = group.set("group", XdValue::make_object()).set(
              "children", XdValue::make_array());
            for(auto& node : nodes) {
              list.append(std::move(node));
            }
            group.set("type", "group");
            children.append(std::move(group));
          }
        }
        auto document = XdValue::make_object();
        document.set("version", "1.5.0");
        document.set("children", std::move(children));
        auto& resources = document.set("resources", XdValue::make_object());
        resources.set("gradients", std::move(m_gradients));
        resources.set("clipPaths", std::move(m_clip_paths));
        resources.set("patterns", std::move(m_patterns));
        document.set("artboards", std::move(m_artboards));
        document.set("viewSource",
          to_json(Rect(0, 0, area.m_width, area.m_height)));
        return document;
      }

    private:
      const Document* m_document;
      SvgImages m_images;
      bool m_is_outlined;
      std::string m_name;
      const SvgTextServices* m_services;
      std::vector<SvgLinkedImage>* m_linked;
      XdValue m_gradients;
      XdValue m_clip_paths;
      XdValue m_patterns;
      XdValue m_artboards;
      Matrix m_origin;
      std::unordered_map<std::string, const ImageResource*> m_uids;
      std::unordered_map<std::string, XdValue> m_sources;
      std::unordered_map<std::string, std::string> m_hrefs;
      std::map<std::string, std::string> m_gradient_keys;
      std::map<std::string, std::string> m_pattern_keys;

      std::vector<XdValue> fetch(const std::vector<NodeId>& ids) const {
        auto options = XdWriteOptions();
        options.m_layout = m_services->m_layout;
        auto text = write_xd_nodes(*m_document, ids, options);
        auto value = parse_xd_value(text);
        if(!value || !value->is_array()) {
          return {};
        }
        return std::move(value->get_elements());
      }

      void collect_references(const XdValue& json,
          std::vector<NodeId>& wanted) const {
        if(json.is_array()) {
          for(auto& element : json.get_elements()) {
            collect_references(element, wanted);
          }
          return;
        }
        if(!json.is_object()) {
          return;
        }
        if(get_string(json.find("type")) == "syncRef") {
          if(auto id = parse_node_id(get_string(json.find("guid")))) {
            if(auto resolution =
                resolve_reference(m_document->get_nodes(), *id)) {
              for(auto& source :
                  {resolution->m_source, resolution->m_child_source}) {
                if(source != *id && !m_sources.contains(to_string(source)) &&
                    std::find(wanted.begin(), wanted.end(), source) ==
                      wanted.end()) {
                  wanted.push_back(source);
                }
              }
            }
          }
        }
        for(auto& member : json.get_members()) {
          collect_references(member.m_value, wanted);
        }
      }

      void load_sources(const std::vector<XdValue>& roots) {
        auto wanted = std::vector<NodeId>();
        for(auto& root : roots) {
          collect_references(root, wanted);
        }
        while(!wanted.empty()) {
          auto jsons = fetch(wanted);
          auto next = std::vector<NodeId>();
          for(auto i = std::size_t(0);
              i != wanted.size() && i != jsons.size(); ++i) {
            m_sources[to_string(wanted[i])] = jsons[i];
          }
          for(auto& json : jsons) {
            collect_references(json, next);
          }
          wanted = std::move(next);
        }
      }

      std::optional<XdValue> expand(const XdValue& reference) const {
        auto id = parse_node_id(get_string(reference.find("guid")));
        if(!id) {
          return std::nullopt;
        }
        auto resolution = resolve_reference(m_document->get_nodes(), *id);
        if(!resolution) {
          return std::nullopt;
        }
        auto source = m_sources.find(to_string(resolution->m_source));
        if(source == m_sources.end()) {
          return std::nullopt;
        }
        auto json = source->second;
        json.set("id", to_string(*id));
        auto transform = resolution->m_transform;
        if(auto node = m_document->find(*id);
            node && !node->m_parent.is_nil()) {
          if(auto parent = m_document->find(node->m_parent)) {
            if(auto artboard = parent->get_artboard()) {
              transform = make_translation(artboard->m_frame.m_x,
                artboard->m_frame.m_y) * transform;
            }
          }
        }
        json.set("transform", to_json(transform));
        auto children = XdValue::make_array();
        if(resolution->m_child_source == *id) {
          auto copy = reference;
          if(auto list = get_children(copy)) {
            children = *list;
          }
        } else if(auto child = m_sources.find(
            to_string(resolution->m_child_source)); child != m_sources.end()) {
          auto copy = child->second;
          if(auto list = get_children(copy)) {
            children = *list;
          }
        }
        if(auto list = get_children(json)) {
          *list = std::move(children);
        }
        return json;
      }

      std::optional<XdValue> convert_artboard(
          const XdValue& json, const NodeId& id) {
        auto node = m_document->find(id);
        auto artboard = node ? node->get_artboard() : nullptr;
        if(!artboard) {
          return std::nullopt;
        }
        auto placement = m_origin * get_world_transform(*m_document, id);
        auto key = to_string(id);
        auto& frame = m_artboards.set(key, XdValue::make_object());
        frame.set("width", artboard->m_frame.m_width);
        frame.set("height", artboard->m_frame.m_height);
        frame.set("name", node->m_header.m_name);
        frame.set("x", placement.m_tx);
        frame.set("y", placement.m_ty);
        if(artboard->m_viewport_height) {
          frame.set("viewportHeight", *artboard->m_viewport_height);
        }
        auto out = XdValue::make_object();
        out.set("id", key);
        if(auto visible = json.find("visible")) {
          out.set("visible", *visible);
        }
        auto style = convert_style(json, id, placement, false, Rect(0, 0,
          artboard->m_frame.m_width, artboard->m_frame.m_height));
        if(!style.get_members().empty()) {
          out.set("style", std::move(style));
        }
        auto children = XdValue::make_array();
        if(auto content = find_path(&json, {"artboard", "children"});
            content && content->is_array()) {
          for(auto& child : content->get_elements()) {
            for(auto& converted : convert_child(child, Matrix(), true)) {
              children.append(std::move(converted));
            }
          }
        }
        auto& content = out.set("artboard", XdValue::make_object());
        content.set("children", std::move(children));
        content.set("ref", key);
        out.set("type", "artboard");
        return out;
      }

      std::vector<XdValue> convert_child(const XdValue& source,
          const Matrix& parent, bool is_in_artboard, bool is_mask = false) {
        auto json = source;
        if(get_string(json.find("type")) == "syncRef") {
          auto expanded = expand(json);
          if(!expanded) {
            return {};
          }
          json = std::move(*expanded);
        }
        auto local = to_matrix(json.find("transform"));
        auto transform = is_in_artboard ? m_origin * local : local;
        auto placement = is_in_artboard ? transform : parent * local;
        auto node = convert(json, transform, placement, is_mask);
        if(is_mask) {
          if(!node) {
            return {};
          }
          auto nodes = std::vector<XdValue>();
          nodes.push_back(std::move(*node));
          return nodes;
        }
        return outline(std::move(node), transform);
      }

      const Shape* find_shape(const NodeId& id) const {
        auto node = m_document->find(id);
        if(!node) {
          return nullptr;
        }
        if(node->get_reference()) {
          if(auto resolution =
              resolve_reference(m_document->get_nodes(), id)) {
            node = m_document->find(resolution->m_source);
          }
        }
        return node ? node->get_shape() : nullptr;
      }

      bool is_in_stack(const NodeId& id) const {
        auto node = m_document->find(id);
        if(!node || node->m_parent.is_nil()) {
          return false;
        }
        auto parent = m_document->find(node->m_parent);
        auto group = parent ? parent->get_group() : nullptr;
        return group && group->m_stack;
      }

      static void hide_filters(XdValue& style) {
        if(auto filters = style.find("filters");
            filters && filters->is_array()) {
          for(auto& filter : filters->get_elements()) {
            filter.set("visible", false);
          }
        }
      }

      std::vector<XdValue> outline(
          std::optional<XdValue> node, const Matrix& transform) {
        auto nodes = std::vector<XdValue>();
        if(!node) {
          return nodes;
        }
        auto id = parse_node_id(get_string(node->find("id"))).value_or(
          NodeId());
        auto shape = m_is_outlined &&
          get_string(node->find("type")) == "shape" ? find_shape(id) :
            nullptr;
        if(!shape || !shape->m_style.m_stroke ||
            !shape->m_style.m_stroke->m_is_enabled) {
          nodes.push_back(std::move(*node));
          return nodes;
        }
        auto outlined = make_stroke_outline(
          to_path(shape->m_geometry), shape->m_style);
        auto is_filled =
          !std::holds_alternative<NoFill>(shape->m_style.m_fill) &&
          !std::holds_alternative<LineShape>(shape->m_geometry);
        auto name = std::string(get_string(node->find("name")));
        auto style = node->find("style") ? *node->find("style") :
          XdValue::make_object();
        auto is_grouped = is_filled && (style.find("opacity") ||
          style.find("isolation") || is_in_stack(id));
        auto path = XdValue::make_object();
        path.set("name", is_filled ?
          name + " - " + std::string(localize("name", "Outline")) : name);
        if(!is_filled && node->find("id")) {
          path.set("id", *node->find("id"));
        }
        path.set("transform", to_json(transform));
        if(auto visible = node->find("visible")) {
          path.set("visible", *visible);
        }
        auto path_style = XdValue::make_object();
        if(!is_grouped) {
          for(auto key : {"opacity", "blendMode", "isolation"}) {
            if(auto value = style.find(key)) {
              path_style.set(key, *value);
            }
          }
        }
        auto& fill = path_style.set("fill", XdValue::make_object());
        fill.set("type", "solid");
        auto stroke = style.find("stroke") ? *style.find("stroke") :
          XdValue::make_object();
        fill.set("color", stroke.find("color") ? *stroke.find("color") :
          XdValue::make_object());
        stroke.set("type", "none");
        path_style.set("stroke", std::move(stroke));
        if(auto filters = style.find("filters")) {
          path_style.set("filters", *filters);
          hide_filters(path_style);
        }
        path.set("style", std::move(path_style));
        path.set("type", "shape");
        auto& geometry = path.set("shape", XdValue::make_object());
        geometry.set("type", "path");
        if(auto outline_path = std::get_if<PathShape>(&outlined.m_geometry)) {
          geometry.set("path", to_svg_path(*outline_path->m_path));
        }
        if(!is_filled) {
          nodes.push_back(std::move(path));
          return nodes;
        }
        if(auto source = node->find("style")) {
          if(auto original = source->find("stroke")) {
            original->set("type", "none");
          }
          hide_filters(*source);
        }
        if(!is_grouped) {
          nodes.push_back(std::move(*node));
          nodes.push_back(std::move(path));
          return nodes;
        }
        auto group = XdValue::make_object();
        group.set("name", name);
        group.set("transform", to_json(Matrix()));
        auto group_style = XdValue::make_object();
        if(auto source = node->find("style")) {
          for(auto key : {"opacity", "blendMode", "isolation"}) {
            if(auto value = source->find(key)) {
              group_style.set(key, *value);
              source->erase(key);
            }
          }
        }
        group.set("style", std::move(group_style));
        auto& list = group.set("group", XdValue::make_object()).set(
          "children", XdValue::make_array());
        list.append(std::move(*node));
        list.append(std::move(path));
        group.set("type", "group");
        nodes.push_back(std::move(group));
        return nodes;
      }

      std::string add_gradient(const XdValue& resource) {
        auto text = Rellu::to_json(resource);
        auto key = m_gradient_keys.find(text);
        if(key == m_gradient_keys.end()) {
          auto name = "gradient-" + std::to_string(m_gradient_keys.size());
          key = m_gradient_keys.emplace(text, name).first;
          m_gradients.set(name, resource);
        }
        return key->second;
      }

      std::string add_pattern(const XdValue& resource) {
        auto text = Rellu::to_json(resource);
        auto key = m_pattern_keys.find(text);
        if(key == m_pattern_keys.end()) {
          auto digest = QCryptographicHash::hash(QByteArray::fromStdString(
            text), QCryptographicHash::Md5).toHex().toStdString();
          key = m_pattern_keys.emplace(text, "pattern-" + digest).first;
          m_patterns.set(key->second, resource);
        }
        return key->second;
      }

      std::string get_href(std::string_view uid) {
        auto image = m_uids.find(std::string(uid));
        if(image == m_uids.end()) {
          return "";
        }
        if(auto href = m_hrefs.find(std::string(uid)); href != m_hrefs.end()) {
          return href->second;
        }
        auto& resource = *image->second;
        auto href = std::string();
        if(m_images == SvgImages::EMBED) {
          auto bytes = QByteArray(
            reinterpret_cast<const char*>(resource.m_data->data()),
            static_cast<qsizetype>(resource.m_data->size()));
          auto type = resource.m_media_type.empty() ?
            std::string("image/tiff") : resource.m_media_type;
          href = "data:" + type + ";base64," + bytes.toBase64().toStdString();
        } else {
          auto count = m_linked ? m_linked->size() : std::size_t(0);
          auto suffix = count == 0 ? std::string() : std::to_string(count + 1);
          href = replace_dashes(m_name + "-image" + suffix +
            get_extension(resource.m_media_type));
          if(m_linked) {
            m_linked->push_back(SvgLinkedImage(href, resource.m_id));
          }
        }
        m_hrefs.emplace(std::string(uid), href);
        return href;
      }

      XdValue make_bitmap(const XdValue& pattern) {
        auto ux = find_path(&pattern, {"meta", "ux"});
        auto bitmap = XdValue::make_object();
        bitmap.set("width", get_number(pattern, "width"));
        bitmap.set("height", get_number(pattern, "height"));
        auto& meta_ux = bitmap.set("meta", XdValue::make_object()).set(
          "ux", XdValue::make_object());
        auto behavior = ux ?
          std::string(get_string(ux->find("scaleBehavior"))) :
          std::string("fill");
        meta_ux.set("scaleBehavior", behavior);
        meta_ux.set("uid", ux ? std::string(get_string(ux->find("uid"))) :
          std::string());
        meta_ux.set("hrefLastModifiedDate",
          ux ? get_number(*ux, "hrefLastModifiedDate") : 0.0);
        if(!is_default_positioned(ux)) {
          meta_ux.set("offsetX", ux ? get_number(*ux, "offsetX") : 0.0);
          meta_ux.set("offsetY", ux ? get_number(*ux, "offsetY") : 0.0);
          if(behavior == "cover") {
            meta_ux.set("scale", ux ? get_number(*ux, "scale", 1) : 1.0);
          } else {
            meta_ux.set("scaleX", ux ? get_number(*ux, "scaleX", 1) : 1.0);
            meta_ux.set("scaleY", ux ? get_number(*ux, "scaleY", 1) : 1.0);
          }
        }
        if(ux && ux->find("flipX") && ux->find("flipX")->to_bool()) {
          meta_ux.set("flipX", true);
        }
        if(ux && ux->find("flipY") && ux->find("flipY")->to_bool()) {
          meta_ux.set("flipY", true);
        }
        bitmap.set("href", get_href(get_string(meta_ux.find("uid"))));
        return bitmap;
      }

      static bool is_default_positioned(const XdValue* ux) {
        if(!ux) {
          return true;
        }
        auto behavior = get_string(ux->find("scaleBehavior"));
        return get_number(*ux, "offsetX") == 0 &&
          get_number(*ux, "offsetY") == 0 &&
          (behavior != "cover" || get_number(*ux, "scale", 1) == 1) &&
          (behavior == "cover" || get_number(*ux, "scaleX", 1) == 1 &&
            get_number(*ux, "scaleY", 1) == 1);
      }

      static bool is_flipped(const XdValue* ux) {
        return ux && (ux->find("flipX") && ux->find("flipX")->to_bool() ||
          ux->find("flipY") && ux->find("flipY")->to_bool());
      }

      Rect get_shape_bounds(const NodeId& id) const {
        if(auto bounds = get_local_bounds(*m_document, id,
            BoundsType::GEOMETRY, m_services->m_measurer)) {
          return *bounds;
        }
        return Rect(0, 0, 0, 0);
      }

      XdValue convert_pattern_fill(const XdValue& fill, const Rect& local) {
        auto pattern = fill.find("pattern");
        auto json = XdValue::make_object();
        json.set("type", "pattern");
        if(!pattern) {
          return json;
        }
        auto bitmap = make_bitmap(*pattern);
        auto ux = find_path(pattern, {"meta", "ux"});
        if(is_default_positioned(ux) && !is_flipped(ux)) {
          json.set("pattern", std::move(bitmap));
          return json;
        }
        auto image_width = get_number(*pattern, "width");
        auto image_height = get_number(*pattern, "height");
        auto behavior = get_string(ux->find("scaleBehavior"));
        auto x = 0.0;
        auto y = 0.0;
        auto width = local.m_width;
        auto height = local.m_height;
        if(behavior == "cover" && image_width != 0 && image_height != 0) {
          auto aspect = local.m_width / local.m_height;
          auto image_aspect = image_width / image_height;
          if(aspect > image_aspect) {
            width = local.m_width;
            height = width / image_aspect;
            y = (local.m_height - height) / 2;
          } else {
            height = local.m_height;
            width = height * image_aspect;
            x = (local.m_width - width) / 2;
          }
        }
        auto& bitmap_ux = *bitmap.find("meta")->find("ux");
        auto scale_x = get_number(bitmap_ux, "scaleX");
        if(!scale_x) {
          scale_x = get_number(bitmap_ux, "scale");
        }
        if(!scale_x) {
          scale_x = 1;
        }
        auto scale_y = get_number(bitmap_ux, "scaleY");
        if(!scale_y) {
          scale_y = get_number(bitmap_ux, "scale");
        }
        if(!scale_y) {
          scale_y = 1;
        }
        auto offset_x = get_number(bitmap_ux, "offsetX");
        auto offset_y = get_number(bitmap_ux, "offsetY");
        auto flip_x = bitmap_ux.find("flipX") &&
          bitmap_ux.find("flipX")->to_bool();
        auto flip_y = bitmap_ux.find("flipY") &&
          bitmap_ux.find("flipY")->to_bool();
        auto meta = *bitmap.find("meta");
        auto& meta_ux = *meta.find("ux");
        for(auto key : {"scale", "scaleX", "scaleY", "offsetX", "offsetY",
            "flipX", "flipY"}) {
          meta_ux.erase(key);
        }
        auto image = XdValue::make_object();
        auto scaled_width = width * scale_x;
        auto scaled_height = height * scale_y;
        auto image_w = width / scale_y;
        auto image_h = height / scale_x;
        auto u = image_w / width;
        auto g = image_h / height;
        if(u < g) {
          image_w = width;
          image_h /= u;
        } else {
          image_h = height;
          image_w /= g;
        }
        image.set("width", image_w);
        image.set("height", image_h);
        image.set("href", *bitmap.find("href"));
        auto reference = XdValue::make_object();
        reference.set("ref", XdValue());
        if(flip_x || flip_y) {
          auto matrix = Matrix();
          auto scale_about = [&] (double sx, double sy, double cx,
              double cy) {
            matrix = matrix * make_translation(cx, cy) * make_scale(sx, sy) *
              make_translation(-cx, -cy);
          };
          if(flip_x) {
            scale_about(-1, 1, image_w, 0);
          }
          if(flip_y) {
            scale_about(1, -1, 0, image_h);
          }
          reference.set("transform", to_json(matrix));
        }
        auto box_x = (scaled_width - local.m_width) / 2 / scale_x;
        auto box_y = (scaled_height - local.m_height) / 2 / scale_y;
        box_x -= offset_x * image_w / scale_x;
        box_y -= offset_y * image_h / scale_y;
        auto box_width = local.m_width / scale_x;
        auto box_height = local.m_height / scale_y;
        box_x += (image_w - width) / 2;
        box_y += (image_h - height) / 2;
        auto resource = XdValue::make_object();
        auto& children = resource.set("children", XdValue::make_array());
        auto& child = children.append(XdValue::make_object());
        child.set("type", "image");
        child.set("image", std::move(image));
        child.set("meta", std::move(meta));
        resource.set("type", "pattern");
        resource.set("viewBox", to_json(Rect(box_x, box_y, box_width,
          box_height)));
        resource.set("x", 0);
        resource.set("y", 0);
        resource.set("width", 1);
        resource.set("height", 1);
        resource.set("units", "objectBoundingBox");
        reference.set("ref", add_pattern(resource));
        json.set("pattern", std::move(reference));
        return json;
      }

      XdValue convert_gradient_fill(const XdValue& fill) {
        auto gradient = fill.find("gradient");
        auto json = XdValue::make_object();
        json.set("type", "gradient");
        if(!gradient || !gradient->is_object()) {
          return json;
        }
        auto ux = find_path(gradient, {"meta", "ux"});
        auto source = ux ? ux->find("gradientResources") : nullptr;
        auto type = source ? std::string(get_string(source->find("type"))) :
          std::string("linear");
        auto resource = XdValue::make_object();
        resource.set("type", type == "angular" ? std::string("radial") : type);
        auto& stops = resource.set("stops", XdValue::make_array());
        if(auto list = source ? source->find("stops") : nullptr;
            list && list->is_array()) {
          for(auto& stop : list->get_elements()) {
            auto& entry = stops.append(XdValue::make_object());
            entry.set("offset", get_number(stop, "offset"));
            if(auto color = stop.find("color")) {
              entry.set("color", make_color(*color));
            }
          }
        }
        auto reference = XdValue::make_object();
        reference.set("ref", add_gradient(resource));
        auto units = std::string(get_string(gradient->find("units")));
        if(units.empty()) {
          units = "objectBoundingBox";
        }
        if(type == "linear") {
          for(auto key : {"x1", "y1", "x2", "y2"}) {
            reference.set(key, get_number(*gradient, key));
          }
          reference.set("units", units);
        } else if(type == "radial") {
          reference.set("cx", get_number(*gradient, "cx"));
          reference.set("cy", get_number(*gradient, "cy"));
          reference.set("fx", get_number(*gradient, "fx"));
          reference.set("fy", get_number(*gradient, "fy"));
          reference.set("r", get_number(*gradient, "r"));
          reference.set("units", units);
          if(auto transform = gradient->find("transform")) {
            reference.set("transform", *transform);
          }
        } else {
          reference.set("cx", ux ? get_number(*ux, "x") : 0.0);
          reference.set("cy", ux ? get_number(*ux, "y") : 0.0);
          reference.set("r", 0.5);
          reference.set("units", units);
        }
        json.set("gradient", std::move(reference));
        return json;
      }

      XdValue convert_fill(const XdValue& fill, const Rect& local) {
        auto type = get_string(fill.find("type"));
        if(type == "none") {
          auto original = get_string(fill.find("uxdesign#type"));
          auto json = XdValue::make_object();
          if(original == "pattern") {
            json = convert_pattern_fill(fill, local);
          } else if(original == "gradient") {
            json = convert_gradient_fill(fill);
          } else if(original == "solid") {
            json.set("type", "solid");
            if(auto color = fill.find("color")) {
              json.set("color", make_color(*color));
            }
          }
          if(!original.empty()) {
            json.set("uxdesign#type", original);
          }
          json.set("type", "none");
          return json;
        }
        if(type == "gradient") {
          return convert_gradient_fill(fill);
        } else if(type == "pattern") {
          return convert_pattern_fill(fill, local);
        }
        auto json = XdValue::make_object();
        json.set("type", "solid");
        if(auto color = fill.find("color")) {
          json.set("color", make_color(*color));
        }
        return json;
      }

      XdValue convert_style(const XdValue& json, const NodeId& id,
          const Matrix& placement, bool is_skipping_paint,
          const Rect& local = Rect(0, 0, 0, 0)) {
        auto style = XdValue::make_object();
        auto source = json.find("style");
        if(!source || !source->is_object()) {
          return style;
        }
        for(auto key : {"opacity", "blendMode", "isolation"}) {
          if(auto value = source->find(key)) {
            style.set(key, *value);
          }
        }
        if(!is_skipping_paint) {
          if(auto fill = source->find("fill"); fill && fill->is_object()) {
            style.set("fill", convert_fill(*fill, local));
          }
          if(auto stroke = source->find("stroke");
              stroke && stroke->is_object()) {
            auto copy = *stroke;
            if(auto color = stroke->find("color")) {
              copy.set("color", make_color(*color));
            }
            copy.erase("linkId");
            style.set("stroke", std::move(copy));
          }
        }
        if(auto filters = source->find("filters");
            filters && filters->is_array() &&
            !filters->get_elements().empty()) {
          style.set("filters", make_filters(*filters));
        }
        for(auto key : {"font", "textAttributes"}) {
          if(auto value = source->find(key)) {
            style.set(key, *value);
          }
        }
        return style;
      }

      void add_clip(XdValue& out, XdValue& style, const NodeId& id,
          XdValue clip) {
        auto key = "clipPath-" + to_string(id);
        auto& resource = m_clip_paths.set(key, XdValue::make_object());
        auto& children = resource.set("children", XdValue::make_array());
        children.append(std::move(clip));
        resource.set("type", "clipPath");
        style.set("clipPath", XdValue::make_object()).set("ref", key);
      }

      static XdValue make_clip_rect(double x, double y, double width,
          double height) {
        auto clip = XdValue::make_object();
        clip.set("type", "shape");
        auto& shape = clip.set("shape", XdValue::make_object());
        shape.set("type", "rect");
        shape.set("x", x);
        shape.set("y", y);
        shape.set("width", width);
        shape.set("height", height);
        return clip;
      }

      std::optional<Rect> get_content_bounds(const NodeId& id) const {
        auto node = m_document->find(id);
        if(!node) {
          return std::nullopt;
        }
        auto children = &node->m_children;
        if(node->get_reference()) {
          if(auto resolution = resolve_reference(m_document->get_nodes(), id)) {
            children = &m_document->get(resolution->m_child_source).m_children;
          }
        }
        auto bounds = std::optional<Rect>();
        for(auto& child : *children) {
          auto child_node = m_document->find(child);
          if(!child_node) {
            continue;
          }
          if(auto measured = get_bounds(*m_document, child,
              get_transform(*m_document, child), BoundsType::GEOMETRY,
              m_services->m_measurer)) {
            bounds = bounds ? unite(*bounds, *measured) : *measured;
          }
        }
        return bounds;
      }

      void convert_group(const XdValue& json, const NodeId& id,
          const Matrix& placement, XdValue& out, XdValue& style) {
        auto ux = find_path(&json, {"meta", "ux"});
        if(ux && ux->is_object()) {
          if(get_string(ux->find("componentType")) == "component") {
            add_clip(out, style, id, make_clip_rect(0, 0,
              get_number(*ux, "width"), get_number(*ux, "height")));
          }
          if(auto grid = ux->find("repeatGrid"); grid && grid->is_object()) {
            add_clip(out, style, id, make_clip_rect(0, 0,
              get_number(*grid, "width"), get_number(*grid, "height")));
          }
          if(auto type = get_string(ux->find("scrollingType"));
              !type.empty()) {
            auto content = get_content_bounds(id).value_or(Rect(0, 0, 0, 0));
            if(type != "vertical") {
              content.m_x = get_number(*ux, "offsetX");
              content.m_width = get_number(*ux, "viewportWidth");
            }
            if(type != "horizontal") {
              content.m_y = get_number(*ux, "offsetY");
              content.m_height = get_number(*ux, "viewportHeight");
            }
            add_clip(out, style, id, make_clip_rect(content.m_x, content.m_y,
              content.m_width, content.m_height));
          }
          if(auto resources = ux->find("clipPathResources");
              resources && resources->is_object()) {
            if(auto masks = resources->find("children");
                masks && masks->is_array() && !masks->get_elements().empty()) {
              auto& mask = masks->get_elements().back();
              auto converted = convert_child(mask, placement, false, true);
              if(!converted.empty()) {
                add_clip(out, style, id, std::move(converted.front()));
              }
            }
          }
        }
        auto children = XdValue::make_array();
        if(auto list = find_path(&json, {"group", "children"});
            list && list->is_array()) {
          for(auto& child : list->get_elements()) {
            for(auto& converted : convert_child(child, placement, false)) {
              children.append(std::move(converted));
            }
          }
        }
        out.set("group", XdValue::make_object()).set(
          "children", std::move(children));
      }

      static bool close_to(double left, double right) {
        return std::abs(left - right) < .001;
      }

      bool is_simple_image(const XdValue& fill, double width, double height) {
        if(get_string(fill.find("type")) != "pattern" &&
            get_string(fill.find("uxdesign#type")) != "pattern") {
          return false;
        }
        auto pattern = fill.find("pattern");
        if(!pattern) {
          return false;
        }
        auto ux = find_path(pattern, {"meta", "ux"});
        if(!ux) {
          return false;
        }
        auto image_width = get_number(*pattern, "width");
        auto image_height = get_number(*pattern, "height");
        return get_string(ux->find("scaleBehavior")) != "cover" &&
          close_to(image_width / image_height, width / height) &&
          close_to(get_number(*ux, "offsetX"), 0) &&
          close_to(get_number(*ux, "offsetY"), 0) &&
          close_to(get_number(*ux, "scale", 1), 1) &&
          close_to(get_number(*ux, "scaleX", 1), 1) &&
          close_to(get_number(*ux, "scaleY", 1), 1) && !is_flipped(ux);
      }

      XdValue convert_text(const XdValue& json, const NodeId& id) {
        auto node = m_document->find(id);
        auto model = node ? node->get_text() : nullptr;
        auto text = XdValue::make_object();
        auto source = json.find("text");
        if(source && source->is_object()) {
          if(auto frame = source->find("frame")) {
            text.set("frame", *frame);
          }
        }
        auto paragraphs = source && source->is_object() ?
          source->find("paragraphs") : nullptr;
        if(paragraphs && paragraphs->is_array()) {
          auto& list = text.set("paragraphs", XdValue::make_array());
          for(auto& paragraph : paragraphs->get_elements()) {
            auto& converted = list.append(XdValue::make_object());
            auto& lines = converted.set("lines", XdValue::make_array());
            auto source_lines = paragraph.find("lines");
            if(!source_lines || !source_lines->is_array()) {
              continue;
            }
            for(auto& line : source_lines->get_elements()) {
              auto& runs = lines.append(XdValue::make_array());
              if(!line.is_array()) {
                continue;
              }
              auto& elements = line.get_elements();
              for(auto k = std::size_t(0); k != elements.size(); ++k) {
                runs.append(convert_run(elements[k], k == 0, model));
              }
            }
          }
        }
        if(model) {
          text.set("rawText", to_utf8(transform_case(*model)));
        } else if(source && source->find("rawText")) {
          text.set("rawText", *source->find("rawText"));
        }
        return text;
      }

      XdValue convert_run(const XdValue& run, bool is_first,
          const Text* model) {
        auto json = XdValue::make_object();
        json.set("y", get_number(run, "y"));
        json.set("from", get_number(run, "from"));
        json.set("to", get_number(run, "to"));
        if(is_first) {
          json.set("x", get_number(run, "x"));
        }
        auto style = XdValue::make_object();
        auto source = run.find("style");
        if(source && source->is_object()) {
          if(auto font = source->find("font")) {
            style.set("font", *font);
          }
          if(auto fill = source->find("fill"); fill && fill->is_object()) {
            auto copy = *fill;
            if(auto color = fill->find("color")) {
              copy.set("color", make_color(*color));
            }
            style.set("fill", std::move(copy));
          }
        }
        auto attributes = XdValue::make_object();
        if(model) {
          auto from = static_cast<std::size_t>(
            std::max(0.0, get_number(run, "from")));
          auto& covering = find_style(*model, from);
          auto& base = model->m_base_style;
          if(covering.m_letter_spacing != base.m_letter_spacing &&
              covering.m_letter_spacing != 0) {
            attributes.set("letterSpacing", covering.m_letter_spacing);
          }
        }
        if(source && source->is_object()) {
          if(auto align = find_path(source, {"textAttributes",
              "verticalAlign"})) {
            attributes.set("verticalAlign", *align);
          }
        }
        if(model) {
          auto from = static_cast<std::size_t>(
            std::max(0.0, get_number(run, "from")));
          auto& covering = find_style(*model, from);
          auto decoration = XdValue::make_array();
          if(covering.m_is_underlined) {
            decoration.append("underline");
          }
          if(covering.m_is_struck_through) {
            decoration.append("line-through");
          }
          if(!decoration.get_elements().empty()) {
            attributes.set("decoration", std::move(decoration));
          }
        }
        if(!attributes.get_members().empty()) {
          style.set("textAttributes", std::move(attributes));
        }
        if(!style.get_members().empty()) {
          json.set("style", std::move(style));
        }
        return json;
      }

      std::optional<XdValue> convert(const XdValue& json, Matrix transform,
          const Matrix& placement, bool is_mask) {
        auto type = std::string(get_string(json.find("type")));
        auto id_text = std::string(get_string(json.find("id")));
        auto id = parse_node_id(id_text).value_or(NodeId());
        auto out = XdValue::make_object();
        if(auto name = json.find("name");
            name && name->get_type() == XdValue::Type::STRING) {
          out.set("name", *name);
        }
        if(!id_text.empty()) {
          out.set("id", id_text);
        }
        out.set("transform", XdValue());
        if(auto visible = json.find("visible")) {
          out.set("visible", *visible);
        }
        auto local = Rect(0, 0, 0, 0);
        if(type == "shape") {
          auto shape = json.find("shape");
          if(!shape || !shape->is_object()) {
            return std::nullopt;
          }
          auto shape_type = std::string(get_string(shape->find("type")));
          auto geometry = XdValue::make_object();
          auto node = m_document->find(id);
          auto model = node ? node->get_shape() : nullptr;
          if(shape_type == "rect") {
            auto x = get_number(*shape, "x");
            auto y = get_number(*shape, "y");
            auto width = get_number(*shape, "width");
            auto height = get_number(*shape, "height");
            if(x != 0 || y != 0) {
              transform = transform * make_translation(x, y);
            }
            local = Rect(0, 0, width, height);
            auto source_style = json.find("style");
            auto fill = source_style ? source_style->find("fill") : nullptr;
            auto stroke = source_style ? source_style->find("stroke") :
              nullptr;
            auto has_radius = false;
            if(auto radii = shape->find("r"); radii && radii->is_array()) {
              for(auto& radius : radii->get_elements()) {
                if(get_number(&radius) > 0) {
                  has_radius = true;
                }
              }
            }
            auto is_stroked = stroke && stroke->is_object() &&
              get_string(stroke->find("type")) != "none" &&
              get_number(*stroke, "width") != 0;
            if(fill && fill->is_object() && !has_radius && !is_stroked &&
                is_simple_image(*fill, width, height)) {
              auto image = make_bitmap(*fill->find("pattern"));
              image.set("width", width);
              image.set("height", height);
              *out.find("transform") = to_json(transform);
              auto style = convert_style(json, id, placement, true, local);
              if(!style.get_members().empty()) {
                out.set("style", std::move(style));
              }
              add_visual_bounds(out, id, placement);
              out.set("type", "image");
              out.set("image", std::move(image));
              return out;
            }
            geometry.set("type", "rect");
            geometry.set("x", 0);
            geometry.set("y", 0);
            geometry.set("width", width);
            geometry.set("height", height);
            if(has_radius) {
              geometry.set("r", *shape->find("r"));
            }
          } else if(shape_type == "circle" || shape_type == "ellipse") {
            auto rx = shape_type == "circle" ? get_number(*shape, "r") :
              get_number(*shape, "rx");
            auto ry = shape_type == "circle" ? get_number(*shape, "r") :
              get_number(*shape, "ry");
            auto x = get_number(*shape, "cx") - rx;
            auto y = get_number(*shape, "cy") - ry;
            if(x != 0 || y != 0) {
              transform = transform * make_translation(x, y);
            }
            local = Rect(0, 0, 2 * rx, 2 * ry);
            geometry.set("cx", rx);
            geometry.set("cy", ry);
            if(rx == ry) {
              geometry.set("type", "circle");
              geometry.set("r", rx);
            } else {
              geometry.set("type", "ellipse");
              geometry.set("rx", rx);
              geometry.set("ry", ry);
            }
          } else if(shape_type == "line") {
            auto x1 = get_number(*shape, "x1");
            auto y1 = get_number(*shape, "y1");
            auto x2 = get_number(*shape, "x2");
            auto y2 = get_number(*shape, "y2");
            auto x = std::min(x1, x2);
            auto y = std::min(y1, y2);
            if(x != 0 || y != 0) {
              transform = transform * make_translation(x, y);
            }
            local = Rect(0, 0, std::abs(x2 - x1), std::abs(y2 - y1));
            geometry.set("type", "line");
            geometry.set("x1", x1 - x);
            geometry.set("y1", y1 - y);
            geometry.set("x2", x2 - x);
            geometry.set("y2", y2 - y);
          } else if(shape_type == "polygon") {
            geometry.set("type", "path");
            if(model) {
              geometry.set("path", to_svg_path(to_path(model->m_geometry)));
            } else {
              geometry.set("path", "");
            }
            local = id.is_nil() ? Rect(0, 0, 0, 0) : get_shape_bounds(id);
          } else if(shape_type == "path" || shape_type == "compound") {
            if(shape_type == "compound") {
              geometry.set("type", "compound");
              if(auto operation = shape->find("operation")) {
                geometry.set("operation", *operation);
              }
            } else {
              geometry.set("type", "path");
            }
            if(auto path = shape->find("path")) {
              geometry.set("path", *path);
            }
            if(auto winding = shape->find("winding")) {
              geometry.set("winding", *winding);
            }
            local = id.is_nil() ? Rect(0, 0, 0, 0) : get_shape_bounds(id);
          } else {
            geometry = *shape;
          }
          *out.find("transform") = to_json(transform);
          auto style = convert_style(json, id, placement, false, local);
          if(!style.get_members().empty()) {
            out.set("style", std::move(style));
          }
          add_visual_bounds(out, id, placement);
          out.set("type", "shape");
          out.set("shape", std::move(geometry));
          return out;
        }
        *out.find("transform") = to_json(transform);
        if(type == "text") {
          auto node = m_document->find(id);
          auto model = node ? node->get_text() : nullptr;
          if(model && model->m_is_flipped_vertically) {
            auto bounds = m_services->m_measurer(*model);
            auto y = bounds.m_y + bounds.m_height / 2;
            *out.find("transform") = to_json(transform *
              make_translation(0, y) * make_scale(1, -1) *
              make_translation(0, -y));
          }
          auto style = convert_style(json, id, placement, false);
          if(!style.get_members().empty()) {
            out.set("style", std::move(style));
          }
          add_visual_bounds(out, id, placement);
          out.set("type", "text");
          auto has_text = !model || !model->m_content->empty() ||
            !std::holds_alternative<PositionedFrame>(model->m_frame);
          if(has_text) {
            out.set("text", convert_text(json, id));
          }
          return out;
        }
        if(type == "group") {
          auto style = convert_style(json, id, placement, false);
          convert_group(json, id, placement, out, style);
          if(!style.get_members().empty()) {
            out.set("style", std::move(style));
          }
          out.set("type", "group");
          return out;
        }
        return std::nullopt;
      }

      void add_visual_bounds(
          XdValue& out, const NodeId& id, const Matrix& placement) {
        auto style = out.find("style");
        if(!has_drop_shadow(style) || id.is_nil()) {
          return;
        }
        if(auto bounds = get_bounds(*m_document, id, placement,
            BoundsType::DRAW, m_services->m_measurer)) {
          out.set("visualBounds", to_json(*bounds));
        }
      }
  };

  SvgWriterOptions make_writer_options(const ExportOptions& options) {
    auto writer = SvgWriterOptions();
    writer.m_styling =
      options.m_svg_styling == SvgStyling::PRESENTATION_ATTRIBUTES ?
        SvgWriterStyling::ATTRIBUTE : SvgWriterStyling::CLASS;
    writer.m_is_minified = options.m_is_svg_minified;
    writer.m_has_carriage_returns = true;
    return writer;
  }
}

XdValue Rellu::make_svg_document(const Document& document,
    const std::vector<NodeId>& nodes, const ExportOptions& options,
    std::string_view name, const SvgTextServices& services,
    std::vector<SvgLinkedImage>* linked) {
  return Builder(document, options, name, services, linked).build(nodes);
}

SvgExport Rellu::make_svg_export(const Document& document,
    const std::vector<NodeId>& nodes, const ExportOptions& options,
    std::string_view name, const SvgTextServices& services) {
  auto result = SvgExport();
  auto json = make_svg_document(
    document, nodes, options, name, services, &result.m_images);
  result.m_markup = write_svg(std::move(json), make_writer_options(options));
  return result;
}

std::string Rellu::make_svg_code(const Document& document,
    const std::vector<NodeId>& nodes, const SvgTextServices& services) {
  auto options = ExportOptions();
  options.m_format = ExportFormat::SVG;
  options.m_svg_styling = SvgStyling::PRESENTATION_ATTRIBUTES;
  options.m_svg_images = SvgImages::EMBED;
  return make_svg_export(document, nodes, options, "", services).m_markup;
}

void Rellu::write_svg_export(const Document& document, const NodeId& node,
    const std::filesystem::path& path, const ExportOptions& options,
    const SvgTextServices& services) {
  auto stem = path.filename().u8string();
  auto name = std::string(stem.begin(), stem.end());
  if(name.size() >= 4 && name.substr(name.size() - 4) == ".svg") {
    name.resize(name.size() - 4);
  }
  auto result = make_svg_export(document, {node}, options, name, services);
  auto write = [] (const std::filesystem::path& target, const char* data,
      std::size_t size) {
    auto file = std::ofstream(target, std::ios::binary | std::ios::trunc);
    if(!file) {
      throw std::runtime_error("The SVG file cannot be written.");
    }
    file.write(data, static_cast<std::streamsize>(size));
    if(!file) {
      throw std::runtime_error("The SVG file cannot be written.");
    }
  };
  write(path, result.m_markup.data(), result.m_markup.size());
  auto& images = document.get_resources().m_images;
  for(auto& linked : result.m_images) {
    auto image = images.find(linked.m_image);
    if(image == images.end()) {
      continue;
    }
    auto& data = *image->second.m_data;
    auto target = path.parent_path() / std::filesystem::path(
      std::u8string(linked.m_name.begin(), linked.m_name.end()));
    write(target, reinterpret_cast<const char*>(data.data()), data.size());
  }
}

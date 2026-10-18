#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  bool starts_with(const QByteArray& data, std::size_t offset,
      std::string_view prefix) {
    if(static_cast<std::size_t>(data.size()) < offset + prefix.size()) {
      return false;
    }
    return std::string_view(data.constData() + offset, prefix.size()) ==
      prefix;
  }

  std::string get_media_type(const QByteArray& data) {
    if(starts_with(data, 0, "\x89PNG")) {
      return "image/png";
    } else if(starts_with(data, 0, "\xFF\xD8\xFF")) {
      return "image/jpeg";
    } else if(starts_with(data, 0, "GIF8")) {
      return "image/gif";
    } else if(starts_with(data, 0, "RIFF") && starts_with(data, 8, "WEBP")) {
      return "image/webp";
    } else if(starts_with(data, 0, "BM")) {
      return "image/bmp";
    } else if(starts_with(data, 4, "ftyp")) {
      return "video/mp4";
    } else if(starts_with(data, 0, "<svg") || starts_with(data, 0, "<?xml")) {
      return "image/svg+xml";
    }
    return "application/octet-stream";
  }

  void import_images(XdImport& import, XdResourceIds& ids) {
    auto& package = import.get_package();
    for(auto& entry : package.get_image_entries()) {
      auto name = QString::fromStdString(entry).mid(10);
      if(auto dot = name.indexOf(u'.'); dot >= 0) {
        name.truncate(dot);
      }
      auto bytes = package.read(entry);
      auto image = ImageResource();
      image.m_id = to_node_id(name);
      image.m_media_type = get_media_type(bytes);
      auto data = std::vector<std::byte>(static_cast<std::size_t>(
        bytes.size()));
      std::memcpy(data.data(), bytes.constData(), data.size());
      image.m_data = Shared(std::move(data));
      if(ids.m_images.contains(image.m_id)) {
        continue;
      }
      try {
        ids.m_images.insert(
          import.get_transaction().add_image(std::move(image)));
      } catch(const std::invalid_argument&) {
        import.warn("Image not added");
      }
    }
  }

  void add_swatch(XdImport& import, XdResourceIds& ids, ColorSwatch swatch) {
    if(ids.m_swatches.contains(swatch.m_id)) {
      return;
    }
    try {
      ids.m_swatches.insert(
        import.get_transaction().add_swatch(std::move(swatch)));
    } catch(const std::invalid_argument&) {
      import.warn("Swatch not added");
    }
  }

  void import_color_swatches(XdArray& swatches, XdImport& import,
      XdResourceIds& ids) {
    for(auto i = std::size_t(0); i != swatches.get_size(); ++i) {
      auto json = swatches.get_object(i);
      if(!json) {
        continue;
      }
      auto color = read_color(*json);
      if(!color) {
        continue;
      }
      auto swatch = ColorSwatch();
      swatch.m_id = derive_node_id(u"colorSwatches", QString::number(i));
      swatch.m_color = *color;
      add_swatch(import, ids, std::move(swatch));
    }
  }

  CharacterStyle read_character_style(XdObject& content) {
    auto style = CharacterStyle();
    auto& text = style.m_style;
    if(auto family = content.get_string("fontFamily"_L1)) {
      text.m_font.m_family = to_std_string(*family);
    }
    if(auto font_style = content.get_string("fontStyle"_L1)) {
      text.m_font.m_style = to_std_string(*font_style);
    }
    if(auto name = content.get_string("postscriptName"_L1)) {
      text.m_font.m_postscript_name = to_std_string(*name);
    }
    text.m_font.m_size = content.get_number("fontSize"_L1, text.m_font.m_size);
    if(auto color_json = content.get_object("fontColor"_L1)) {
      if(auto color = read_color(*color_json)) {
        text.m_color = *color;
      }
    }
    text.m_letter_spacing = content.get_number("charSpacing"_L1, 0);
    text.m_is_underlined = content.get_bool("underline"_L1, false);
    text.m_is_struck_through = content.get_bool("strikethrough"_L1, false);
    auto& json = content.get_json();
    if(auto transform =
        to_text_transform(json.value("textTransform"_L1).toString())) {
      text.m_transform = *transform;
      content.consume("textTransform"_L1);
    }
    if(auto script = to_text_script(json.value("textScript"_L1).toString())) {
      text.m_script = *script;
      content.consume("textScript"_L1);
    }
    if(auto spacing = content.get_number("lineSpacing"_L1); spacing &&
        *spacing > 0) {
      style.m_line_height = *spacing;
    }
    if(json.value("paragraphSpacing"_L1).isNull()) {
      content.consume("paragraphSpacing"_L1);
    } else {
      style.m_paragraph_spacing =
        content.get_number("paragraphSpacing"_L1, 0);
    }
    content.ignore({"linkId"_L1});
    return style;
  }

  void import_element(XdObject& element, XdImport& import,
      XdResourceIds& ids) {
    auto type = element.get_json().value("type"_L1).toString();
    auto representations = element.get_array("representations"_L1);
    if(!representations || representations->get_size() == 0) {
      return;
    }
    auto representation = representations->get_object(0);
    if(!representation) {
      return;
    }
    auto id = to_node_id(element.get_json().value("id"_L1).toString());
    auto name = to_std_string(element.get_json().value("name"_L1).toString());
    if(type == "application/vnd.adobe.element.color+dcx"_L1) {
      auto content = representation->get_object("content"_L1);
      if(!content) {
        return;
      }
      auto color = read_color(*content);
      if(!color) {
        return;
      }
      add_swatch(import, ids, ColorSwatch(id, name, *color,
        std::vector<GradientStop>()));
    } else if(type == "application/vnd.adobe.element.gradient+dcx"_L1) {
      auto content = representation->get_array("content"_L1);
      if(!content) {
        return;
      }
      auto swatch = ColorSwatch(id, name, Color(0, 0, 0, 1),
        std::vector<GradientStop>());
      for(auto i = std::size_t(0); i != content->get_size(); ++i) {
        auto stop = content->get_object(i);
        if(!stop) {
          continue;
        }
        auto color_json = stop->get_object("color"_L1);
        auto color = color_json ? read_color(*color_json) : std::nullopt;
        if(!color) {
          continue;
        }
        swatch.m_gradient.push_back(
          GradientStop(stop->get_number("stop"_L1, 0), *color));
      }
      if(!swatch.m_gradient.empty()) {
        swatch.m_color = swatch.m_gradient.front().m_color;
      }
      add_swatch(import, ids, std::move(swatch));
    } else if(type == "application/vnd.adobe.element.characterstyle+dcx"_L1) {
      auto content = representation->get_object("content"_L1);
      if(!content) {
        return;
      }
      auto style = read_character_style(*content);
      style.m_id = id;
      style.m_name = name;
      if(ids.m_character_styles.contains(id)) {
        return;
      }
      try {
        ids.m_character_styles.insert(
          import.get_transaction().add_character_style(std::move(style)));
      } catch(const std::invalid_argument&) {
        import.warn("Character style not added");
      }
    } else {
      return;
    }
    element.consume("type"_L1);
    element.ignore({"id"_L1, "name"_L1, "created"_L1, "modified"_L1,
      "displayName"_L1, "index"_L1, "updateElement"_L1, "assetGroupData"_L1,
      "listViewPosition"_L1, "meta"_L1});
    representation->ignore({"id"_L1, "type"_L1, "relationship"_L1});
  }

  void import_library(XdObject& library, XdImport& import,
      XdResourceIds& ids) {
    library.ignore({"version"_L1, "isStickerSheet"_L1, "hashedMetadata"_L1,
      "groupData"_L1});
    if(auto elements = library.get_array("elements"_L1)) {
      for(auto i = std::size_t(0); i != elements->get_size(); ++i) {
        if(auto element = elements->get_object(i)) {
          import_element(*element, import, ids);
        }
      }
    }
  }
}

void Rellu::add_resource_importer(XdImporter& importer) {
  importer.add_document_importer(XdPhase::RESOURCES, [] (XdImport& import) {
    auto& ids = import.get_states().get<XdResourceIds>();
    import_images(import, ids);
    auto& root = import.get_resources();
    root.ignore({"version"_L1});
    root.get_object("artboards"_L1);
    if(auto children = root.get_array("children"_L1)) {
      if(children->get_size() != 0) {
        import.warn("Resources with nodes");
      }
    }
    auto resources = root.get_object("resources"_L1);
    if(!resources) {
      return;
    }
    if(auto gradients = resources->get_object("gradients"_L1)) {
      if(!gradients->get_json().isEmpty()) {
        import.warn("Unsupported gradient resources");
      }
    }
    if(auto clip_paths = resources->get_object("clipPaths"_L1)) {
      if(!clip_paths->get_json().isEmpty()) {
        import.warn("Unsupported clip path resources");
      }
    }
    auto ux = get_ux(*resources);
    if(!ux) {
      return;
    }
    if(auto swatches = ux->get_array("colorSwatches"_L1)) {
      import_color_swatches(*swatches, import, ids);
    }
    if(auto library = ux->get_object("documentLibrary"_L1)) {
      import_library(*library, import, ids);
    }
    ux->ignore({"gridDefaults"_L1, "symbolsMetadata"_L1,
      "artboardGuidesGlobalSettings"_L1, "colorProfile"_L1});
  });
}

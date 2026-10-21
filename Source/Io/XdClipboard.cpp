#include <algorithm>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <private/qzipwriter_p.h>
#include "Rellu/Io/XdClipboard.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"

using namespace Rellu;

namespace {
  const auto VERSION = QStringLiteral("1.5.0");

  QJsonObject make_matrix(const Matrix& matrix) {
    auto json = QJsonObject();
    json.insert("a", matrix.m_a);
    json.insert("b", matrix.m_b);
    json.insert("c", matrix.m_c);
    json.insert("d", matrix.m_d);
    json.insert("tx", matrix.m_tx);
    json.insert("ty", matrix.m_ty);
    return json;
  }

  QJsonObject make_rect(const Rect& rect) {
    auto json = QJsonObject();
    json.insert("x", rect.m_x);
    json.insert("y", rect.m_y);
    json.insert("width", rect.m_width);
    json.insert("height", rect.m_height);
    return json;
  }

  QJsonObject make_content(QJsonArray children) {
    auto content = QJsonObject();
    content.insert("version", VERSION);
    content.insert("children", std::move(children));
    return content;
  }

  QByteArray to_bytes(const QJsonObject& json) {
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
  }

  QString decode(const QByteArray& data) {
    auto text = QString();
    if(data.size() >= 2 && data[1] == '\0') {
      text = QString::fromUtf16(
        reinterpret_cast<const char16_t*>(data.constData()), data.size() / 2);
    } else {
      text = QString::fromUtf8(data);
    }
    while(!text.isEmpty() && text.back() == QChar(0)) {
      text.chop(1);
    }
    return text;
  }

  QJsonObject get_ux(const QJsonObject& object) {
    return object.value("meta").toObject().value("ux").toObject();
  }

  void set_ux(QJsonObject& object, const QJsonObject& ux) {
    auto meta = object.value("meta").toObject();
    if(ux.isEmpty()) {
      meta.remove("ux");
    } else {
      meta.insert("ux", ux);
    }
    if(meta.isEmpty()) {
      object.remove("meta");
    } else {
      object.insert("meta", meta);
    }
  }

  template<typename F>
  QJsonValue transform_objects(const QJsonValue& value, F& transform) {
    if(value.isArray()) {
      auto array = QJsonArray();
      for(auto element : value.toArray()) {
        array.append(transform_objects(element, transform));
      }
      return array;
    } else if(!value.isObject()) {
      return value;
    }
    auto object = value.toObject();
    transform(object);
    for(auto entry = object.begin(); entry != object.end(); ++entry) {
      *entry = transform_objects(*entry, transform);
    }
    return object;
  }

  QJsonValue strip_ids(const QJsonValue& value) {
    auto strip = [] (QJsonObject& object) {
      if(!object.contains("type")) {
        return;
      }
      if(auto id = object.take("id"); id.isString()) {
        auto ux = get_ux(object);
        ux.insert("sourceGuid", id);
        set_ux(object, ux);
      }
    };
    return transform_objects(value, strip);
  }

  QJsonValue restore_ids(const QJsonValue& value) {
    auto restore = [] (QJsonObject& object) {
      if(!object.contains("type")) {
        return;
      }
      auto ux = get_ux(object);
      if(auto id = ux.take("sourceGuid"); id.isString() &&
          !object.contains("id")) {
        object.insert("id", id);
      }
      ux.remove("symbolOnClipboard");
      ux.remove("definitionModTime");
      if(object.contains("meta")) {
        set_ux(object, ux);
      }
    };
    return transform_objects(value, restore);
  }

  QJsonValue mark_instances(
      const QJsonValue& value, const QJsonObject& times) {
    auto mark = [&] (QJsonObject& object) {
      auto ux = get_ux(object);
      auto symbol = ux.value("symbolId").toString();
      if(symbol.isEmpty()) {
        return;
      }
      ux.insert("symbolOnClipboard", true);
      if(auto time = times.value(symbol); time.isDouble()) {
        ux.insert("definitionModTime", time);
      }
      set_ux(object, ux);
    };
    return transform_objects(value, mark);
  }

  void collect_images(const QJsonValue& value,
      std::vector<std::pair<QString, QByteArray>>& entries) {
    if(value.isArray()) {
      for(auto element : value.toArray()) {
        collect_images(element, entries);
      }
      return;
    } else if(!value.isObject()) {
      return;
    }
    auto object = value.toObject();
    if(auto pattern = object.value("pattern").toObject();
        pattern.contains("href")) {
      auto uid = pattern.value("meta").toObject().value("ux").toObject().
        value("uid").toString();
      auto file = QFile(pattern.value("href").toString());
      if(!uid.isEmpty() && file.open(QIODevice::ReadOnly)) {
        entries.emplace_back("resources/" + uid, file.readAll());
      }
    }
    for(auto entry : object) {
      collect_images(entry, entries);
    }
  }

  const Component* find_master(
      const Document& document, const NodeId& id) {
    for(auto& entry : document.get_resources().m_components) {
      if(entry.second.m_master == id) {
        return &entry.second;
      }
    }
    return nullptr;
  }

  void collect_components(const Document& document, const NodeId& id,
      std::vector<ComponentId>& components) {
    auto node = document.find(id);
    if(!node) {
      return;
    }
    if(auto component = find_master(document, id)) {
      if(std::ranges::contains(components, component->m_id)) {
        return;
      }
      components.push_back(component->m_id);
    }
    for(auto& child : node->m_children) {
      collect_components(document, child, components);
    }
    auto group = node->get_group();
    if(!group || !group->m_component_instance) {
      return;
    }
    auto& instance = *group->m_component_instance;
    for(auto& state : instance.m_states) {
      collect_components(document, state.m_tree, components);
    }
    auto& resources = document.get_resources().m_components;
    if(auto component = resources.find(instance.m_component);
        component != resources.end() &&
        !std::ranges::contains(components, component->first)) {
      collect_components(document, component->second.m_master, components);
    }
  }

  std::vector<ComponentId> collect_components(
      const Document& document, const std::vector<NodeId>& roots) {
    auto components = std::vector<ComponentId>();
    for(auto& root : roots) {
      auto found = std::vector<ComponentId>();
      collect_components(document, root, found);
      for(auto& id : found | std::views::reverse) {
        if(!std::ranges::contains(components, id)) {
          components.push_back(id);
        }
      }
    }
    return components;
  }

  QJsonValue link_images(const QJsonValue& value, const Document& document,
      const std::filesystem::path& directory) {
    if(value.isArray()) {
      auto array = QJsonArray();
      for(auto element : value.toArray()) {
        array.append(link_images(element, document, directory));
      }
      return array;
    } else if(!value.isObject()) {
      return value;
    }
    auto object = value.toObject();
    for(auto entry = object.begin(); entry != object.end(); ++entry) {
      *entry = link_images(*entry, document, directory);
    }
    if(!object.contains("href") || !object.value("meta").isObject()) {
      return object;
    }
    auto uid = object.value("meta").toObject().value("ux").toObject().value(
      "uid").toString().toStdString();
    if(uid.empty()) {
      return object;
    }
    for(auto& entry : document.get_resources().m_images) {
      if(to_uid(entry.first) != uid) {
        continue;
      }
      auto path = directory / uid;
      auto file = QFile(QString::fromStdU16String(path.u16string()));
      auto& data = *entry.second.m_data;
      if(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
          file.write(reinterpret_cast<const char*>(data.data()),
            static_cast<qsizetype>(data.size())) ==
          static_cast<qsizetype>(data.size())) {
        object.insert("href", QString::fromStdU16String(
          path.make_preferred().u16string()));
      }
      break;
    }
    return object;
  }

  QByteArray make_archive(
      const std::vector<std::pair<QString, QByteArray>>& entries) {
    auto bytes = QByteArray();
    auto buffer = QBuffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    auto writer = QZipWriter(&buffer);
    for(auto& entry : entries) {
      writer.addFile(entry.first, entry.second);
    }
    writer.close();
    return bytes;
  }
}

QByteArray Rellu::write_xd_clipboard(const Document& document,
    const std::vector<XdClipboardItem>& items, const Rect& bounds,
    const XdTextLayout& layout, const std::filesystem::path& directory) {
  auto ids = std::vector<NodeId>();
  for(auto& item : items) {
    ids.push_back(item.m_id);
  }
  auto options = XdWriteOptions();
  options.m_layout = layout;
  auto text = write_xd_nodes(document, ids, options);
  auto nodes = QJsonDocument::fromJson(QByteArray(text.data(),
    static_cast<qsizetype>(text.size()))).array();
  auto children = QJsonArray();
  auto artboards = QJsonObject();
  auto offsets = QJsonArray();
  auto artboard_ids = QJsonArray();
  for(auto i = std::size_t(0); i != items.size(); ++i) {
    auto& item = items[i];
    auto& node = document.get(item.m_id);
    auto json = nodes.at(static_cast<qsizetype>(i)).toObject();
    if(auto artboard = node.get_artboard()) {
      auto reference =
        json.value("artboard").toObject().value("ref").toString();
      auto frame = QJsonObject();
      frame.insert("width", artboard->m_frame.m_width);
      frame.insert("height", artboard->m_frame.m_height);
      frame.insert("name", QString::fromStdString(node.m_header.m_name));
      frame.insert("x", artboard->m_frame.m_x);
      frame.insert("y", artboard->m_frame.m_y);
      if(artboard->m_viewport_height) {
        frame.insert("viewportHeight", *artboard->m_viewport_height);
      }
      artboards.insert(reference, frame);
    } else {
      if(!node.m_header.m_transform_3d) {
        json.insert("transform", make_matrix(item.m_transform));
      }
      if(item.m_offset) {
        auto offset = QJsonObject();
        offset.insert("x", item.m_offset->m_x);
        offset.insert("y", item.m_offset->m_y);
        offsets.append(offset);
      } else {
        offsets.append(QJsonValue::Null);
      }
      artboard_ids.append(QJsonValue::Null);
    }
    children.append(strip_ids(json));
  }
  auto resources = QJsonObject();
  resources.insert("gradients", QJsonObject());
  resources.insert("clipPaths", QJsonObject());
  resources.insert("patterns", QJsonObject());
  auto components = collect_components(document, ids);
  if(!components.empty()) {
    auto masters = std::vector<NodeId>();
    auto& definitions = document.get_resources().m_components;
    for(auto& id : components) {
      masters.push_back(definitions.at(id).m_master);
    }
    auto serialized = write_xd_nodes(document, masters, options);
    auto symbols = QJsonDocument::fromJson(QByteArray(serialized.data(),
      static_cast<qsizetype>(serialized.size()))).array();
    auto times = QJsonObject();
    for(auto symbol : symbols) {
      auto ux = get_ux(symbol.toObject());
      if(auto time = ux.value("modTime"); time.isDouble()) {
        times.insert(ux.value("symbolId").toString(), time);
      }
    }
    children = mark_instances(children, times).toArray();
    auto ux = QJsonObject();
    ux.insert("symbols", mark_instances(symbols, times));
    auto meta = QJsonObject();
    meta.insert("ux", ux);
    resources.insert("meta", meta);
  }
  if(!directory.empty()) {
    children = link_images(children, document, directory).toArray();
    resources = link_images(resources, document, directory).toObject();
  }
  auto ux = QJsonObject();
  if(!offsets.isEmpty()) {
    ux.insert("artboardOffset", offsets);
    ux.insert("artboardId", artboard_ids);
  }
  ux.insert("groupItems", false);
  auto meta = QJsonObject();
  meta.insert("ux", ux);
  auto root = make_content(children);
  root.insert("resources", resources);
  root.insert("artboards", artboards);
  root.insert("viewSource", make_rect(bounds));
  root.insert("meta", meta);
  auto encoded = QString::fromUtf8(to_bytes(root));
  encoded.append(QChar(0));
  return QByteArray(reinterpret_cast<const char*>(encoded.utf16()),
    encoded.size() * static_cast<qsizetype>(sizeof(char16_t)));
}

XdClipboardContent Rellu::read_xd_clipboard(
    const QByteArray& data, Document& document) {
  auto error = QJsonParseError();
  auto parsed = QJsonDocument::fromJson(decode(data).toUtf8(), &error);
  if(error.error != QJsonParseError::NoError || !parsed.isObject() ||
      !parsed.object().value("children").isArray()) {
    throw std::invalid_argument("The clipboard data is not Adobe XD's.");
  }
  auto root = parsed.object();
  root.insert("children", restore_ids(root.value("children")));
  root.insert("resources", restore_ids(root.value("resources")));
  auto offsets = root.value("meta").toObject().value("ux").toObject().value(
    "artboardOffset").toArray();
  auto content = XdClipboardContent();
  auto pasteboard = QJsonArray();
  auto artwork = QJsonArray();
  auto pasteboard_entry = QJsonObject();
  pasteboard_entry.insert("path", "pasteboard");
  artwork.append(pasteboard_entry);
  auto entries = std::vector<std::pair<QString, QByteArray>>();
  auto artboard_entries = std::vector<std::pair<QString, QByteArray>>();
  auto children = root.value("children").toArray();
  for(auto i = qsizetype(0); i != children.size(); ++i) {
    auto child = children[i].toObject();
    if(child.value("type").toString() == "artboard") {
      auto path = QString("artboard-%1").arg(artboard_entries.size() + 1);
      auto entry = QJsonObject();
      entry.insert("path", path);
      artwork.append(entry);
      artboard_entries.emplace_back("artwork/" + path +
        "/graphics/graphicContent.agc",
        to_bytes(make_content(QJsonArray({child}))));
      continue;
    }
    pasteboard.append(child);
    auto offset = offsets.at(i).toObject();
    if(offset.contains("x") && offset.contains("y")) {
      content.m_offsets.emplace_back(Point(offset.value("x").toDouble(),
        offset.value("y").toDouble()));
    } else {
      content.m_offsets.emplace_back();
    }
  }
  auto artwork_entry = QJsonObject();
  artwork_entry.insert("path", "artwork");
  artwork_entry.insert("children", artwork);
  auto manifest = QJsonObject();
  manifest.insert("manifest-format-version", 6);
  manifest.insert("children", QJsonArray({artwork_entry}));
  entries.emplace_back(QString::fromUtf8(XdPackage::MANIFEST.data(),
    static_cast<qsizetype>(XdPackage::MANIFEST.size())), to_bytes(manifest));
  auto resources = make_content(QJsonArray());
  resources.insert("resources", root.value("resources").toObject());
  resources.insert("artboards", root.value("artboards").toObject());
  entries.emplace_back(QString::fromUtf8(XdPackage::RESOURCES.data(),
    static_cast<qsizetype>(XdPackage::RESOURCES.size())), to_bytes(resources));
  entries.emplace_back(QString::fromUtf8(XdPackage::PASTEBOARD.data(),
    static_cast<qsizetype>(XdPackage::PASTEBOARD.size())),
    to_bytes(make_content(pasteboard)));
  entries.insert(
    entries.end(), artboard_entries.begin(), artboard_entries.end());
  collect_images(children, entries);
  collect_images(root.value("resources"), entries);
  import_xd(XdPackage(make_archive(entries)), document);
  auto source = root.value("viewSource").toObject();
  if(source.contains("width") && source.contains("height")) {
    content.m_bounds = Rect(source.value("x").toDouble(),
      source.value("y").toDouble(), source.value("width").toDouble(),
      source.value("height").toDouble());
  }
  return content;
}

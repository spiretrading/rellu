#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <optional>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <QFile>
#include <QtZlib/zlib.h>
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Io/BitmapExport.hpp"
#include "Rellu/Io/XdError.hpp"
#include "Rellu/Io/XdMetadata.hpp"
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdPatch.hpp"
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/CompoundResolution.hpp"
#include "Parallel.hpp"

using namespace Rellu;

namespace {
  const auto MIMETYPE =
    std::string_view("application/vnd.adobe.sparkler.project+dcxucf");
  const auto METADATA = std::string_view("META-INF/metadata.xml");
  const auto SHARING = std::string_view("sharing/sharing.json");
  const auto PREVIEW = std::string_view("preview.png");
  const auto THUMBNAIL = std::string_view("thumbnail.png");
  const auto RENDITIONS = std::string_view("renditions/");
  const auto COMPOUND_TOLERANCE = 0.01;

  using Hook = std::function<void (std::size_t, std::string&)>;

  struct Entry {
    std::string m_name;
    std::string m_data;
    bool m_is_compressed = false;
    std::string m_compressed;
    std::uint32_t m_crc = 0;
  };

  std::string to_id(const NodeId& id) {
    return Rellu::to_string(id);
  }

  std::string new_id() {
    return to_id(generate_node_id());
  }

  bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
      text.substr(text.size() - suffix.size()) == suffix;
  }

  bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
  }

  bool is_stored(std::string_view name) {
    return name == "mimetype" || name == METADATA ||
      starts_with(name, "renditions/") || ends_with(name, ".png") ||
      ends_with(name, ".jpg") || ends_with(name, ".jpeg") ||
      ends_with(name, ".gif") || (starts_with(name, "resources/") &&
        !starts_with(name, "resources/graphics/"));
  }

  double get_area(const Path& path) {
    auto area = 0.0;
    for(auto& polyline : flatten(path, COMPOUND_TOLERANCE)) {
      auto& points = polyline.m_points;
      for(auto i = std::size_t(0); i != points.size(); ++i) {
        area += cross(points[i], points[(i + 1) % points.size()]);
      }
    }
    return std::abs(area) / 2;
  }

  std::optional<Path> refresh_compound(const Document& document,
      const Node& node, const CompoundShape& compound) {
    if(!decode_node_data(node.m_header.m_opaque.get_bytes()).
        m_orphans.empty()) {
      return std::nullopt;
    }
    auto nested = [&] (const Node& inner) {
      auto& geometry = std::get<CompoundShape>(inner.get_shape()->m_geometry);
      if(auto result = refresh_compound(document, inner, geometry)) {
        return *result;
      }
      return *geometry.m_path;
    };
    auto result = combine_operands(
      document.get_nodes(), node, COMPOUND_TOLERANCE, nested);
    if(!result) {
      return std::nullopt;
    }
    auto& stored = *compound.m_path;
    auto difference = get_area(
      combine(*result, stored, BooleanOperation::EXCLUDE, COMPOUND_TOLERANCE));
    auto total = get_area(
      combine(*result, stored, BooleanOperation::ADD, COMPOUND_TOLERANCE));
    if(difference <= std::max(0.01 * total, 0.5)) {
      return std::nullopt;
    }
    return result;
  }

  void serialize(const XdValue& value, std::string& out,
      const XdStructure& structure, std::vector<std::string_view>& path,
      const Hook& hook) {
    if(!value.is_object()) {
      append_json(value, out);
      return;
    }
    out += '{';
    auto is_first = true;
    for(auto& member : value.get_members()) {
      if(!is_first) {
        out += ',';
      }
      is_first = false;
      append_json(XdValue(std::string_view(member.m_key)), out);
      out += ':';
      path.push_back(member.m_key);
      auto index = structure.m_paths.size();
      auto is_prefix = false;
      for(auto i = std::size_t(0); i != structure.m_paths.size(); ++i) {
        auto& candidate = structure.m_paths[i];
        if(candidate.size() < path.size() ||
            !std::equal(path.begin(), path.end(), candidate.begin())) {
          continue;
        }
        if(candidate.size() == path.size()) {
          index = i;
        } else {
          is_prefix = true;
        }
      }
      if(index != structure.m_paths.size() && member.m_value.is_array()) {
        hook(index, out);
      } else if(is_prefix) {
        serialize(member.m_value, out, structure, path, hook);
      } else {
        append_json(member.m_value, out);
      }
      path.pop_back();
    }
    out += '}';
  }

  void serialize(const XdValue& value, std::string& out,
      const XdStructure& structure, const Hook& hook) {
    auto path = std::vector<std::string_view>();
    serialize(value, out, structure, path, hook);
  }

  std::uint16_t to_dos_time(std::chrono::system_clock::time_point time) {
    auto seconds = std::chrono::floor<std::chrono::seconds>(time);
    auto day = std::chrono::floor<std::chrono::days>(seconds);
    auto clock = std::chrono::hh_mm_ss(seconds - day);
    return static_cast<std::uint16_t>((clock.hours().count() << 11) |
      (clock.minutes().count() << 5) | (clock.seconds().count() / 2));
  }

  std::uint16_t to_dos_date(std::chrono::system_clock::time_point time) {
    auto date = std::chrono::year_month_day(
      std::chrono::floor<std::chrono::days>(time));
    auto year = std::max(0, static_cast<int>(date.year()) - 1980);
    return static_cast<std::uint16_t>((year << 9) |
      (static_cast<unsigned>(date.month()) << 5) |
      static_cast<unsigned>(date.day()));
  }

  void put16(std::string& out, std::uint32_t value) {
    out += static_cast<char>(value & 0xFF);
    out += static_cast<char>((value >> 8) & 0xFF);
  }

  void put32(std::string& out, std::uint32_t value) {
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
  }

  void compress(Entry& entry) {
    entry.m_crc = static_cast<std::uint32_t>(crc32(0,
      reinterpret_cast<const Bytef*>(entry.m_data.data()),
      static_cast<uInt>(entry.m_data.size())));
    if(is_stored(entry.m_name) || entry.m_data.empty()) {
      return;
    }
    auto stream = z_stream();
    std::memset(&stream, 0, sizeof(stream));
    if(deflateInit2(&stream, 1, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
      throw XdError("The package cannot be compressed.");
    }
    entry.m_compressed.resize(
      deflateBound(&stream, static_cast<uLong>(entry.m_data.size())));
    stream.next_in = reinterpret_cast<Bytef*>(entry.m_data.data());
    stream.avail_in = static_cast<uInt>(entry.m_data.size());
    stream.next_out = reinterpret_cast<Bytef*>(entry.m_compressed.data());
    stream.avail_out = static_cast<uInt>(entry.m_compressed.size());
    auto status = deflate(&stream, Z_FINISH);
    auto size = stream.total_out;
    deflateEnd(&stream);
    if(status != Z_STREAM_END) {
      throw XdError("The package cannot be compressed.");
    }
    entry.m_compressed.resize(size);
    entry.m_is_compressed = true;
  }

  QByteArray make_archive(std::vector<Entry>& entries) {
    parallel_for(entries.size(), [&] (std::size_t i) {
      compress(entries[i]);
    });
    auto now = std::chrono::system_clock::now();
    auto time = to_dos_time(now);
    auto date = to_dos_date(now);
    auto out = std::string();
    auto total = std::size_t(0);
    for(auto& entry : entries) {
      total += entry.m_name.size() * 2 + 128 + (entry.m_is_compressed ?
        entry.m_compressed.size() : entry.m_data.size());
    }
    if(total >= 0xFFFFFFFFULL || entries.size() >= 0xFFFF) {
      throw XdError("The package is too large to write.");
    }
    out.reserve(total);
    auto directory = std::string();
    for(auto& entry : entries) {
      auto offset = static_cast<std::uint32_t>(out.size());
      auto& data = entry.m_is_compressed ? entry.m_compressed : entry.m_data;
      auto method = entry.m_is_compressed ? 8U : 0U;
      auto flags = entry.m_is_compressed ? 6U : 0U;
      put32(out, 0x04034B50);
      put16(out, 20);
      put16(out, flags);
      put16(out, method);
      put16(out, time);
      put16(out, date);
      put32(out, entry.m_crc);
      put32(out, static_cast<std::uint32_t>(data.size()));
      put32(out, static_cast<std::uint32_t>(entry.m_data.size()));
      put16(out, static_cast<std::uint32_t>(entry.m_name.size()));
      put16(out, 0);
      out += entry.m_name;
      out += data;
      put32(directory, 0x02014B50);
      put16(directory, 20);
      put16(directory, 20);
      put16(directory, flags);
      put16(directory, method);
      put16(directory, time);
      put16(directory, date);
      put32(directory, entry.m_crc);
      put32(directory, static_cast<std::uint32_t>(data.size()));
      put32(directory, static_cast<std::uint32_t>(entry.m_data.size()));
      put16(directory, static_cast<std::uint32_t>(entry.m_name.size()));
      put16(directory, 0);
      put16(directory, 0);
      put16(directory, 0);
      put16(directory, 0);
      put32(directory, 0);
      put32(directory, offset);
      directory += entry.m_name;
    }
    auto directory_offset = static_cast<std::uint32_t>(out.size());
    out += directory;
    put32(out, 0x06054B50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put32(out, static_cast<std::uint32_t>(directory.size()));
    put32(out, directory_offset);
    put16(out, 0);
    return QByteArray(out.data(), static_cast<qsizetype>(out.size()));
  }

  bool is_transient(const std::error_code& error) {
    auto condition = error.default_error_condition();
    return condition == std::errc::permission_denied ||
      condition == std::errc::device_or_resource_busy ||
      condition == std::errc::no_lock_available ||
      condition == std::errc::resource_unavailable_try_again;
  }

  void replace(const std::filesystem::path& source,
      const std::filesystem::path& target, std::error_code& error) {
    auto delay = std::chrono::milliseconds(5);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(true) {
      std::filesystem::rename(source, target, error);
      if(!error || !is_transient(error) ||
          std::chrono::steady_clock::now() >= deadline) {
        return;
      }
      std::this_thread::sleep_for(delay);
      delay = std::min(2 * delay, std::chrono::milliseconds(200));
    }
  }

  XdValue* find_child(XdValue& children, std::string_view path) {
    for(auto& child : children.get_elements()) {
      if(auto entry = child.find("path"); entry && entry->to_string() == path) {
        return &child;
      }
    }
    return nullptr;
  }

  XdValue make_component(std::string_view rel, std::string_view type,
      std::string_view name, std::string_view path) {
    auto json = XdValue::make_object();
    json.set("rel", rel);
    json.set("type", type);
    json.set("id", new_id());
    json.set("state", "unmodified");
    json.set("name", name);
    json.set("path", path);
    return json;
  }

  std::string get_rendition_path(QSize size) {
    return std::format("renditions/image-{}-{}.png", size.width(),
      size.height());
  }

  XdValue make_image_component(std::string_view rel, QSize size,
      std::string_view name, std::string_view path) {
    auto json = XdValue::make_object();
    json.set("rel", rel);
    json.set("height", size.height());
    json.set("width", size.width());
    json.set("type", "image/png");
    json.set("state", "unmodified");
    json.set("id", new_id());
    if(rel == "rendition") {
      json.set("name", name);
      json.set("path", path);
    } else {
      json.set("path", path);
      json.set("name", name);
    }
    return json;
  }

  XdValue make_folder(std::string_view name) {
    auto json = XdValue::make_object();
    json.set("id", new_id());
    json.set("name", name);
    json.set("path", name);
    return json;
  }

  XdValue make_graphics() {
    auto graphics = XdValue::make_object();
    graphics.set("components", XdValue::make_array()).append(make_component(
      "primary", "application/vnd.adobe.agc.graphicsTree+json", "",
      "graphicContent.agc"));
    graphics.set("id", new_id());
    graphics.set("name", "graphics");
    graphics.set("path", "graphics");
    return graphics;
  }

  std::string make_metadata(std::string_view id, std::string_view name) {
    auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
    auto stamp = std::format("{:%Y-%m-%dT%H:%M:%S}Z", now);
    auto title = std::string();
    for(auto c : name) {
      if(c == '<') {
        title += "&lt;";
      } else if(c == '>') {
        title += "&gt;";
      } else if(c == '&') {
        title += "&amp;";
      } else if(static_cast<unsigned char>(c) < 0x20) {
        title += ' ';
      } else {
        title += c;
      }
    }
    return std::format(
      "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
      " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      "  <rdf:Description rdf:about=\"\"\n"
      "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
      "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
      "    xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n"
      "   xmp:CreateDate=\"{0}\"\n"
      "   xmp:ModifyDate=\"{0}\"\n"
      "   xmp:MetadataDate=\"{0}\"\n"
      "   dc:format=\"application/vnd.adobe.sparkler.project+dcx\"\n"
      "   xmpMM:DocumentID=\"{1}\"\n"
      "   xmpMM:OriginalDocumentID=\"{1}\"\n"
      "   xmpMM:InstanceID=\"{1}\">\n"
      "   <dc:title>\n"
      "    <rdf:Alt>\n"
      "     <rdf:li xml:lang=\"x-default\">{2}</rdf:li>\n"
      "    </rdf:Alt>\n"
      "   </dc:title>\n"
      "  </rdf:Description>\n"
      " </rdf:RDF>\n"
      "</x:xmpmeta>\n"
      "<?xpacket end=\"w\"?>", stamp, id, title);
  }

  std::string replace_attribute(
      std::string xml, std::string_view name, std::string_view value) {
    auto key = std::string(name) + "=\"";
    auto start = xml.find(key);
    if(start == std::string::npos) {
      return xml;
    }
    start += key.size();
    auto end = xml.find('"', start);
    if(end == std::string::npos) {
      return xml;
    }
    xml.replace(start, end - start, value);
    return xml;
  }

  void renew_ids(XdValue& value, const QString& composite) {
    if(value.is_array()) {
      for(auto& element : value.get_elements()) {
        renew_ids(element, composite);
      }
    } else if(value.is_object()) {
      for(auto& member : value.get_members()) {
        if(member.m_key == "id" &&
            member.m_value.get_type() == XdValue::Type::STRING) {
          auto id = member.m_value.to_string();
          member.m_value = to_id(derive_node_id(QString::fromUtf8(id.data(),
            static_cast<qsizetype>(id.size())), composite));
        } else {
          renew_ids(member.m_value, composite);
        }
      }
    }
  }

  class Writer {
    public:
      Writer(const Document& document, const XdWriteOptions& options);

      QByteArray write();
      std::string write_list(const std::vector<NodeId>& nodes) const;

    private:
      const Document* m_document;
      const XdWriteOptions* m_options;
      XdGenerator m_generator;
      XdPackageData m_data;
      XdValue m_manifest;
      std::string m_composite;
      bool m_is_new_composite;
      std::unordered_map<ImageId, std::string> m_image_names;
      QSize m_preview_size;
      QSize m_thumbnail_size;

      void write_node(const Node& node, std::string& out) const;
      void write_nodes(const std::vector<NodeId>& nodes,
        std::string& out) const;
      std::string write_artboard(const Node& node) const;
      std::string write_pasteboard() const;
      std::string write_resources() const;
      std::string write_interactions() const;
      std::string write_manifest(const std::vector<Entry>& entries) const;
      void update_previews(XdValue& components) const;
      std::string get_image_name(const ImageResource& image) const;
      std::string get_name() const;
  };

  Writer::Writer(const Document& document, const XdWriteOptions& options)
      : m_document(&document),
        m_options(&options),
        m_generator(document, options.m_layout),
        m_data(decode_package_data(document.get_opaque().get_bytes())),
        m_composite(options.m_composite),
        m_is_new_composite(false) {
    for(auto& name : m_data.m_image_names) {
      m_image_names.emplace(name.first, name.second);
    }
    if(options.m_previews && !options.m_previews->m_preview.isNull() &&
        !options.m_previews->m_thumbnail.isNull()) {
      m_preview_size = options.m_previews->m_preview.size();
      m_thumbnail_size = options.m_previews->m_thumbnail.size();
    }
    if(!m_data.m_manifest.empty()) {
      if(auto manifest = parse_xd_value(m_data.m_manifest)) {
        m_manifest = std::move(*manifest);
      }
    }
    if(m_manifest.is_object()) {
      auto recorded = m_manifest.find("id");
      if(m_composite.empty()) {
        if(recorded) {
          m_composite = recorded->to_string();
        }
      } else {
        m_is_new_composite = recorded && recorded->to_string() != m_composite;
      }
    } else if(m_composite.empty()) {
      m_composite = new_id();
    }
  }

  void Writer::write_nodes(
      const std::vector<NodeId>& nodes, std::string& out) const {
    out += '[';
    auto is_first = true;
    for(auto& id : nodes) {
      if(auto node = m_document->find(id)) {
        if(!is_first) {
          out += ',';
        }
        is_first = false;
        write_node(*node, out);
      }
    }
    out += ']';
  }

  void Writer::write_node(const Node& node, std::string& out) const {
    auto json = m_generator.make_node(node);
    if(auto shape = node.get_shape()) {
      if(auto compound = std::get_if<CompoundShape>(&shape->m_geometry)) {
        if(auto result = refresh_compound(*m_document, node, *compound)) {
          json.find("shape")->set("path", to_svg_path(*result));
        }
      }
    }
    auto data = decode_node_data(node.m_header.m_opaque.get_bytes());
    if(!data.m_patch.empty()) {
      auto is_cache_valid = !data.m_has_cache ||
        XdGenerator::get_cache_hash(json) == data.m_cache_hash;
      apply_patch(data.m_patch, json, is_cache_valid);
    }
    serialize(json, out, XdStructure::get_node(),
      [&] (std::size_t index, std::string& out) {
        if(index == 3) {
          write_nodes(m_generator.get_states(node), out);
          return;
        }
        out += '[';
        auto is_first = true;
        auto add = [&] (auto write) {
          if(!is_first) {
            out += ',';
          }
          is_first = false;
          write();
        };
        auto write_orphans = [&] (const NodeId& anchor) {
          for(auto& orphan : data.m_orphans) {
            if(orphan.first == anchor) {
              add([&] {
                auto view = orphan.second;
                append_json(decode_value(view), out);
              });
            }
          }
        };
        auto children = std::unordered_set<NodeId>(
          node.m_children.begin(), node.m_children.end());
        for(auto& id : node.m_children) {
          write_orphans(id);
          if(auto child = m_document->find(id)) {
            add([&] {
              write_node(*child, out);
            });
          }
        }
        for(auto& orphan : data.m_orphans) {
          if(!children.contains(orphan.first)) {
            add([&] {
              auto view = orphan.second;
              append_json(decode_value(view), out);
            });
          }
        }
        out += ']';
      });
  }

  std::string Writer::write_artboard(const Node& node) const {
    auto json = m_generator.make_part(1);
    auto data = decode_node_data(node.m_header.m_opaque.get_bytes());
    if(!data.m_part.empty()) {
      apply_patch(data.m_part, json, true);
    }
    auto out = std::string();
    serialize(json, out, XdStructure::get_part(),
      [&] (std::size_t, std::string& out) {
        out += '[';
        write_node(node, out);
        out += ']';
      });
    return out;
  }

  std::string Writer::write_pasteboard() const {
    auto& nodes = m_generator.get_pasteboard();
    auto json = m_generator.make_part(nodes.size());
    if(!m_data.m_pasteboard.empty()) {
      apply_patch(m_data.m_pasteboard, json, true);
    }
    auto out = std::string();
    serialize(json, out, XdStructure::get_part(),
      [&] (std::size_t, std::string& out) {
        write_nodes(nodes, out);
      });
    return out;
  }

  std::string Writer::write_resources() const {
    auto symbols = order_symbols(*m_document, m_data.m_symbols);
    auto json = m_generator.make_resources(symbols);
    if(!m_data.m_resources.empty()) {
      apply_patch(m_data.m_resources, json, true);
    }
    auto masters = std::vector<NodeId>();
    auto& components = m_document->get_resources().m_components;
    for(auto& id : symbols) {
      masters.push_back(components.at(id).m_master);
    }
    auto out = std::string();
    serialize(json, out, XdStructure::get_resources(),
      [&] (std::size_t, std::string& out) {
        write_nodes(masters, out);
      });
    return out;
  }

  std::string Writer::write_interactions() const {
    auto json = m_generator.make_interactions();
    if(!m_data.m_interactions.empty()) {
      apply_patch(m_data.m_interactions, json, true);
    }
    return to_json(json);
  }

  std::string Writer::get_image_name(const ImageResource& image) const {
    if(auto name = m_image_names.find(image.m_id);
        name != m_image_names.end()) {
      return name->second;
    }
    return "resources/" + to_uid(image.m_id);
  }

  std::string Writer::get_name() const {
    if(!m_options->m_name.empty()) {
      return m_options->m_name;
    }
    if(m_manifest.is_object()) {
      if(auto name = m_manifest.find("name")) {
        return std::string(name->to_string());
      }
    }
    return "Untitled";
  }

  std::string Writer::write_manifest(const std::vector<Entry>& entries) const {
    auto names = std::unordered_set<std::string_view>();
    for(auto& entry : entries) {
      names.insert(entry.m_name);
    }
    auto manifest = m_manifest;
    if(!manifest.is_object()) {
      manifest = XdValue::make_object();
      manifest.set("name",
        m_options->m_name.empty() ? std::string("Untitled") :
          m_options->m_name);
      manifest.set("manifest-format-version", 6);
      manifest.set("uxdesign#applicationVersion", "57.1.12.2");
      manifest.set("type", "application/vnd.adobe.sparkler.project+dcx");
      manifest.set("state", "unmodified");
      manifest.set("components", XdValue::make_array());
      manifest.set("uxdesign#id", new_id());
      manifest.set("id", m_composite);
      manifest.set("uxdesign#compositeID", m_composite);
      manifest.set("uxdesign#version", "2.33");
      manifest.set("children", XdValue::make_array());
    } else {
      if(m_is_new_composite) {
        auto composite = QString::fromStdString(m_composite);
        for(auto key : {"children", "components"}) {
          if(auto value = manifest.find(key)) {
            renew_ids(*value, composite);
          }
        }
      }
      if(!m_options->m_composite.empty()) {
        manifest.set("id", m_composite);
        manifest.set("uxdesign#compositeID", m_composite);
      }
      if(!m_options->m_name.empty()) {
        manifest.set("name", m_options->m_name);
      }
    }
    if(!m_options->m_serialization.empty()) {
      manifest.set("uxdesign#serializationId", m_options->m_serialization);
    }
    if(!m_options->m_id.empty()) {
      manifest.set("uxdesign#id", m_options->m_id);
    }
    if(!m_options->m_parent.empty()) {
      manifest.set("uxdesign#parentDocId", m_options->m_parent);
    }
    if(m_options->m_time.isValid()) {
      manifest.set("uxdesign#applicationVersion", XD_APPLICATION_VERSION);
      manifest.set("uxdesign#version", XD_DOCUMENT_VERSION);
    }
    auto& components = manifest.find("components") ?
      *manifest.find("components") :
      manifest.set("components", XdValue::make_array());
    update_previews(components);
    auto kept = std::vector<XdValue>();
    auto is_metadata_listed = false;
    for(auto& component : components.get_elements()) {
      if(auto path = component.find("path");
          path && names.contains(path->to_string())) {
        if(path->to_string() == METADATA) {
          is_metadata_listed = true;
        }
        kept.push_back(component);
      }
    }
    if(names.contains(METADATA) && !is_metadata_listed) {
      auto rendition = std::find_if(kept.begin(), kept.end(),
        [] (const auto& component) {
          auto rel = component.find("rel");
          return rel && rel->to_string() == "rendition";
        });
      kept.insert(rendition, make_component(
        "metadata", "application/rdf+xml", "xmp-metadata", METADATA));
    }
    components.get_elements() = std::move(kept);
    auto& children = manifest.find("children") ? *manifest.find(
      "children") : manifest.set("children", XdValue::make_array());
    auto artwork = find_child(children, "artwork");
    if(!artwork) {
      artwork = &children.append(make_folder("artwork"));
    }
    auto& artwork_children = artwork->find("children") ?
      *artwork->find("children") :
      artwork->set("children", XdValue::make_array());
    auto entries_by_path = std::unordered_map<std::string, XdValue>();
    auto list = std::vector<XdValue>();
    for(auto& child : artwork_children.get_elements()) {
      auto path = std::string(child.find("path") ?
        child.find("path")->to_string() : std::string_view());
      if(starts_with(path, "artboard-")) {
        entries_by_path.emplace(path, child);
      } else {
        list.push_back(child);
      }
    }
    if(!find_child(artwork_children, "pasteboard")) {
      auto pasteboard = make_folder("pasteboard");
      pasteboard.set("children", XdValue::make_array()).append(
        make_graphics());
      list.insert(list.begin(), std::move(pasteboard));
    }
    for(auto& id : m_generator.get_artboards()) {
      auto& node = m_document->get(id);
      auto& artboard = *node.get_artboard();
      auto path = "artboard-" + to_id(id);
      auto entry = XdValue();
      if(auto existing = entries_by_path.find(path);
          existing != entries_by_path.end()) {
        entry = std::move(existing->second);
      } else {
        entry = XdValue::make_object();
        entry.set("uxdesign#bounds", XdValue::make_object());
        entry.set("children", XdValue::make_array()).append(make_graphics());
        entry.set("id", new_id());
        entry.set("name", node.m_header.m_name);
        entry.set("path", path);
      }
      auto bounds = entry.find("uxdesign#bounds");
      if(!bounds || !bounds->is_object()) {
        bounds = &entry.set("uxdesign#bounds", XdValue::make_object());
      }
      bounds->set("x", artboard.m_frame.m_x);
      bounds->set("y", artboard.m_frame.m_y);
      bounds->set("width", artboard.m_frame.m_width);
      bounds->set("height", artboard.m_frame.m_height);
      if(artboard.m_viewport_height) {
        auto viewport = entry.find("uxdesign#viewport");
        if(!viewport || !viewport->is_object()) {
          viewport = &entry.set("uxdesign#viewport", XdValue::make_object());
        }
        viewport->set("height", *artboard.m_viewport_height);
      } else {
        entry.erase("uxdesign#viewport");
      }
      entry.set("name", node.m_header.m_name);
      list.push_back(std::move(entry));
    }
    artwork_children.get_elements() = std::move(list);
    auto resources = find_child(children, "resources");
    if(!resources) {
      resources = &children.append(make_folder("resources"));
    }
    if(!resources->find("children")) {
      resources->set("children", XdValue::make_array()).append(
        make_graphics());
    }
    if(!resources->find("components") &&
        !m_document->get_resources().m_images.empty()) {
      resources->set("components", XdValue::make_array());
    }
    auto empty_components = XdValue::make_array();
    auto& resource_components = resources->find("components") ?
      *resources->find("components") : empty_components;
    auto images = std::vector<XdValue>();
    auto listed = std::unordered_set<std::string>();
    for(auto& component : resource_components.get_elements()) {
      if(auto path = component.find("path")) {
        auto name = "resources/" + std::string(path->to_string());
        if(names.contains(name) && listed.insert(name).second) {
          images.push_back(component);
        }
      }
    }
    for(auto& entry : m_document->get_resources().m_images) {
      auto name = get_image_name(entry.second);
      if(listed.insert(name).second) {
        auto type = entry.second.m_media_type;
        if(type.empty() || type == "application/octet-stream") {
          type = "image/png";
        }
        images.push_back(make_component(
          "primary", type, "", std::string_view(name).substr(10)));
      }
    }
    resource_components.get_elements() = std::move(images);
    if(names.contains(XdPackage::INTERACTIONS) &&
        !find_child(children, "interactions")) {
      auto interactions = make_folder("interactions");
      interactions.set("components", XdValue::make_array()).append(
        make_component("primary",
          "application/vnd.adobe.uxdesign.interactions+json", "interactions",
          "interactions.json"));
      children.append(std::move(interactions));
    }
    if(!names.contains(SHARING)) {
      auto& elements = children.get_elements();
      std::erase_if(elements, [] (const auto& child) {
        auto path = child.find("path");
        return path && path->to_string() == "sharing";
      });
    }
    return to_json(manifest);
  }

  void Writer::update_previews(XdValue& components) const {
    if(m_preview_size.isEmpty()) {
      return;
    }
    auto has_thumbnail = false;
    auto has_preview = false;
    auto has_preview_rendition = false;
    auto has_thumbnail_rendition = m_thumbnail_size == m_preview_size;
    auto kept = std::vector<XdValue>();
    for(auto& component : components.get_elements()) {
      auto rel = component.find("rel");
      auto name = component.find("name");
      auto rel_text = rel ? rel->to_string() : std::string_view();
      auto name_text = name ? name->to_string() : std::string_view();
      if(rel_text == "thumbnail" || rel_text == "preview") {
        auto is_thumbnail = rel_text == "thumbnail";
        auto& flag = is_thumbnail ? has_thumbnail : has_preview;
        if(flag) {
          continue;
        }
        flag = true;
        auto size = is_thumbnail ? m_thumbnail_size : m_preview_size;
        component.set("height", size.height());
        component.set("width", size.width());
        component.set("path", is_thumbnail ? THUMBNAIL : PREVIEW);
      } else if(rel_text == "rendition") {
        auto is_thumbnail = name_text == "thumbnail";
        auto& flag =
          is_thumbnail ? has_thumbnail_rendition : has_preview_rendition;
        if(flag || (name_text != "thumbnail" && name_text != "preview")) {
          continue;
        }
        flag = true;
        auto size = is_thumbnail ? m_thumbnail_size : m_preview_size;
        component.set("height", size.height());
        component.set("width", size.width());
        component.set("path", get_rendition_path(size));
      }
      kept.push_back(std::move(component));
    }
    auto front = std::vector<XdValue>();
    if(!has_thumbnail) {
      front.push_back(make_image_component(
        "thumbnail", m_thumbnail_size, "thumbnail", THUMBNAIL));
    }
    if(!has_preview) {
      front.push_back(make_image_component(
        "preview", m_preview_size, "preview", PREVIEW));
    }
    kept.insert(kept.begin(), std::make_move_iterator(front.begin()),
      std::make_move_iterator(front.end()));
    if(!has_thumbnail_rendition) {
      kept.push_back(make_image_component("rendition", m_thumbnail_size,
        "thumbnail", get_rendition_path(m_thumbnail_size)));
    }
    if(!has_preview_rendition) {
      kept.push_back(make_image_component("rendition", m_preview_size,
        "preview", get_rendition_path(m_preview_size)));
    }
    components.get_elements() = std::move(kept);
  }

  QByteArray Writer::write() {
    auto& artboards = m_generator.get_artboards();
    auto parts = std::vector<Entry>(artboards.size() + 3);
    parallel_for(parts.size(), [&] (std::size_t i) {
      if(i < artboards.size()) {
        auto& node = m_document->get(artboards[i]);
        parts[i] = Entry("artwork/artboard-" + to_id(node.get_id()) +
          "/graphics/graphicContent.agc", write_artboard(node));
      } else if(i == artboards.size()) {
        parts[i] = Entry(
          std::string(XdPackage::PASTEBOARD), write_pasteboard());
      } else if(i == artboards.size() + 1) {
        parts[i] =
          Entry(std::string(XdPackage::RESOURCES), write_resources());
      } else if(m_data.m_has_interactions ||
          !m_document->get_prototype().m_interactions.empty()) {
        parts[i] = Entry(
          std::string(XdPackage::INTERACTIONS), write_interactions());
      }
    });
    std::erase_if(parts, [] (const auto& entry) {
      return entry.m_name.empty();
    });
    auto available = std::unordered_map<std::string, Entry>();
    for(auto& part : parts) {
      available.emplace(part.m_name, std::move(part));
    }
    for(auto& entry : m_document->get_resources().m_images) {
      auto& bytes = *entry.second.m_data;
      auto name = get_image_name(entry.second);
      available.emplace(name, Entry(name, std::string(
        reinterpret_cast<const char*>(bytes.data()), bytes.size())));
    }
    if(!m_preview_size.isEmpty()) {
      auto preview = encode_png(m_options->m_previews->m_preview);
      auto thumbnail = m_thumbnail_size == m_preview_size ? preview :
        encode_png(m_options->m_previews->m_thumbnail);
      auto add_image = [&] (const std::string& name, const QByteArray& bytes) {
        available.emplace(name, Entry(name, bytes.toStdString()));
      };
      add_image(std::string(PREVIEW), preview);
      add_image(std::string(THUMBNAIL), thumbnail);
      add_image(get_rendition_path(m_preview_size), preview);
      add_image(get_rendition_path(m_thumbnail_size), thumbnail);
    }
    for(auto& part : m_data.m_parts) {
      if(!m_preview_size.isEmpty() && (part.first == PREVIEW ||
          part.first == THUMBNAIL || starts_with(part.first, RENDITIONS))) {
        continue;
      }
      available.try_emplace(part.first, Entry(part.first, part.second));
    }
    if(!available.contains("mimetype")) {
      available.emplace("mimetype",
        Entry("mimetype", std::string(MIMETYPE)));
    }
    auto metadata = available.find(std::string(METADATA));
    if(m_options->m_time.isValid()) {
      auto xmp = save_xd_metadata(m_options->m_metadata, get_name(),
        m_composite, m_options->m_instance, m_options->m_time);
      if(metadata == available.end()) {
        available.emplace(std::string(METADATA),
          Entry(std::string(METADATA), std::move(xmp)));
      } else {
        metadata->second.m_data = std::move(xmp);
      }
    } else if(!m_manifest.is_object() && metadata == available.end()) {
      available.emplace(std::string(METADATA), Entry(std::string(METADATA),
        make_metadata(m_composite, m_options->m_name.empty() ?
          std::string_view("Untitled") : m_options->m_name)));
    } else if(m_is_new_composite && metadata != available.end()) {
      for(auto name : {"xmpMM:DocumentID", "xmpMM:OriginalDocumentID"}) {
        metadata->second.m_data = replace_attribute(
          std::move(metadata->second.m_data), name, m_composite);
      }
    }
    auto order = std::vector<std::string>();
    auto ordered = std::unordered_set<std::string>();
    for(auto& name : m_data.m_entries) {
      if(available.contains(name) && ordered.insert(name).second) {
        order.push_back(name);
      }
    }
    auto add = [&] (const std::string& name) {
      if(available.contains(name) && ordered.insert(name).second) {
        order.push_back(name);
      }
    };
    if(!ordered.contains("mimetype")) {
      order.insert(order.begin(), "mimetype");
      ordered.insert("mimetype");
    }
    add(std::string(XdPackage::MANIFEST));
    add(std::string(METADATA));
    for(auto& id : artboards) {
      add("artwork/artboard-" + to_id(id) + "/graphics/graphicContent.agc");
    }
    add(std::string(XdPackage::PASTEBOARD));
    add(std::string(XdPackage::INTERACTIONS));
    for(auto& entry : m_document->get_resources().m_images) {
      add(get_image_name(entry.second));
    }
    add(std::string(XdPackage::RESOURCES));
    auto remaining = std::vector<std::string>();
    for(auto& entry : available) {
      if(!ordered.contains(entry.first)) {
        remaining.push_back(entry.first);
      }
    }
    std::sort(remaining.begin(), remaining.end());
    for(auto& name : remaining) {
      add(name);
    }
    auto entries = std::vector<Entry>();
    entries.reserve(order.size() + 1);
    for(auto& name : order) {
      entries.push_back(std::move(available.at(name)));
    }
    auto manifest = write_manifest(entries);
    auto position = std::find_if(entries.begin(), entries.end(),
      [] (const auto& entry) {
        return entry.m_name == XdPackage::MANIFEST;
      });
    if(position == entries.end()) {
      auto mimetype = std::find_if(entries.begin(), entries.end(),
        [] (const auto& entry) {
          return entry.m_name == "mimetype";
        });
      entries.insert(mimetype + 1,
        Entry(std::string(XdPackage::MANIFEST), std::move(manifest)));
    } else {
      position->m_data = std::move(manifest);
    }
    return make_archive(entries);
  }

  std::string Writer::write_list(const std::vector<NodeId>& nodes) const {
    auto out = std::string();
    write_nodes(nodes, out);
    return out;
  }
}

std::string Rellu::write_xd_nodes(const Document& document,
    const std::vector<NodeId>& nodes, const XdWriteOptions& options) {
  return Writer(document, options).write_list(nodes);
}

QByteArray Rellu::write_xd(
    const Document& document, const XdWriteOptions& options) {
  auto writer = Writer(document, options);
  return writer.write();
}

void Rellu::write_xd(const Document& document,
    const std::filesystem::path& path, const XdWriteOptions& options) {
  auto bytes = write_xd(document, options);
  auto temporary = path;
  temporary += ".saving";
  {
    auto file = QFile(QString::fromStdU16String(temporary.u16string()));
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(bytes) != bytes.size() || !file.flush()) {
      throw XdError("The package cannot be written: " +
        file.errorString().toStdString());
    }
  }
  auto error = std::error_code();
  replace(temporary, path, error);
  if(error) {
    auto message = error.message();
    std::filesystem::remove(temporary, error);
    auto name = path.generic_u8string();
    throw XdError("The package cannot be replaced: " +
      std::string(name.begin(), name.end()) + ": " + message);
  }
}

std::vector<ComponentId> Rellu::order_symbols(
    const Document& document, const std::vector<NodeId>& order) {
  auto symbols = std::vector<ComponentId>();
  auto added = std::unordered_set<ComponentId>();
  auto& components = document.get_resources().m_components;
  for(auto& id : order) {
    if(components.contains(id) && added.insert(id).second) {
      symbols.push_back(id);
    }
  }
  for(auto& entry : components) {
    if(added.insert(entry.first).second) {
      symbols.push_back(entry.first);
    }
  }
  return symbols;
}

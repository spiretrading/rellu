#include <chrono>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <QJsonArray>
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPatch.hpp"
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Parallel.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  const auto MIMETYPE =
    std::string_view("application/vnd.adobe.sparkler.project+dcxucf");

  void merge(XdPatchCounts& source, XdPatchCounts& target) {
    for(auto& entry : source) {
      target[entry.first] += entry.second;
    }
  }

  std::string to_bytes(const QByteArray& bytes) {
    return std::string(bytes.constData(), static_cast<std::size_t>(
      bytes.size()));
  }

  std::string encode_json(const QJsonValue& value) {
    auto out = std::string();
    encode_value(value, out);
    return out;
  }

  std::string get_root(const QJsonObject& json) {
    auto type = json.value("type"_L1);
    if(type.isString()) {
      return "<" + type.toString().toStdString() + ">";
    }
    return "<?>";
  }

  std::vector<NodeId> read_symbols(const QJsonObject& resources) {
    auto symbols = std::vector<NodeId>();
    auto list = resources.value("resources"_L1).toObject().value(
      "meta"_L1).toObject().value("ux"_L1).toObject().value(
        "symbols"_L1).toArray();
    for(auto symbol : list) {
      auto id = symbol.toObject().value("meta"_L1).toObject().value(
        "ux"_L1).toObject().value("symbolId"_L1).toString();
      if(!id.isEmpty()) {
        symbols.push_back(to_node_id(id));
      }
    }
    return symbols;
  }

  void preserve(XdImport& import) {
    auto start = std::chrono::steady_clock::now();
    auto& document = import.get_document();
    auto& package = import.get_package();
    auto& report = import.get_report();
    auto generator = XdGenerator(document, XdTextLayout());
    auto parts = std::unordered_map<NodeId, const QJsonObject*>();
    auto generated_entries = std::unordered_set<std::string>();
    auto& artboards = import.get_artboards();
    for(auto i = std::size_t(0); i != artboards.size(); ++i) {
      auto& json = artboards[i]->get_json();
      auto children = json.value("children"_L1).toArray();
      if(children.size() != 1) {
        continue;
      }
      auto id = to_node_id(
        children.first().toObject().value("id"_L1).toString());
      auto node = document.find(id);
      if(!node || !node->get_artboard() || !document.is_on_canvas(id)) {
        continue;
      }
      parts.emplace(id, &json);
      generated_entries.insert(package.get_artboard_entries()[i]);
    }
    auto& records = import.get_records();
    auto results = std::vector<std::string>(records.size());
    auto mutex = std::mutex();
    auto counts = XdPatchCounts();
    parallel_for((records.size() + 63) / 64, [&] (std::size_t block) {
      auto local = XdPatchCounts();
      auto end = std::min(records.size(), (block + 1) * 64);
      for(auto i = block * 64; i != end; ++i) {
        auto& record = records[i];
        auto node = document.find(record.m_id);
        if(!node) {
          continue;
        }
        auto json = generator.make_node(*node);
        auto difference = make_patch(QJsonValue(record.m_json), json,
          XdStructure::get_node(), get_root(record.m_json), &local);
        auto data = XdNodeData();
        data.m_patch = difference.m_patch;
        if(difference.m_has_cache) {
          data.m_has_cache = true;
          data.m_cache_hash = XdGenerator::get_cache_hash(json);
        }
        auto orphans = std::vector<std::string>();
        orphans.reserve(record.m_orphans.size());
        for(auto& orphan : record.m_orphans) {
          orphans.push_back(encode_json(QJsonValue(orphan.second)));
          ++local["orphan " + get_root(record.m_json)];
        }
        for(auto j = std::size_t(0); j != orphans.size(); ++j) {
          data.m_orphans.emplace_back(record.m_orphans[j].first, orphans[j]);
        }
        auto part = XdDifference();
        if(auto entry = parts.find(record.m_id); entry != parts.end()) {
          part = make_patch(QJsonValue(*entry->second),
            generator.make_part(1), XdStructure::get_part(), "agc", &local);
          data.m_part = part.m_patch;
        }
        results[i] = encode_node_data(data);
      }
      auto lock = std::lock_guard(mutex);
      merge(local, counts);
    });
    auto interned = std::unordered_map<std::string, OpaqueData>();
    auto& transaction = import.get_transaction();
    for(auto i = std::size_t(0); i != records.size(); ++i) {
      if(results[i].empty()) {
        continue;
      }
      auto entry = interned.find(results[i]);
      if(entry == interned.end()) {
        report.m_opaque_size += results[i].size();
        auto opaque = OpaqueData(results[i]);
        entry = interned.emplace(std::move(results[i]), opaque).first;
      }
      auto header = document.get(records[i].m_id).m_header;
      header.m_opaque = entry->second;
      transaction.replace_header(std::move(header));
      ++report.m_opaque_node_count;
    }
    auto data = XdPackageData();
    data.m_entries = package.get_entries();
    auto& resources = import.get_resources().get_json();
    data.m_symbols = read_symbols(resources);
    data.m_resources = make_patch(QJsonValue(resources),
      generator.make_resources(order_symbols(document, data.m_symbols)),
      XdStructure::get_resources(), "agc", &counts).m_patch;
    data.m_pasteboard = make_patch(
      QJsonValue(import.get_pasteboard().get_json()),
      generator.make_part(generator.get_pasteboard().size()),
      XdStructure::get_part(), "agc", &counts).m_patch;
    data.m_has_interactions = package.contains(XdPackage::INTERACTIONS);
    if(data.m_has_interactions) {
      data.m_interactions = make_patch(
        QJsonValue(import.get_interactions().get_json()),
        generator.make_interactions(), XdStructure::get_none(),
        "interactions", &counts).m_patch;
    }
    data.m_manifest = encode_json(QJsonValue(package.get_manifest()));
    generated_entries.insert(std::string(XdPackage::MANIFEST));
    generated_entries.insert(std::string(XdPackage::RESOURCES));
    generated_entries.insert(std::string(XdPackage::PASTEBOARD));
    generated_entries.insert(std::string(XdPackage::INTERACTIONS));
    auto& images = document.get_resources().m_images;
    auto named = std::unordered_set<ImageId>();
    for(auto& entry : package.get_image_entries()) {
      auto name = QString::fromStdString(entry).mid(10);
      if(auto dot = name.indexOf(u'.'); dot >= 0) {
        name.truncate(dot);
      }
      auto id = to_node_id(name);
      if(!images.contains(id) || !named.insert(id).second) {
        continue;
      }
      generated_entries.insert(entry);
      if(entry != "resources/" + to_uid(id)) {
        data.m_image_names.emplace_back(id, entry);
      }
    }
    auto names = std::vector<std::string_view>();
    for(auto& entry : data.m_entries) {
      if(!generated_entries.contains(entry)) {
        names.push_back(entry);
      }
    }
    auto contents = std::vector<std::string>(names.size());
    auto errors = std::vector<std::exception_ptr>(names.size());
    parallel_for(names.size(), [&] (std::size_t index) {
      try {
        contents[index] = to_bytes(package.read(names[index]));
      } catch(...) {
        errors[index] = std::current_exception();
      }
    });
    for(auto i = std::size_t(0); i != names.size(); ++i) {
      if(errors[i]) {
        std::rethrow_exception(errors[i]);
      }
      if(names[i] != "mimetype" || contents[i] != MIMETYPE) {
        data.m_parts.emplace_back(
          std::string(names[i]), std::move(contents[i]));
      }
    }
    auto bytes = encode_package_data(data);
    report.m_package_size = bytes.size();
    import.set_opaque(OpaqueData(std::move(bytes)));
    merge(counts, report.m_preserved);
    report.m_preservation_time +=
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start);
  }
}

void Rellu::add_preservation_importer(XdImporter& importer) {
  importer.add_document_importer(XdPhase::PRESERVATION, preserve);
}

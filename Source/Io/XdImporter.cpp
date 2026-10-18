#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include "Rellu/Io/XdImporter.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  std::vector<QJsonObject> read_entries(const XdPackage& package,
      const std::vector<std::string_view>& names) {
    auto objects = std::vector<QJsonObject>(names.size());
    auto errors = std::vector<std::exception_ptr>(names.size());
    auto next = std::atomic<std::size_t>(0);
    auto work = [&] {
      while(true) {
        auto index = next.fetch_add(1);
        if(index >= names.size()) {
          return;
        }
        try {
          objects[index] = package.read_json(names[index]);
        } catch(...) {
          errors[index] = std::current_exception();
        }
      }
    };
    auto count = std::min<std::size_t>(names.size(),
      std::max(1U, std::thread::hardware_concurrency()));
    auto threads = std::vector<std::jthread>();
    threads.reserve(count - 1);
    for(auto i = std::size_t(1); i < count; ++i) {
      threads.emplace_back(work);
    }
    work();
    for(auto& thread : threads) {
      thread.join();
    }
    for(auto& error : errors) {
      if(error) {
        std::rethrow_exception(error);
      }
    }
    return objects;
  }

  std::string get_reason(const std::invalid_argument& error) {
    auto message = std::string(error.what());
    if(auto colon = message.find(':'); colon != std::string::npos) {
      message.resize(colon);
    }
    return message;
  }
}

XdImporter::XdImporter() noexcept = default;

void XdImporter::set_node_importer(QString type, NodeImporter importer) {
  for(auto& entry : m_node_importers) {
    if(entry.first == type) {
      entry.second = std::move(importer);
      return;
    }
  }
  m_node_importers.emplace_back(std::move(type), std::move(importer));
}

const XdImporter::NodeImporter* XdImporter::find_node_importer(
    const QString& type) const {
  for(auto& entry : m_node_importers) {
    if(entry.first == type) {
      return &entry.second;
    }
  }
  return nullptr;
}

void XdImporter::add_property_importer(PropertyImporter importer) {
  m_property_importers.push_back(std::move(importer));
}

void XdImporter::add_finisher(NodeFinisher finisher) {
  m_finishers.push_back(std::move(finisher));
}

void XdImporter::add_document_importer(
    XdPhase phase, DocumentImporter importer) {
  m_document_importers.emplace_back(phase, std::move(importer));
}

ImportReport XdImporter::import(
    const XdPackage& package, Document& document) const {
  auto report = ImportReport();
  auto start = std::chrono::steady_clock::now();
  {
    auto transaction = Transaction(document);
    auto import = XdImport(*this, package, document, transaction, report);
    for(auto phase : {XdPhase::RESOURCES, XdPhase::COMPONENTS,
        XdPhase::CANVAS, XdPhase::LINKS}) {
      for(auto& importer : m_document_importers) {
        if(importer.first == phase) {
          importer.second(import);
        }
      }
    }
    transaction.commit();
  }
  document.clear_history();
  report.m_build_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - start) - report.m_read_time;
  return report;
}

XdImport::XdImport(const XdImporter& importer, const XdPackage& package,
    Document& document, Transaction& transaction, ImportReport& report)
    : m_importer(&importer),
      m_package(&package),
      m_document(&document),
      m_transaction(&transaction),
      m_report(&report) {
  auto start = std::chrono::steady_clock::now();
  auto names = std::vector<std::string_view>();
  names.push_back(XdPackage::RESOURCES);
  names.push_back(XdPackage::PASTEBOARD);
  for(auto& entry : package.get_artboard_entries()) {
    names.push_back(entry);
  }
  auto has_interactions = package.contains(XdPackage::INTERACTIONS);
  if(has_interactions) {
    names.push_back(XdPackage::INTERACTIONS);
  }
  auto objects = read_entries(package, names);
  m_resources = std::make_unique<XdObject>(
    std::move(objects[0]), "agc", report);
  m_pasteboard = std::make_unique<XdObject>(
    std::move(objects[1]), "agc", report);
  auto artboard_count = package.get_artboard_entries().size();
  m_artboards.reserve(artboard_count);
  for(auto i = std::size_t(0); i != artboard_count; ++i) {
    m_artboards.push_back(
      std::make_unique<XdObject>(std::move(objects[i + 2]), "agc", report));
  }
  if(has_interactions) {
    m_interactions = std::make_unique<XdObject>(
      std::move(objects.back()), "interactions", report);
  } else {
    m_interactions =
      std::make_unique<XdObject>(QJsonObject(), "interactions", report);
  }
  report.m_read_time += std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - start);
}

XdImport::~XdImport() = default;

const XdPackage& XdImport::get_package() const {
  return *m_package;
}

Document& XdImport::get_document() {
  return *m_document;
}

Transaction& XdImport::get_transaction() {
  return *m_transaction;
}

ImportReport& XdImport::get_report() {
  return *m_report;
}

XdStates& XdImport::get_states() {
  return m_states;
}

XdObject& XdImport::get_resources() {
  return *m_resources;
}

XdObject& XdImport::get_pasteboard() {
  return *m_pasteboard;
}

const std::vector<std::unique_ptr<XdObject>>& XdImport::get_artboards() {
  return m_artboards;
}

XdObject& XdImport::get_interactions() {
  return *m_interactions;
}

std::unique_ptr<XdObject> XdImport::make_node_json(const QJsonObject& json) {
  auto type = json.value("type"_L1);
  auto path = std::string("<");
  if(type.isString()) {
    path += type.toString().toStdString();
  } else {
    path += '?';
  }
  path += '>';
  return std::make_unique<XdObject>(json, std::move(path), *m_report);
}

NodeId XdImport::import_node(
    XdObject& json, XdNode* parent, XdNodeRole role) {
  auto type = json.get_json().value("type"_L1).toString();
  auto importer = m_importer->find_node_importer(type);
  if(!importer) {
    if(type.isEmpty()) {
      warn("Node without a type");
    } else {
      warn("Unsupported node type " + type.toStdString());
    }
    return NodeId();
  }
  json.consume("type"_L1);
  auto node = XdNode(this, &json, parent, role, std::move(type));
  (*importer)(node);
  if(!node.m_payload) {
    return NodeId();
  }
  for(auto& property_importer : m_importer->m_property_importers) {
    property_importer(node);
  }
  try {
    if(role == XdNodeRole::NODE) {
      auto parent_id = parent ? parent->m_header.m_id : NodeId();
      node.m_header.m_id = m_transaction->append(
        parent_id, node.m_header, std::move(*node.m_payload));
    } else {
      node.m_header.m_id = m_transaction->insert_root(
        node.m_header, std::move(*node.m_payload));
    }
  } catch(const std::invalid_argument& error) {
    auto message = "Node not inserted: " + get_reason(error) + " (" +
      node.m_type.toStdString();
    if(parent) {
      message += " in " + parent->m_type.toStdString();
    }
    warn(message + ")");
    return NodeId();
  }
  node.m_payload = std::nullopt;
  node.m_is_inserted = true;
  ++m_report->m_node_count;
  if(node.m_children) {
    for(auto child : node.m_children->get_json()) {
      if(child.isObject()) {
        auto child_json = make_node_json(child.toObject());
        import_node(*child_json, &node, XdNodeRole::NODE);
      }
    }
  }
  for(auto& finisher : m_importer->m_finishers) {
    finisher(node);
  }
  return node.m_header.m_id;
}

void XdImport::warn(std::string_view message) {
  auto entry = m_report->m_warnings.find(message);
  if(entry == m_report->m_warnings.end()) {
    m_report->m_warnings.emplace(std::string(message), 1);
  } else {
    ++entry->second;
  }
}

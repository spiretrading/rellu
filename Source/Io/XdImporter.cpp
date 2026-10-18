#include <algorithm>
#include <chrono>
#include <deque>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include "Rellu/Io/XdImporter.hpp"
#include "Parallel.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  std::string get_reason(const std::invalid_argument& error) {
    auto message = std::string(error.what());
    if(auto colon = message.find(':'); colon != std::string::npos) {
      message.resize(colon);
    }
    return message;
  }

  bool has_valid_runs(const Payload& payload) {
    auto text = std::get_if<Box<Text>>(&payload);
    if(!text || (*text)->m_runs.empty()) {
      return true;
    }
    auto length = std::size_t(0);
    for(auto& run : (*text)->m_runs) {
      length += run.m_length;
    }
    return length == (*text)->m_content->size();
  }

  void add_counts(const auto& source, auto& target) {
    for(auto& entry : source) {
      if(auto i = target.find(entry.first); i != target.end()) {
        i->second += entry.second;
      } else {
        target.emplace(entry.first, entry.second);
      }
    }
  }
}

struct XdImport::Batch {
  struct Pending {
    Node m_node;
    XdNodeRole m_role;
  };
  ImportReport m_report;
  std::deque<Pending> m_pending;
  std::unordered_map<NodeId, std::size_t> m_index;
  std::vector<NodeId> m_misses;
};

XdStates XdStates::fork() const {
  auto states = XdStates();
  states.m_states.reserve(m_states.size());
  for(auto& entry : m_states) {
    if(entry.m_fork) {
      states.m_states.push_back(Entry(entry.m_type,
        entry.m_fork(entry.m_state.get()), entry.m_fork, entry.m_merge,
        true));
    } else {
      states.m_states.push_back(Entry(entry.m_type, entry.m_state,
        entry.m_fork, entry.m_merge, false));
    }
  }
  return states;
}

void XdStates::merge(XdStates states) {
  for(auto& entry : states.m_states) {
    if(!entry.m_is_own) {
      continue;
    }
    auto state = std::find_if(m_states.begin(), m_states.end(),
      [&] (const auto& state) {
        return state.m_type == entry.m_type;
      });
    if(state == m_states.end()) {
      m_states.push_back(std::move(entry));
    } else if(state->m_merge) {
      state->m_merge(state->m_state.get(), entry.m_state.get());
    }
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
  auto opaque = OpaqueData();
  {
    auto transaction = Transaction(document);
    auto import = XdImport(*this, package, document, transaction, report);
    for(auto phase : {XdPhase::RESOURCES, XdPhase::COMPONENTS,
        XdPhase::CANVAS, XdPhase::LINKS, XdPhase::PRESERVATION}) {
      for(auto& importer : m_document_importers) {
        if(importer.first == phase) {
          importer.second(import);
        }
      }
    }
    opaque = import.get_opaque();
    transaction.commit();
  }
  if(!opaque.is_empty()) {
    document.set_opaque(std::move(opaque));
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
      m_report(&report),
      m_main(this) {
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
  auto objects = std::vector<QJsonObject>(names.size());
  auto errors = std::vector<std::exception_ptr>(names.size());
  parallel_for(names.size(), [&] (std::size_t index) {
    try {
      objects[index] = package.read_json(names[index]);
    } catch(...) {
      errors[index] = std::current_exception();
    }
  });
  for(auto& error : errors) {
    if(error) {
      std::rethrow_exception(error);
    }
  }
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

XdImport::XdImport(XdImport& main)
    : m_importer(main.m_importer),
      m_package(main.m_package),
      m_document(main.m_document),
      m_transaction(nullptr),
      m_report(nullptr),
      m_main(&main),
      m_states(main.m_states.fork()),
      m_batch(std::make_unique<Batch>()) {
  m_report = &m_batch->m_report;
}

XdImport::~XdImport() {
  if(m_batch) {
    return;
  }
  m_resources = nullptr;
  m_pasteboard = nullptr;
  m_artboards.clear();
  m_interactions = nullptr;
  auto block_size = std::size_t(1024);
  parallel_for((m_records.size() + block_size - 1) / block_size,
    [&] (std::size_t block) {
      auto end = std::min(m_records.size(), (block + 1) * block_size);
      for(auto i = block * block_size; i != end; ++i) {
        m_records[i] = XdNodeRecord();
      }
    });
}

const XdPackage& XdImport::get_package() const {
  return *m_package;
}

Document& XdImport::get_document() {
  return *m_document;
}

Transaction& XdImport::get_transaction() {
  if(!m_transaction) {
    throw std::logic_error("No transaction within a parallel import.");
  }
  return *m_transaction;
}

ImportReport& XdImport::get_report() {
  return *m_report;
}

XdStates& XdImport::get_states() {
  return m_states;
}

std::mutex& XdImport::get_mutex() {
  return m_main->m_mutex;
}

XdObject& XdImport::get_resources() {
  return *m_main->m_resources;
}

XdObject& XdImport::get_pasteboard() {
  return *m_main->m_pasteboard;
}

const std::vector<std::unique_ptr<XdObject>>& XdImport::get_artboards() {
  return m_main->m_artboards;
}

XdObject& XdImport::get_interactions() {
  return *m_main->m_interactions;
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
    node.m_header.m_id = insert(node);
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
  auto record = XdNodeRecord(node.m_header.m_id, json.get_json(),
    std::vector<std::pair<NodeId, QJsonObject>>());
  if(node.m_children) {
    auto pending = std::vector<QJsonObject>();
    for(auto child : node.m_children->get_json()) {
      if(child.isObject()) {
        auto object = child.toObject();
        auto child_json = make_node_json(object);
        auto id = import_node(*child_json, &node, XdNodeRole::NODE);
        if(id.is_nil()) {
          pending.push_back(std::move(object));
        } else {
          for(auto& orphan : pending) {
            record.m_orphans.emplace_back(id, std::move(orphan));
          }
          pending.clear();
        }
      }
    }
    for(auto& orphan : pending) {
      record.m_orphans.emplace_back(NodeId(), std::move(orphan));
    }
  }
  for(auto& finisher : m_importer->m_finishers) {
    finisher(node);
  }
  m_records.push_back(std::move(record));
  return node.m_header.m_id;
}

std::vector<NodeId> XdImport::import_nodes(
    const std::vector<QJsonObject>& nodes, XdNodeRole role,
    std::string_view path) {
  auto ids = std::vector<NodeId>(nodes.size());
  if(m_batch || nodes.size() < 2) {
    for(auto i = std::size_t(0); i != nodes.size(); ++i) {
      auto json = make_root(nodes[i], path);
      ids[i] = import_node(*json, nullptr, role);
    }
    return ids;
  }
  auto batches = std::vector<std::unique_ptr<XdImport>>(nodes.size());
  parallel_for(nodes.size(), [&] (std::size_t index) {
    auto batch = std::unique_ptr<XdImport>(new XdImport(*this));
    {
      auto json = batch->make_root(nodes[index], path);
      ids[index] = batch->import_node(*json, nullptr, role);
    }
    batches[index] = std::move(batch);
  });
  for(auto i = std::size_t(0); i != nodes.size(); ++i) {
    if(!merge(*batches[i])) {
      auto json = make_root(nodes[i], path);
      ids[i] = import_node(*json, nullptr, role);
    }
    batches[i] = nullptr;
  }
  return ids;
}

const Node* XdImport::find_node(const NodeId& id) {
  if(!m_batch) {
    return m_document->find(id);
  }
  if(auto entry = m_batch->m_index.find(id); entry != m_batch->m_index.end()) {
    return &m_batch->m_pending[entry->second].m_node;
  }
  if(auto node = m_document->find(id)) {
    return node;
  }
  m_batch->m_misses.push_back(id);
  return nullptr;
}

void XdImport::replace_payload(const NodeId& id, Payload payload) {
  if(!m_batch) {
    m_transaction->replace_payload(id, std::move(payload));
    return;
  }
  auto entry = m_batch->m_index.find(id);
  if(entry == m_batch->m_index.end()) {
    throw std::logic_error("Node not imported by this parallel import.");
  }
  m_batch->m_pending[entry->second].m_node.m_payload = std::move(payload);
}

const std::vector<XdNodeRecord>& XdImport::get_records() const {
  return m_records;
}

const OpaqueData& XdImport::get_opaque() const {
  return m_opaque;
}

void XdImport::set_opaque(OpaqueData opaque) {
  m_opaque = std::move(opaque);
}

void XdImport::warn(std::string_view message) {
  auto entry = m_report->m_warnings.find(message);
  if(entry == m_report->m_warnings.end()) {
    m_report->m_warnings.emplace(std::string(message), 1);
  } else {
    ++entry->second;
  }
}

std::unique_ptr<XdObject> XdImport::make_root(
    const QJsonObject& json, std::string_view path) {
  if(path.empty()) {
    return make_node_json(json);
  }
  return std::make_unique<XdObject>(json, std::string(path), *m_report);
}

NodeId XdImport::insert(XdNode& node) {
  auto& payload = *node.m_payload;
  if(!m_batch) {
    if(node.m_role == XdNodeRole::NODE) {
      auto parent = node.m_parent ? node.m_parent->m_header.m_id : NodeId();
      return m_transaction->append(parent, node.m_header, std::move(payload));
    }
    return m_transaction->insert_root(node.m_header, std::move(payload));
  }
  auto header = node.m_header;
  if(header.m_id.is_nil()) {
    header.m_id = generate_node_id();
  }
  auto kind = get_kind(payload);
  auto parent = NodeId();
  if(node.m_role != XdNodeRole::NODE) {
    if(kind == NodeKind::ARTBOARD) {
      throw std::invalid_argument("An artboard must be on the canvas");
    }
  } else if(node.m_parent) {
    parent = node.m_parent->m_header.m_id;
  }
  if(!has_valid_runs(payload)) {
    throw std::invalid_argument("Text runs do not cover the content.");
  }
  auto& index = m_batch->m_index;
  auto& pending = m_batch->m_pending;
  auto parent_index = std::optional<std::size_t>();
  if(!parent.is_nil()) {
    auto entry = index.find(parent);
    if(entry == index.end()) {
      throw std::logic_error("Parent not imported by this parallel import.");
    }
    if(!can_contain(pending[entry->second].m_node.m_payload, kind)) {
      throw std::invalid_argument("Parent does not accept the node");
    }
    parent_index = entry->second;
  }
  auto id = header.m_id;
  if(index.contains(id) || m_document->find(id)) {
    throw std::invalid_argument("Duplicate node");
  }
  index.emplace(id, pending.size());
  pending.push_back(Batch::Pending(Node(std::move(header), std::move(payload),
    parent, std::vector<NodeId>()), node.m_role));
  if(parent_index) {
    pending[*parent_index].m_node.m_children.push_back(id);
  }
  return id;
}

bool XdImport::merge(XdImport& batch) {
  auto& data = *batch.m_batch;
  for(auto& pending : data.m_pending) {
    if(m_document->find(pending.m_node.get_id())) {
      return false;
    }
  }
  for(auto& miss : data.m_misses) {
    if(m_document->find(miss)) {
      return false;
    }
  }
  for(auto& pending : data.m_pending) {
    auto& node = pending.m_node;
    if(pending.m_role == XdNodeRole::NODE) {
      m_transaction->append(
        node.m_parent, std::move(node.m_header), std::move(node.m_payload));
    } else {
      m_transaction->insert_root(
        std::move(node.m_header), std::move(node.m_payload));
    }
  }
  m_records.insert(m_records.end(), std::make_move_iterator(
    batch.m_records.begin()), std::make_move_iterator(batch.m_records.end()));
  auto& report = data.m_report;
  add_counts(report.m_unconsumed, m_report->m_unconsumed);
  add_counts(report.m_warnings, m_report->m_warnings);
  add_counts(report.m_preserved, m_report->m_preserved);
  m_report->m_node_count += report.m_node_count;
  m_states.merge(std::move(batch.m_states));
  return true;
}

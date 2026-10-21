#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <QFileInfo>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Io/XdMetadata.hpp"
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdValue.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  std::string read_manifest(const Document& document, std::string_view key) {
    auto data = decode_package_data(document.get_opaque().get_bytes());
    if(auto manifest = parse_xd_value(data.m_manifest)) {
      if(auto value = manifest->find(key)) {
        return std::string(value->to_string());
      }
    }
    return std::string();
  }

  std::string read_metadata(const Document& document) {
    auto data = decode_package_data(document.get_opaque().get_bytes());
    for(auto& part : data.m_parts) {
      if(part.first == "META-INF/metadata.xml") {
        return part.second;
      }
    }
    return std::string();
  }
}

DocumentSession::DocumentSession(
  Document& document, QString path, XdTextLayout layout)
  : m_document(&document),
    m_path(std::move(path)),
    m_id(read_manifest(document, "uxdesign#id")),
    m_parent(read_manifest(document, "uxdesign#parentDocId")),
    m_composite(read_manifest(document, "id")),
    m_metadata(read_metadata(document)),
    m_layout(std::move(layout)),
    m_editor(document) {
  if(m_id.empty()) {
    m_id = to_string(generate_node_id());
  }
  m_editor.get_history().set_compound_filter([] (const Node& compound) {
    return decode_node_data(compound.m_header.m_opaque.get_bytes()).
      m_orphans.empty();
  });
}

Document& DocumentSession::get_document() const {
  return *m_document;
}

Editor& DocumentSession::get_editor() {
  return m_editor;
}

const QString& DocumentSession::get_path() const {
  return m_path;
}

QString DocumentSession::get_name() const {
  if(m_path.isEmpty()) {
    return translate("Untitled");
  }
  return QFileInfo(m_path).completeBaseName();
}

const std::string& DocumentSession::get_id() const {
  return m_id;
}

bool DocumentSession::is_dirty() const {
  return m_editor.get_history().is_dirty();
}

void DocumentSession::save(const QString& path) {
  save(path, QDateTime::currentDateTime());
}

void DocumentSession::save(const QString& path, const QDateTime& time) {
  auto options = XdWriteOptions();
  options.m_layout = m_layout;
  options.m_name = QFileInfo(path).completeBaseName().toStdString() +
    '\x01';
  options.m_id = m_id;
  options.m_parent = m_parent;
  options.m_composite = m_composite;
  options.m_serialization = to_string(generate_node_id());
  options.m_time = time;
  options.m_instance = to_string(generate_node_id());
  options.m_metadata = m_metadata;
  if(m_path.isEmpty() || path != m_path || !QFileInfo::exists(path)) {
    options.m_metadata.clear();
    options.m_id = to_string(generate_node_id());
    if(options.m_parent.empty()) {
      options.m_parent = options.m_id;
    }
    options.m_composite = to_string(generate_node_id());
  } else {
    if(options.m_id.empty()) {
      options.m_id = to_string(generate_node_id());
    }
    if(options.m_composite.empty()) {
      options.m_composite = to_string(generate_node_id());
    }
  }
  write_xd(*m_document, std::filesystem::path(path.toStdU16String()), options);
  m_metadata = save_xd_metadata(options.m_metadata, options.m_name,
    options.m_composite, options.m_instance, time);
  m_path = path;
  m_id = std::move(options.m_id);
  m_parent = std::move(options.m_parent);
  m_composite = std::move(options.m_composite);
  m_editor.get_history().mark_clean();
}

bool Rellu::resolve_unsaved_changes(const DocumentSession& session,
    const std::function<UnsavedChoice ()>& prompt,
    const std::function<bool ()>& save) {
  if(!session.is_dirty()) {
    return true;
  }
  auto choice = prompt();
  if(choice == UnsavedChoice::SAVE) {
    return save();
  }
  return choice == UnsavedChoice::DISCARD;
}

std::unique_ptr<Document> Rellu::make_new_document() {
  auto document = std::make_unique<Document>();
  auto preset = ArtboardPreset("Web 1920", Size(1920, 1080));
  {
    auto transaction = Transaction(*document);
    insert_preset_artboard(*document, transaction, preset, Point(0, 0));
    transaction.commit();
  }
  document->clear_history();
  set_last_used_preset(preset);
  return document;
}

bool Rellu::open_launched_document(const QStringList& arguments,
    const std::function<bool (const QString& path)>& open,
    const std::function<bool ()>& make_new) {
  if(arguments.size() > 1) {
    return open(arguments[1]);
  }
  return make_new();
}

QString Rellu::to_xd_path(const QString& path) {
  if(path.isEmpty() ||
      QFileInfo(path).suffix().compare("xd", Qt::CaseInsensitive) == 0) {
    return path;
  }
  return path + ".xd";
}

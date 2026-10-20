#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <QFileInfo>
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPatch.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  std::string read_manifest(const Document& document, std::string_view key) {
    auto data = decode_package_data(document.get_opaque().get_bytes());
    auto encoding = std::string_view(data.m_manifest);
    if(encoding.empty()) {
      return std::string();
    }
    auto manifest = decode_value(encoding);
    if(auto value = manifest.find(key)) {
      return std::string(value->to_string());
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
    m_layout(std::move(layout)),
    m_editor(document) {
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
  auto options = XdWriteOptions();
  options.m_layout = m_layout;
  options.m_name = QFileInfo(path).completeBaseName().toStdString();
  options.m_id = m_id;
  options.m_parent = m_parent;
  if(m_path.isEmpty() || path != m_path || !QFileInfo::exists(path)) {
    options.m_id = to_string(generate_node_id());
    if(options.m_parent.empty()) {
      options.m_parent = options.m_id;
    }
  } else if(options.m_id.empty()) {
    options.m_id = to_string(generate_node_id());
  }
  write_xd(*m_document, std::filesystem::path(path.toStdU16String()), options);
  m_path = path;
  m_id = std::move(options.m_id);
  m_parent = std::move(options.m_parent);
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

QString Rellu::to_xd_path(const QString& path) {
  if(path.isEmpty() ||
      QFileInfo(path).suffix().compare("xd", Qt::CaseInsensitive) == 0) {
    return path;
  }
  return path + ".xd";
}

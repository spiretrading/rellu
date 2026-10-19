#include <filesystem>
#include <utility>
#include <QFileInfo>
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Ui/DocumentSession.hpp"

using namespace Rellu;

DocumentSession::DocumentSession(
  Document& document, QString path, XdTextLayout layout)
  : m_document(&document),
    m_path(std::move(path)),
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
    return "Untitled";
  }
  return QFileInfo(m_path).completeBaseName();
}

bool DocumentSession::is_dirty() const {
  return m_editor.get_history().is_dirty();
}

void DocumentSession::save(const QString& path) {
  auto options = XdWriteOptions();
  options.m_layout = m_layout;
  options.m_name = QFileInfo(path).completeBaseName().toStdString();
  write_xd(*m_document, std::filesystem::path(path.toStdU16String()), options);
  m_path = path;
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

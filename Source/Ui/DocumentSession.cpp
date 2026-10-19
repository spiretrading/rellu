#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <QFileInfo>
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdPatch.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  const auto ID_KEY = std::string_view("uxdesign#id");

  std::string read_id(const Document& document) {
    auto data = decode_package_data(document.get_opaque().get_bytes());
    auto encoding = std::string_view(data.m_manifest);
    if(encoding.empty()) {
      return std::string();
    }
    auto manifest = decode_value(encoding);
    if(auto id = manifest.find(ID_KEY)) {
      return std::string(id->to_string());
    }
    return std::string();
  }

  std::string read_id(const QString& path) {
    try {
      auto package = XdPackage(std::filesystem::path(path.toStdU16String()));
      return package.get_manifest().value(QString::fromUtf8(
        ID_KEY.data(), static_cast<qsizetype>(ID_KEY.size()))).toString().
          toStdString();
    } catch(const std::exception&) {
      return std::string();
    }
  }
}

DocumentSession::DocumentSession(
  Document& document, QString path, XdTextLayout layout)
  : m_document(&document),
    m_path(std::move(path)),
    m_id(read_id(document)),
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
  write_xd(*m_document, std::filesystem::path(path.toStdU16String()), options);
  m_path = path;
  m_id = read_id(path);
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

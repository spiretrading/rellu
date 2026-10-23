#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <QFileInfo>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Io/XdMetadata.hpp"
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPreview.hpp"
#include "Rellu/Io/XdValue.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  constexpr auto LOAD_ERROR_CODE = 47;

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
    m_instance(to_string(generate_node_id())),
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

const std::string& DocumentSession::get_instance() const {
  return m_instance;
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
  options.m_previews = render_xd_previews(
    *m_document, make_painted_text_measurer(get_default_font_library()));
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

XdAlertContent Rellu::make_unsaved_alert(const QString& name) {
  auto content = XdAlertContent();
  content.m_message = translate("dialog",
    "Do you want to save the changes made to the document \"%1\"?").arg(
      name);
  content.m_explanation = translate("dialog",
    "Your changes will be lost if you don't save them.");
  content.m_buttons = {translate("dialog", "Save"),
    translate("dialog", "Don't Save"), translate("dialog", "Cancel")};
  content.m_default = 0;
  content.m_escape = 2;
  content.m_sign = XdAlertSign::WARNING;
  content.m_is_fitted = true;
  return content;
}

XdAlertContent Rellu::make_open_document_alert(const QString& path) {
  auto content = XdAlertContent();
  content.m_message = translate("dialog",
    "Could not save as \"%1\" because that document is currently open.").
      arg(QFileInfo(path).fileName());
  content.m_explanation = translate("dialog",
    "Save using a different name, or close the document and try again.");
  content.m_buttons = {translate("dialog", "OK")};
  content.m_is_fitted = true;
  return content;
}

XdAlertContent Rellu::make_load_failure_alert(const QString& path) {
  auto content = XdAlertContent();
  content.m_message = translate("dialog",
    "There was an error opening the file %1\n(Error code: %2)").arg(
      QFileInfo(path).fileName()).arg(LOAD_ERROR_CODE);
  content.m_buttons = {translate("dialog", "OK")};
  content.m_is_fitted = true;
  return content;
}

std::unique_ptr<Document> Rellu::make_new_document() {
  return make_new_document(ArtboardPreset("Web 1920", Size(1920, 1080)));
}

std::unique_ptr<Document> Rellu::make_new_document(
    const ArtboardPreset& preset) {
  auto document = std::make_unique<Document>();
  if(preset.m_size.is_empty()) {
    return document;
  }
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

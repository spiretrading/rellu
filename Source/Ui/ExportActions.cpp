#include <utility>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include "Rellu/Ui/ExportActions.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  const auto BATCH = std::string_view("file.export.batch");
  const auto SELECTED = std::string_view("file.export.selected");
  const auto ALL_ARTBOARDS = std::string_view("file.export.all");

  QString get_failure_link() {
    return translate("export", QString::fromUtf16(u"We\u2019d love to "
      u"understand what went wrong so that we can fix this for you. Please "
      u"tell us about the problem [here|http://adobe.com/go/"
      u"xd_export_errors]."));
  }

  QString get_permission_link() {
    return translate("export", "If you continue to have problems, please "
      "report them to us in [our forums|http://www.adobe.com/go/xd_forums].");
  }
}

ExportActions::ExportActions(ActionRegistry& actions, QWidget& window)
    : m_actions(&actions),
      m_window(&window),
      m_editor(nullptr),
      m_measurer(estimate_text_bounds) {
  m_dialog_handler = [] (auto& dialog) {
    dialog.exec();
  };
  m_alert_handler = [this] (const auto& content) {
    return show_xd_alert(content, m_window);
  };
  if(actions.find(BATCH)) {
    actions.set_handler(BATCH, [this] {
      if(m_editor) {
        export_assets(
          get_batch_export_assets(m_editor->get_document()), false);
      }
    });
  }
  if(actions.find(SELECTED)) {
    actions.set_handler(SELECTED, [this] {
      if(m_editor) {
        export_nodes(m_editor->get_selection().get_nodes(), false);
      }
    });
  }
  if(actions.find(ALL_ARTBOARDS)) {
    actions.set_handler(ALL_ARTBOARDS, [this] {
      if(m_editor) {
        export_nodes(get_export_artboards(m_editor->get_document()), true);
      }
    });
  }
  update();
}

ExportActions::~ExportActions() = default;

Editor* ExportActions::get_editor() const {
  return m_editor;
}

void ExportActions::set_editor(Editor* editor) {
  m_selection_subscription.release();
  m_history_subscription.release();
  m_editor = editor;
  if(m_editor) {
    m_selection_subscription = m_editor->get_selection().subscribe([this] {
      update();
    });
    m_history_subscription = m_editor->get_history().subscribe([this] {
      update();
    });
  }
  update();
}

void ExportActions::set_text_measurer(TextMeasurer measurer) {
  m_measurer = std::move(measurer);
}

void ExportActions::set_path_provider(PathProvider provider) {
  m_path_provider = std::move(provider);
}

void ExportActions::set_dialog_handler(DialogHandler handler) {
  m_dialog_handler = std::move(handler);
}

void ExportActions::set_alert_handler(AlertHandler handler) {
  m_alert_handler = std::move(handler);
}

void ExportActions::set_settings_handler(std::function<void ()> handler) {
  m_settings_handler = std::move(handler);
}

void ExportActions::set_settings(const ExportOptions& options,
    const QString& scale_text, const QString& folder) {
  m_options = options;
  m_scale_text = scale_text;
  m_folder = folder;
}

const ExportOptions& ExportActions::get_options() const {
  return m_options;
}

const QString& ExportActions::get_scale_text() const {
  return m_scale_text;
}

const QString& ExportActions::get_folder() const {
  return m_folder;
}

void ExportActions::export_nodes(
    const std::vector<NodeId>& nodes, bool is_all_artboards) {
  export_assets(to_export_assets(nodes), is_all_artboards);
}

void ExportActions::export_assets(
    const std::vector<ExportAsset>& assets, bool is_all_artboards) {
  if(!m_editor || assets.empty()) {
    return;
  }
  auto dialog = ExportDialog(m_options, m_scale_text, m_folder,
    is_all_artboards, static_cast<int>(assets.size()), m_window);
  dialog.set_export_handler([&] {
    run(dialog, assets);
  });
  m_dialog_handler(dialog);
}

void ExportActions::update() {
  auto document = m_editor ? &m_editor->get_document() : nullptr;
  if(m_actions->find(BATCH)) {
    m_actions->set_enabled(BATCH,
      document && has_batch_export_nodes(*document));
  }
  if(m_actions->find(SELECTED)) {
    m_actions->set_enabled(SELECTED,
      m_editor && !m_editor->get_selection().is_empty());
  }
  if(m_actions->find(ALL_ARTBOARDS)) {
    m_actions->set_enabled(ALL_ARTBOARDS,
      document && !get_export_artboards(*document).empty());
  }
}

void ExportActions::run(
    ExportDialog& dialog, const std::vector<ExportAsset>& assets) {
  m_options = dialog.get_options();
  m_scale_text = dialog.get_scale_text();
  m_folder = dialog.get_folder();
  if(m_settings_handler) {
    m_settings_handler();
  }
  auto& document = m_editor->get_document();
  auto name = QString();
  if(m_path_provider) {
    if(auto path = m_path_provider(); !path.isEmpty()) {
      name = QFileInfo(path).completeBaseName();
    }
  }
  auto tasks = make_export_tasks(
    document, assets, m_options, m_folder, name, m_measurer);
  if(tasks.empty()) {
    dialog.accept();
    m_alert_handler(make_failure_alert(
      ExportFailure::GENERIC, static_cast<int>(assets.size()), {}));
    return;
  }
  dialog.set_exporting(true);
  if(auto existing = find_existing_files(tasks); !existing.empty() &&
      m_alert_handler(make_overwrite_alert(existing, m_folder)) != 0) {
    dialog.set_exporting(false);
    return;
  }
  auto result = run_export(document, tasks, m_options, m_measurer,
    [&] (int done, int total) {
      dialog.set_progress(done, total);
      QCoreApplication::processEvents();
      return !dialog.is_cancel_requested();
    });
  dialog.set_exporting(false);
  if(result.m_is_cancelled) {
    return;
  }
  dialog.accept();
  report(result);
}

void ExportActions::report(const ExportResult& result) {
  if(result.m_failed_nodes.empty()) {
    return;
  }
  m_alert_handler(make_failure_alert(result.m_failure,
    static_cast<int>(result.m_failed_nodes.size()), result.m_failed_paths));
}

XdAlertContent Rellu::make_overwrite_alert(
    const std::vector<QString>& names, const QString& folder) {
  auto content = XdAlertContent();
  auto folder_name = QFileInfo(QDir::cleanPath(folder)).fileName();
  if(names.size() == 1) {
    content.m_message = translate("export",
      "\"%1\" already exists. Do you want to replace it?").arg(names[0]);
    content.m_explanation = translate("export", "A file with the same name "
      "already exists in the folder \"%1\". Replacing it will overwrite its "
      "current contents.").arg(folder_name);
  } else {
    if(names.size() == 2) {
      content.m_message = translate("export", "\"%1\" and \"%2\" already "
        "exist. Do you want to replace them?").arg(names[0], names[1]);
    } else {
      content.m_message = translate("export", "\"%1\" and %2 other files "
        "already exist. Do you want to replace them?").arg(names[0]).arg(
          static_cast<int>(names.size()) - 1);
    }
    content.m_explanation = translate("export", "Files with the same name "
      "already exist in the folder \"%1\". Replacing them will overwrite "
      "their current contents.").arg(folder_name);
  }
  content.m_buttons = {translate("export", "Replace"),
    translate("export", "Cancel")};
  content.m_default = 1;
  return content;
}

XdAlertContent Rellu::make_failure_alert(ExportFailure failure, int count,
    const std::vector<QString>& paths) {
  auto content = XdAlertContent();
  if(failure == ExportFailure::READ_ONLY ||
      failure == ExportFailure::ACCESS_DENIED) {
    if(failure == ExportFailure::READ_ONLY) {
      if(count == 1) {
        content.m_message = translate("export",
          "There was a problem overwriting a read-only asset");
      } else {
        content.m_message = translate("export",
          "There was a problem overwriting %1 read-only assets").arg(count);
      }
    } else if(count == 1) {
      content.m_message = translate("export", "There was a problem exporting "
        "an asset. Could not overwrite the asset from that location because "
        "it is in use or you do not have permissions.");
    } else {
      content.m_message = translate("export", "There was a problem exporting "
        "%1 assets. Could not overwrite assets from that location because "
        "they are in use or you do not have permissions.").arg(count);
    }
    if(paths.size() <= 5) {
      auto names = QStringList();
      for(auto& path : paths) {
        names.push_back(QFileInfo(path).fileName());
      }
      content.m_explanation = names.join('\n');
    }
    content.m_link = get_permission_link();
  } else {
    if(count == 1) {
      content.m_message =
        translate("export", "There was a problem exporting your asset.");
    } else {
      content.m_message = translate("export",
        "There was a problem exporting %1 assets.").arg(count);
    }
    content.m_link = get_failure_link();
  }
  content.m_buttons = {translate("export", "OK")};
  content.m_default = 0;
  return content;
}

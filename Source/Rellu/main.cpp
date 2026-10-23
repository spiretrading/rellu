#include <functional>
#include <memory>
#include <utility>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QList>
#include <QMetaObject>
#include <QSettings>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtPlugin>
#include "Rellu/Ui/Clipboard.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/DocumentWindows.hpp"
#include "Rellu/Ui/FeatureCards.hpp"
#include "Rellu/Ui/GreyscaleText.hpp"
#include "Rellu/Ui/PixelHitFilter.hpp"
#include "Rellu/Ui/SingleInstance.hpp"
#include "Rellu/Ui/Theme.hpp"
#include "Rellu/Ui/ToolTips.hpp"
#include "FileTracking.hpp"
#include "MainWindow.hpp"
#include "NativeFrame.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QModernWindowsStylePlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

namespace {
  const auto RECENT_FILES_KEY = QString("recent_files");
  const auto RECENT_IDENTITIES_KEY = QString("recent_file_identities");

  QList<QByteArray> load_identities(const QSettings& settings) {
    auto identities = QList<QByteArray>();
    for(auto& value : settings.value(RECENT_IDENTITIES_KEY).toList()) {
      identities.push_back(value.toByteArray());
    }
    return identities;
  }

  void save_identities(QSettings& settings,
      const QList<QByteArray>& identities) {
    auto values = QVariantList();
    for(auto& identity : identities) {
      values.push_back(identity);
    }
    settings.setValue(RECENT_IDENTITIES_KEY, values);
  }

  QStringList get_launched_paths(const QStringList& arguments) {
    auto paths = QStringList();
    for(auto i = 1; i < arguments.size(); ++i) {
      paths.push_back(QFileInfo(arguments[i]).absoluteFilePath());
    }
    return paths;
  }
}

int main(int argc, char** argv) {
  auto application = QApplication(argc, argv);
  application.setOrganizationName("Rellu");
  application.setApplicationName("Rellu");
  auto instance = SingleInstance(make_instance_key());
  allow_foreground_activation();
  if(instance.forward(get_launched_paths(QApplication::arguments()))) {
    return 0;
  }
  set_language(load_language());
  apply_theme(application);
  application.installEventFilter(new PixelHitFilter(&application));
  application.installEventFilter(new GreyscaleTextFilter(&application));
  application.installEventFilter(new ToolTipFilter(&application));
  application.installEventFilter(new FeatureCardFilter(&application));
  Clipboard::remove_stale_images(Clipboard::get_default_image_directory(),
    QApplication::clipboard()->mimeData());
  QApplication::setQuitOnLastWindowClosed(false);
  auto windows = DocumentWindows();
  windows.set_file_tracking(identify_file, locate_file);
  {
    auto settings = QSettings();
    windows.set_recent_files(settings.value(RECENT_FILES_KEY).toStringList(),
      load_identities(settings));
  }
  auto saved_files = windows.get_recent_files();
  auto saved_identities = windows.get_recent_identities();
  auto recent_files = windows.subscribe([&] {
    if(windows.get_recent_files() != saved_files ||
        windows.get_recent_identities() != saved_identities) {
      saved_files = windows.get_recent_files();
      saved_identities = windows.get_recent_identities();
      auto settings = QSettings();
      settings.setValue(RECENT_FILES_KEY, saved_files);
      save_identities(settings, saved_identities);
    }
  });
  windows.locate_recent_files();
  windows.set_factory([&] (const QString& path, auto document) {
    return std::unique_ptr<DocumentWindow>(
      make_main_window(windows, path, std::move(document)));
  });
  auto empty_handler = std::function<void ()>([] {
    QCoreApplication::quit();
  });
  windows.set_empty_handler(empty_handler);
  instance.listen([&] (const QStringList& paths) {
    if(paths.isEmpty()) {
      if(auto window = windows.get_active()) {
        windows.activate(*window);
      }
    }
    for(auto& path : paths) {
      windows.open(path);
    }
  });
  if(!open_launched_document(QApplication::arguments(),
      [&] (const QString& path) {
        return windows.open(path);
      },
      [&] {
        return windows.make_new();
      }) && windows.get_windows().empty()) {
    QMetaObject::invokeMethod(
      &application, empty_handler, Qt::QueuedConnection);
  }
  auto result = application.exec();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  return result;
}

#include <QApplication>
#include <QStringList>
#include <QtPlugin>
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/Theme.hpp"
#include "MainWindow.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QModernWindowsStylePlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

int main(int argc, char** argv) {
  auto application = QApplication(argc, argv);
  application.setOrganizationName("Rellu");
  application.setApplicationName("Rellu");
  set_language(load_language());
  apply_theme(application);
  auto window = MainWindow();
  window.show();
  open_launched_document(QApplication::arguments(),
    [&] (const QString& path) {
      return window.open(path);
    },
    [&] {
      return window.make_new();
    });
  return application.exec();
}

#include <QApplication>
#include <QStringList>
#include <QtPlugin>
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/Theme.hpp"
#include "MainWindow.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QModernWindowsStylePlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

int main(int argc, char** argv) {
  auto application = QApplication(argc, argv);
  set_default_font_directories({get_bundled_font_directory()});
  application.setOrganizationName("Rellu");
  application.setApplicationName("Rellu");
  apply_theme(application);
  auto window = MainWindow();
  window.show();
  auto arguments = QApplication::arguments();
  if(arguments.size() > 1) {
    window.open(arguments[1]);
  }
  return application.exec();
}

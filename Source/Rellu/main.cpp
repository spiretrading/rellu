#include <QApplication>
#include <QStringList>
#include <QtPlugin>
#include "MainWindow.hpp"

Q_IMPORT_PLUGIN(QModernWindowsStylePlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

int main(int argc, char** argv) {
  auto application = QApplication(argc, argv);
  application.setApplicationName("Rellu");
  auto window = MainWindow();
  window.resize(1280, 800);
  window.show();
  auto arguments = QApplication::arguments();
  if(arguments.size() > 1) {
    window.open(arguments[1]);
  }
  return application.exec();
}

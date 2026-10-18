#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <QGuiApplication>
#include <QtPlugin>

Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

int main(int argc, char** argv) {
  if(!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  auto application = QGuiApplication(argc, argv);
  auto context = doctest::Context(argc, argv);
  return context.run();
}

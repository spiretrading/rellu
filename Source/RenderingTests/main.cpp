#define DOCTEST_CONFIG_IMPLEMENT
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QGuiApplication>
#include <QtPlugin>

Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

int main(int argc, char** argv) {
  auto platform = std::string("-platform");
  auto offscreen = std::string("offscreen");
  auto arguments = std::vector<char*>(argv, argv + argc);
  arguments.insert(arguments.begin() + 1, {platform.data(), offscreen.data()});
  argc = static_cast<int>(arguments.size());
  arguments.push_back(nullptr);
  argv = arguments.data();
  auto application = QGuiApplication(argc, argv);
  auto context = doctest::Context(argc, argv);
  return context.run();
}

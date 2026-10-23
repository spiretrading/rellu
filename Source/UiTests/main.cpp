#define DOCTEST_CONFIG_IMPLEMENT
#include <string>
#include <string_view>
#include <vector>
#include <doctest/doctest.h>
#include <QApplication>
#include <QtPlugin>
#include "Rellu/Testing/FailureDialogs.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "TestOptions.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)

int main(int argc, char** argv) {
  Rellu::Testing::suppress_failure_dialogs();
  auto platform = std::string("-platform");
  auto offscreen = std::string("offscreen");
  auto option = std::string_view("--xd-document=");
  auto arguments = std::vector<char*>();
  for(auto i = 0; i != argc; ++i) {
    auto argument = std::string_view(argv[i]);
    if(argument.starts_with(option)) {
      Rellu::Tests::get_xd_document_path() = argument.substr(option.size());
    } else {
      arguments.push_back(argv[i]);
    }
  }
  arguments.insert(arguments.begin() + 1, {platform.data(), offscreen.data()});
  argc = static_cast<int>(arguments.size());
  arguments.push_back(nullptr);
  argv = arguments.data();
  auto application = QApplication(argc, argv);
  Rellu::set_default_font_directories({RELLU_FONTS_DIRECTORY});
  auto context = doctest::Context(argc, argv);
  return context.run();
}

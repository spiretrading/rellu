#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Rellu/Testing/FailureDialogs.hpp"
#include "Rellu/Text/FontLibrary.hpp"

int main(int argc, char** argv) {
  Rellu::Testing::suppress_failure_dialogs();
  Rellu::set_default_font_directories({RELLU_FONTS_DIRECTORY});
  auto context = doctest::Context(argc, argv);
  return context.run();
}

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Rellu/Text/FontLibrary.hpp"

int main(int argc, char** argv) {
  Rellu::set_default_font_directories({RELLU_FONTS_DIRECTORY});
  auto context = doctest::Context(argc, argv);
  return context.run();
}

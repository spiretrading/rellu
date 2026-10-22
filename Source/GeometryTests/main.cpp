#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Rellu/Testing/FailureDialogs.hpp"

int main(int argc, char** argv) {
  Rellu::Testing::suppress_failure_dialogs();
  auto context = doctest::Context(argc, argv);
  return context.run();
}

#ifndef RELLU_UI_TESTS_TEST_OPTIONS_HPP
#define RELLU_UI_TESTS_TEST_OPTIONS_HPP
#include <string>

namespace Rellu::Tests {

  /**
   * Returns the path of an Adobe XD document given on the command line for
   * the tests to round trip, or empty if none was given.
   */
  inline std::string& get_xd_document_path() {
    static auto path = std::string();
    return path;
  }
}

#endif

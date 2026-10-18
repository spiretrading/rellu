#ifndef RELLU_FORMAT_FORMAT_ERROR_HPP
#define RELLU_FORMAT_FORMAT_ERROR_HPP
#include <stdexcept>

namespace Rellu {

  /**
   * Signals that a Rellu document file cannot be read or written, is of an
   * unsupported version, or is truncated or corrupt.
   */
  class FormatError : public std::runtime_error {
    public:
      using std::runtime_error::runtime_error;
  };
}

#endif

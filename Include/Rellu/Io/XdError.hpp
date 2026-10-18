#ifndef RELLU_IO_XD_ERROR_HPP
#define RELLU_IO_XD_ERROR_HPP
#include <stdexcept>

namespace Rellu {

  /**
   * Signals that an Adobe XD package cannot be read, is missing a required
   * entry or holds a corrupt entry.
   */
  class XdError : public std::runtime_error {
    public:
      using std::runtime_error::runtime_error;
  };
}

#endif

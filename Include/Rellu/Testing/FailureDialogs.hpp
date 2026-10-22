#ifndef RELLU_TESTING_FAILURE_DIALOGS_HPP
#define RELLU_TESTING_FAILURE_DIALOGS_HPP
#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(
  unsigned int mode);
#endif

namespace Rellu::Testing {

  /**
   * Makes aborts, failed assertions and crashes of a test program report on
   * the standard error stream and fail the run, never in a dialog, on
   * Windows.
   */
  inline void suppress_failure_dialogs() {
#ifdef _WIN32
    constexpr auto FAIL_CRITICAL_ERRORS = 0x0001u;
    constexpr auto NO_FAULT_ERROR_BOX = 0x0002u;
    constexpr auto NO_OPEN_FILE_ERROR_BOX = 0x8000u;
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for(auto type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
      _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    SetErrorMode(
      FAIL_CRITICAL_ERRORS | NO_FAULT_ERROR_BOX | NO_OPEN_FILE_ERROR_BOX);
#endif
  }
}

#endif

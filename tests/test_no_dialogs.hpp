#pragma once

// Prevents Windows CRT assert/abort failures from popping modal dialogs
// (Abort/Retry/Ignore) that hang unattended test runs. Failures go to
// stderr and exit nonzero instead. Call at the top of every test main().

#if defined(_WIN32)
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace pulse {
namespace test {

inline void disable_error_dialogs() {
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
}

}  // namespace test
}  // namespace pulse

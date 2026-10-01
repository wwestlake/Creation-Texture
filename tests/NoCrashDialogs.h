#pragma once

// Test executables only: a failed assert() or abort() inside the process (including the FRust compiler, which runs
// in-process) prints to stderr and exits, instead of raising a blocking Windows dialog on the user's desktop.
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>

inline void disableCrashDialogs()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
}

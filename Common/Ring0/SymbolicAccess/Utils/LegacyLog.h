#pragma once

// Compatibility shim for legacy breakpoint code.  The old implementation
// called outToFile(), but that symbol was never provided by the checked-in
// sources.  Route it through the unified symbolic-access logger so failures
// are visible in the kernel debugger stream using UDBG-UTF8/1.
#include "Log.h"

#ifdef DEBUG
#define outToFile(...) symbolic_access::LogPrint(LOG_TYPE_ERROR, __VA_ARGS__)
#else
#define outToFile(...)
#endif

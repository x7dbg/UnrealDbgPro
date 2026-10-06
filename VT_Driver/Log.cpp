#include "Driver.h"
#include <stdarg.h>

// VT_Driver does not expose a user-mode device.  Keep its diagnostics on the
// kernel debugger stream so a failed VMX/VMM initialization is still visible
// even before DbgkSysWin11 can create \\.\\UnrealDbg.
namespace symbolic_access
{
    void LogPrint(__log_type type, const char* fmt, ...)
    {
        const ULONG level = type == LOG_TYPE_ERROR ? DPFLTR_ERROR_LEVEL :
            (type == LOG_TYPE_INFO ? DPFLTR_INFO_LEVEL : DPFLTR_TRACE_LEVEL);
        va_list args;
        va_start(args, fmt);
        const char* prefix = type == LOG_TYPE_ERROR ? "[UDBG-UTF8/1][VT_Driver][错误] " :
            (type == LOG_TYPE_INFO ? "[UDBG-UTF8/1][VT_Driver][信息] " : "[UDBG-UTF8/1][VT_Driver][调试] ");
        // Keep formatting in the kernel debugger implementation.  Calling
        // CRT-backed sprintf from a kernel driver causes __stdio_common_* link
        // failures and is not safe in a WDK build.
        vDbgPrintExWithPrefix(prefix, DPFLTR_IHVDRIVER_ID, level, fmt, args);
        va_end(args);
    }

    void PrintToDebugger(std::string_view format, ...)
    {
        va_list args;
        va_start(args, format);
        vDbgPrintExWithPrefix("[UDBG-UTF8/1][VT_Driver][符号] ", DPFLTR_IHVDRIVER_ID,
            DPFLTR_TRACE_LEVEL, format.data(), args);
        va_end(args);
    }
}

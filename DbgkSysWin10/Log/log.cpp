#include "../Driver.h"
#include "log.h"

namespace symbolic_access
{
    void LogPrint(__log_type type, const char* fmt, ...)
    {
        const char* level = "调试";
        ULONG dbgLevel = DPFLTR_TRACE_LEVEL;
        switch (type)
        {
        case LOG_TYPE_ERROR:
            level = "错误";
            dbgLevel = DPFLTR_ERROR_LEVEL;
            break;
        case LOG_TYPE_INFO:
            level = "信息";
            dbgLevel = DPFLTR_INFO_LEVEL;
            break;
        case LOG_TYPE_DUMP:
            level = "转储";
            dbgLevel = DPFLTR_TRACE_LEVEL;
            break;
        default:
            break;
        }

        CHAR message[512] = { 0 };
        va_list args;
        va_start(args, fmt);
        NTSTATUS formatStatus = RtlStringCchVPrintfA(message, RTL_NUMBER_OF(message), fmt, args);
        va_end(args);
        if (!NT_SUCCESS(formatStatus))
        {
            RtlStringCchCopyA(message, RTL_NUMBER_OF(message), "<驱动日志格式化失败>");
        }

        LARGE_INTEGER systemTime = {};
        TIME_FIELDS timeFields = {};
        KeQuerySystemTime(&systemTime);
        ExSystemTimeToLocalTime(&systemTime, &systemTime);
        RtlTimeToTimeFields(&systemTime, &timeFields);

        // UDBG-UTF8/1 is a line-oriented protocol: the payload after the
        // level tag is UTF-8 and can be consumed by DebugView/ETW collectors.
        // It intentionally never writes a privileged kernel log file.
        CHAR output[768] = { 0 };
        RtlStringCchPrintfA(output, RTL_NUMBER_OF(output),
            "[UDBG-UTF8/1][DbgkSysWin10][%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu][%s] %s\n",
            timeFields.Year, timeFields.Month, timeFields.Day,
            timeFields.Hour, timeFields.Minute, timeFields.Second,
            timeFields.Milliseconds, level, message);

        DbgPrintEx(DPFLTR_IHVDRIVER_ID, dbgLevel, "%s", output);
    }
}

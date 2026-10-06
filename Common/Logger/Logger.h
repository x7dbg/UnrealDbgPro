#pragma once

#ifndef _LOGGER_H
#define _LOGGER_H

#include <Windows.h>
#include <tchar.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <tuple>
#include <mutex>
#include "../StringHandler/StringHandler.h"
#include "../FileSystem/FileSystem.h"


class Logger {
public:
    Logger(const std::string& filename) : filename(filename), sessionId(MakeSessionId())
    {
        // 日志统一放到运行目录的 Log 子目录，避免污染程序根目录；目录不存在时自动创建。
        // 汇总日志供界面实时读取；同目录的 Sessions 子目录则固化某个 DLL
        // 进程生命周期的完整记录，便于把多次测试的证据严格分开。
        const std::string directory = ParentDirectory(filename);
        if (!directory.empty())
        {
            (void)::CreateDirectoryA(directory.c_str(), nullptr);
        }
        const std::string sessionsDirectory = directory.empty() ? "Sessions" : directory + "\\Sessions";
        (void)::CreateDirectoryA(sessionsDirectory.c_str(), nullptr);
        if (ShouldRotateLog(filename, 0))
        {
            (void)RotateLog(filename);
        }
        sessionFilename = sessionsDirectory + "\\" + BaseLogName(filename) + "-" + sessionId + ".log";

        logFile.open(filename, std::ios::out | std::ios::app | std::ios::binary);
        if (!logFile.is_open())
        {
            // 日志文件打不开时也使用 Unicode 弹窗，避免中文标题被 ANSI
            // 代码页破坏，并把可操作的错误码/系统原文直接告知用户。
            DWORD error = GetLastError();
            if (error == ERROR_SUCCESS)
            {
                error = ERROR_ACCESS_DENIED;
            }
            LPWSTR systemText = nullptr;
            const DWORD systemLength = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&systemText), 0, nullptr);
            std::wstring systemMessage = systemLength != 0 && systemText != nullptr
                ? std::wstring(systemText, systemLength) : L"系统未提供错误文本";
            if (systemText != nullptr)
            {
                LocalFree(systemText);
            }
            while (!systemMessage.empty() && (systemMessage.back() == L'\r' || systemMessage.back() == L'\n' ||
                systemMessage.back() == L' '))
            {
                systemMessage.pop_back();
            }
            const std::wstring title(filename.begin(), filename.end());
            std::wstringstream details;
            details << L"无法打开日志文件，请确认程序目录可写。\r\n\r\n"
                << L"错误码：" << error << L" (0x" << std::hex << std::uppercase << std::setw(8)
                << std::setfill(L'0') << error << std::dec << L")\r\n"
                << L"系统原文：" << systemMessage << L"\r\n"
                << L"原因：日志路径不存在、目录不可写、文件被占用或被安全软件拦截。\r\n"
                << L"解决方案：检查程序目录和 Log 子目录权限，关闭占用日志文件的程序后重试。";
            ::MessageBoxW(NULL, details.str().c_str(), title.c_str(), MB_ICONWARNING | MB_SYSTEMMODAL);
        }

        // 单会话日志失败时静默退化到汇总日志，不能因诊断能力本身造成
        // 后端 DLL 初始化失败或额外弹窗。
        sessionLogFile.open(sessionFilename, std::ios::out | std::ios::app | std::ios::binary);
        if (sessionLogFile.is_open())
        {
            const std::string header = "=== UnrealDbg 后端开发诊断会话开始 ===\r\n"
                "会话=" + sessionId + "\r\n"
                "进程PID=" + std::to_string(::GetCurrentProcessId()) + "\r\n"
                "编码=UTF-8\r\n\r\n";
            sessionLogFile.write(header.data(), static_cast<std::streamsize>(header.size()));
            sessionLogFile.flush();
            sessionLogBytes = static_cast<std::uint64_t>(header.size());
        }
    }

    Logger()
    {

    }

    ~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
        if (sessionLogFile.is_open()) {
            sessionLogFile.close();
        }
    }

    void _outDebug(TCHAR* sText)
    {
        // 线程同步：使用互斥锁保护临界区
        std::lock_guard<std::mutex> lock(mutex);
        TCHAR szBuf[1024] = { 0 };

        if (m_modName.empty())
        {
            m_modName = LingoLab::stringToWideString(FileSystem::GetSelfModuleName());
        }

        wcscat(szBuf, _T("["));
        wcscat(szBuf, m_modName.c_str());
        wcscat(szBuf, _T("] "));
        wcscat(szBuf, sText);
        //logger.Log(Common::wideStringToString(sText));
        OutputDebugString(szBuf);
        OutputDebugString(_T("\n"));
    }

    int outDebug(const TCHAR* _Format, ...)
    {
        if (_Format == nullptr)
        {
            LogError("outDebug 收到空格式字符串", ERROR_INVALID_PARAMETER);
            return -1;
        }

        TCHAR szBuf[1024] = { 0 };
        va_list list;
        va_start(list, _Format);
#ifdef _UNICODE
        int iRet = _vsnwprintf_s(szBuf, _countof(szBuf), _TRUNCATE, _Format, list);
#else
        int iRet = _vsnprintf_s(szBuf, _countof(szBuf), _TRUNCATE, _Format, list);
#endif
        va_end(list);

        if (iRet < 0)
        {
            _tcscpy_s(szBuf, _countof(szBuf), _T("<日志格式化失败或消息过长>"));
            Log("[ERROR] 调试日志格式化失败或内容被截断");
        }

        _outDebug(szBuf);
#ifdef _UNICODE
        LogW(L"[DEBUG] %ls", szBuf);
#else
        Log("[DEBUG] %s", szBuf);
#endif
        return iRet;
    }

    // Wide format strings are required for paths and other Chinese text.
    // Formatting them through the narrow vsnprintf_s path can fail under the
    // process locale and silently hide the actual symbol/driver diagnostics.
    void LogW(const wchar_t* format, ...)
    {
        if (format == nullptr)
        {
            Log("[ERROR] LogW 收到空格式字符串");
            return;
        }

        wchar_t message[4096] = { 0 };
        va_list args;
        va_start(args, format);
        const int formatted = _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
        va_end(args);
        if (formatted < 0)
        {
            WriteFormattedMessage("<日志格式化失败或消息过长>");
            return;
        }

        const std::string utf8 = WideToUtf8(message);
        WriteFormattedMessage(utf8.empty() && message[0] != L'\0' ? "<日志 UTF-8 转换失败>" : utf8);
    }

    // 将 Win32 错误码转换为可读的系统错误说明。
    static std::string Win32ErrorMessage(DWORD error)
    {
        switch (error)
        {
        case ERROR_SUCCESS: return "操作成功";
        case ERROR_FILE_NOT_FOUND: return "找不到文件，请检查部署目录和文件名";
        case ERROR_PATH_NOT_FOUND: return "找不到路径，请检查目录和配置项";
        case ERROR_ACCESS_DENIED: return "访问被拒绝，请使用管理员权限并检查安全软件";
        case ERROR_INVALID_HANDLE: return "句柄无效或已关闭，请先完成初始化";
        case ERROR_NOT_ENOUGH_MEMORY: return "内存不足，请关闭无关程序后重试";
        case ERROR_GEN_FAILURE: return "设备未正常工作，通常是驱动未启动或设备初始化失败";
        case ERROR_SHARING_VIOLATION: return "文件正被其他进程占用，请关闭占用程序";
        case ERROR_INVALID_PARAMETER: return "参数无效，请检查 PID、路径、地址和缓冲区长度";
        case ERROR_INSUFFICIENT_BUFFER: return "输出缓冲区不足，请按所需长度重新调用";
        case ERROR_MOD_NOT_FOUND: return "找不到 DLL 或模块，请检查文件是否存在及位数是否匹配";
        case ERROR_DLL_NOT_FOUND: return "找不到所需 DLL，请检查部署目录、依赖项和程序位数是否匹配";
        case ERROR_PROC_NOT_FOUND: return "DLL 缺少所需导出函数，请部署匹配版本";
        case ERROR_BAD_EXE_FORMAT: return "可执行文件或 DLL 格式无效，通常是 32/64 位不匹配或文件损坏";
        case ERROR_INVALID_IMAGE_HASH: return "Windows 拒绝加载驱动映像，通常是驱动签名无效或测试证书不受信任";
        case ERROR_FILENAME_EXCED_RANGE: return "文件路径或名称超过 Windows 长度限制，请缩短路径后重试";
        case ERROR_NOT_ALL_ASSIGNED: return "未分配 SeDebugPrivilege，请以管理员权限重新启动";
        case ERROR_PARTIAL_COPY: return "只完成部分读写，目标进程可能已退出或地址无效";
        case ERROR_INVALID_ADDRESS: return "内存地址无效，请重新枚举模块并校验偏移";
        case ERROR_NOACCESS: return "内存不可访问，请确认目标进程和句柄权限";
        case ERROR_DLL_INIT_FAILED: return "DLL 初始化失败，请检查依赖项和运行库";
        case ERROR_NOT_FOUND: return "找不到请求的设备、进程、模块或配置对象";
        case ERROR_CANCELLED: return "操作已被取消，请重新执行";
        case ERROR_ACCESS_DISABLED_BY_POLICY: return "操作被组策略或安全软件阻止";
        case ERROR_DRIVER_BLOCKED: return "驱动被 Windows 安全策略或签名要求阻止";
        case ERROR_INVALID_WINDOW_HANDLE: return "窗口句柄无效，窗口可能已销毁";
        case ERROR_NOT_ENOUGH_QUOTA: return "系统配额不足，请释放资源或增加虚拟内存";
        case ERROR_SERVICE_NOT_ACTIVE: return "目标 Windows 服务未运行，请启动对应服务后重试";
        case ERROR_SERVICE_DISABLED: return "目标 Windows 服务已禁用，请在服务管理器中启用后重试";
        case ERROR_SERVICE_EXISTS: return "目标 Windows 服务已存在；需要确认现有服务状态，运行中的服务可复用，已停止的服务必须先排查其退出原因";
        case ERROR_SERVICE_ALREADY_RUNNING: return "目标 Windows 服务已经运行，无需重复启动";
        case ERROR_SERVICE_MARKED_FOR_DELETE: return "目标 Windows 服务已标记为删除，请重启后再安装或启动";
        case ERROR_SERVICE_SPECIFIC_ERROR: return "服务报告了专用退出错误；应结合服务状态中的具体退出码继续排查";
        default:
        {
            std::ostringstream unknown;
            unknown << "未收录的 Windows 错误，请根据错误码排查 (0x" << std::hex << std::uppercase << error << ")";
            return unknown.str();
        }
        }
    }

    // 与错误原因一一对应的处理建议。后端 DLL 的日志不应只打印一个
    // 泛化的“检查权限和路径”，否则 577、183 等关键错误仍无法修复。
    static std::string Win32ErrorSolution(DWORD error)
    {
        switch (error)
        {
        case ERROR_SUCCESS: return "无需处理";
        case ERROR_FILE_NOT_FOUND: return "确认驱动、DLL、PDB 和配置文件已部署到日志中显示的目录，并检查文件名大小写和扩展名";
        case ERROR_PATH_NOT_FOUND: return "检查程序目录、驱动目录和配置项，创建缺失目录后重新初始化";
        case ERROR_ACCESS_DENIED: return "以管理员身份运行，检查文件/设备权限和安全软件拦截记录";
        case ERROR_INVALID_HANDLE: return "重新执行初始化以获取有效句柄，避免在失败或卸载后继续使用旧句柄";
        case ERROR_NOT_ENOUGH_MEMORY: return "关闭无关程序、释放内存并重新启动；同时检查是否存在异常的大块分配";
        case ERROR_GEN_FAILURE: return "确认驱动服务已启动、设备符号链接存在且 Windows 版本匹配，并查看服务退出码和驱动日志";
        case ERROR_SHARING_VIOLATION: return "关闭占用文件的程序或服务，确认日志/驱动文件可独占访问后重试";
        case ERROR_INVALID_PARAMETER: return "检查路径、PID、地址、长度和配置参数，修正无效值后重试";
        case ERROR_INVALID_DATA: return "检查配置、PDB/符号表、驱动与 DLL 版本是否配套；重新部署损坏文件后重试";
        case ERROR_DUPLICATE_TAG: return "使用现有条目或先删除重复配置，再重新保存";
        case ERROR_ARITHMETIC_OVERFLOW: return "校验模块基址、偏移和位数，确保地址/长度计算不溢出";
        case ERROR_UNHANDLED_EXCEPTION: return "查看同一时间点的组件日志和异常文本，确认 DLL、驱动及目标进程版本匹配后重启";
        case ERROR_INSUFFICIENT_BUFFER: return "按 API 返回的所需长度增大输出缓冲区后重新调用";
        case ERROR_MOD_NOT_FOUND: return "确认依赖 DLL 位于程序目录且全部为 x64，使用依赖检查工具补齐缺失模块";
        case ERROR_DLL_NOT_FOUND: return "确认 UnrealDbgDll.dll、AIHelper.dll、D-encryption.dll 及其依赖项均在同一 x64 部署目录";
        case ERROR_PROC_NOT_FOUND: return "部署配套版本的 DLL，确认 LoadNT、UnloadNT、outDebug、PrintLog 等导出名称和位数一致";
        case ERROR_BAD_EXE_FORMAT: return "统一程序、DLL 和驱动的 x64 架构并重新部署未损坏的文件";
        case ERROR_FILENAME_EXCED_RANGE: return "缩短程序目录、驱动路径或调试器名称，避免超过 Windows 路径长度限制";
        case ERROR_NOT_ALL_ASSIGNED: return "以管理员身份运行，并在本地安全策略中允许当前账户获得 SeDebugPrivilege 后重启";
        case ERROR_PARTIAL_COPY: return "确认目标 PID 仍存活、模块版本和地址正确，并以管理员权限重新读取";
        case ERROR_INVALID_ADDRESS: return "重新枚举目标模块并校验偏移、位数和地址范围";
        case ERROR_NOACCESS: return "确认目标进程仍在运行、地址可访问且句柄权限足够，必要时重新打开句柄";
        case ERROR_DLL_INIT_FAILED: return "检查 DLL 依赖、驱动状态和运行库版本，查看 Log\\UnrealDbgDll.log 后重新启动";
        case ERROR_NOT_FOUND: return "确认设备、服务、进程、模块或配置对象已创建且名称正确";
        case ERROR_CANCELLED: return "重新执行操作；若未主动取消，检查安全软件、策略和超时设置";
        case ERROR_ACCESS_DISABLED_BY_POLICY: return "检查组策略、应用控制和安全软件规则，允许本程序及驱动后重试";
        case ERROR_INVALID_IMAGE_HASH: return "开发测试版：以管理员身份将运行目录 Certificates 中对应 .cer 导入本地计算机的“受信任的根证书颁发机构”和“受信任的发布者”，确认测试环境后启用 TESTSIGNING 并重启；正式环境改用受信任的正式签名驱动";
        case ERROR_DRIVER_BLOCKED: return "检查驱动签名、内存完整性和代码完整性事件日志，使用与当前 Windows/WDK 匹配的正式签名驱动";
        case ERROR_INVALID_WINDOW_HANDLE: return "确认窗口仍存在，并在窗口销毁时停止相关线程和清理句柄";
        case ERROR_NOT_ENOUGH_QUOTA: return "释放系统资源、增加虚拟内存并降低单次分配大小后重试";
        case ERROR_SERVICE_NOT_ACTIVE: return "启动对应驱动服务，确认服务状态为运行后重新初始化";
        case ERROR_SERVICE_DISABLED: return "在服务管理器中启用服务，并检查组策略或安全软件是否阻止启动";
        case ERROR_SERVICE_EXISTS: return "先查询现有服务状态：运行中的服务可复用；已停止的服务必须根据服务退出码修复签名、路径或驱动环境";
        case ERROR_SERVICE_ALREADY_RUNNING: return "无需重复启动；若设备仍不可用，检查设备符号链接和驱动日志";
        case ERROR_SERVICE_MARKED_FOR_DELETE: return "关闭占用该服务的程序，必要时重启系统后再安装或启动驱动";
        case ERROR_SERVICE_SPECIFIC_ERROR: return "查询服务的具体退出码并结合驱动日志排查；程序会优先记录该具体退出码";
        default: return "记录完整错误码、调用步骤和组件日志，检查权限、路径、目标进程与驱动状态后再重试";
        }
    }

    // 保留 Windows 对应错误的原始系统文本；仅打印数字会丢失签名、权限、
    // 服务状态等关键信息。这里先取 Unicode，再转成日志使用的 UTF-8，避免
    // 中文 Windows 上的 ANSI/UTF-8 混用再次产生乱码。
    static std::string Win32SystemMessage(DWORD error)
    {
        LPWSTR buffer = nullptr;
        const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
        if (length == 0 || buffer == nullptr)
        {
            return "系统未提供错误文本";
        }

        std::wstring message(buffer, length);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        {
            message.pop_back();
        }
        const std::string utf8 = WideToUtf8(message.c_str());
        return utf8.empty() ? "系统未提供错误文本" : utf8;
    }

    void LogError(const char* operation, DWORD error = ::GetLastError())
    {
        if (operation == nullptr)
        {
            operation = "操作";
        }
        if (error == ERROR_SUCCESS)
        {
            // 失败路径偶尔没有设置 GetLastError；避免把失败误报成“错误码 0（成功）”。
            error = ERROR_GEN_FAILURE;
        }
        Log("[ERROR] %s 失败；错误码=%lu (0x%08lX)；系统原文=%s；原因=%s；解决方案：%s。",
            operation,
            static_cast<unsigned long>(error),
            static_cast<unsigned long>(error),
            Win32SystemMessage(error).c_str(),
            Win32ErrorMessage(error).c_str(),
            Win32ErrorSolution(error).c_str());
    }

    ////英文日期
    //void Log(const std::string& message) {
    //    if (logFile.is_open()) {
    //        std::time_t now = std::time(nullptr);
    //        std::string timestamp = std::ctime(&now);
    //        timestamp.resize(timestamp.length() - 1);  // Remove trailing newline

    //        logFile << "[" << timestamp << "] " << message << std::endl;
    //        logFile.flush();
    //    }
    //}

    void Log(const char* format, ...) {
        if (format == nullptr)
        {
            format = "<null log format>";
        }

        va_list args;
        va_start(args, format);
        char message[4096] = { 0 };
        const int formatted = vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
        va_end(args);
        if (formatted < 0)
        {
            WriteFormattedMessage("<日志格式化失败或消息过长>");
            return;
        }

        WriteFormattedMessage(message);
    }

private:
    static std::string ParentDirectory(const std::string& path)
    {
        const std::string::size_type separator = path.find_last_of("\\/");
        return separator == std::string::npos ? std::string{} : path.substr(0, separator);
    }

    static std::string BaseLogName(const std::string& path)
    {
        const std::string::size_type separator = path.find_last_of("\\/");
        const std::string::size_type first = separator == std::string::npos ? 0 : separator + 1;
        const std::string::size_type extension = path.find_last_of('.');
        const std::string::size_type last = extension == std::string::npos || extension < first
            ? path.size() : extension;
        const std::string value = path.substr(first, last - first);
        return value.empty() ? "UnrealDbgDll" : value;
    }

    static std::string MakeSessionId()
    {
        SYSTEMTIME now{};
        ::GetLocalTime(&now);
        char value[96]{};
        sprintf_s(value, sizeof(value), "%04u%02u%02u-%02u%02u%02u-%03u-pid%lu-%08llX",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            now.wMilliseconds, static_cast<unsigned long>(::GetCurrentProcessId()),
            static_cast<unsigned long long>(::GetTickCount64() & 0xFFFFFFFFULL));
        return value;
    }

    static bool ShouldRotateLog(const std::string& path, const std::uint64_t incomingBytes)
    {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (!::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attributes))
        {
            return false;
        }
        ULARGE_INTEGER size{};
        size.LowPart = attributes.nFileSizeLow;
        size.HighPart = attributes.nFileSizeHigh;
        constexpr ULONGLONG kMaximumAggregateLogBytes = 16ULL * 1024ULL * 1024ULL;
        return size.QuadPart > kMaximumAggregateLogBytes ||
            (size.QuadPart <= kMaximumAggregateLogBytes &&
                incomingBytes > kMaximumAggregateLogBytes - size.QuadPart);
    }

    static bool RotateLog(const std::string& path)
    {
        const std::string::size_type extension = path.find_last_of('.');
        const std::string previous = extension == std::string::npos
            ? path + ".previous" : path.substr(0, extension) + ".previous" + path.substr(extension);
        // 轮转不成功（例如其他进程暂时持有文件）时保持追加，绝不丢失
        // 后端诊断信息。
        return ::MoveFileExA(path.c_str(), previous.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }

    static void ReopenAggregateLog(const std::string& path, std::ofstream& stream)
    {
        stream.open(path, std::ios::out | std::ios::app | std::ios::binary);
    }

    static void RotateAggregateLogIfNeeded(const std::string& path, std::ofstream& stream,
        const std::uint64_t incomingBytes)
    {
        if (!stream.is_open() || !ShouldRotateLog(path, incomingBytes))
        {
            return;
        }
        stream.flush();
        stream.close();
        (void)RotateLog(path);
        ReopenAggregateLog(path, stream);
    }

    static std::string WideToUtf8(const wchar_t* text)
    {
        if (text == nullptr || *text == L'\0')
        {
            return {};
        }

        const int length = static_cast<int>(wcslen(text));

        int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            text, length, nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            required = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
        }
        if (required <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, text, length, &result[0], required, nullptr, nullptr) != required)
        {
            return {};
        }
        return result;
    }

    void WriteFormattedMessage(const std::string& message)
    {
        // 线程同步：使用互斥锁保护临界区
        std::lock_guard<std::mutex> lock(mutex);

        SYSTEMTIME now = {};
        GetLocalTime(&now);
        char timestamp[64] = { 0 };
        sprintf_s(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
            now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        // 统一把旧模块沿用的英文级别标签转换为中文，避免不同组件的日志语言不一致。
        std::string localizedMessage = message;
        if (localizedMessage.rfind("[INFO]", 0) == 0)
        {
            localizedMessage.replace(0, 6, "[信息]");
        }
        else if (localizedMessage.rfind("[DEBUG]", 0) == 0)
        {
            localizedMessage.replace(0, 7, "[调试]");
        }
        else if (localizedMessage.rfind("[ERROR]", 0) == 0)
        {
            localizedMessage.replace(0, 7, "[错误]");
        }

        std::ostringstream oss;
        oss << "[" << timestamp << "]"
            << "[pid=" << GetCurrentProcessId() << "]"
            << "[tid=" << GetCurrentThreadId() << "] "
            << "[会话=" << sessionId << "]"
            << "[序号=" << ++sequence << "] "
            << localizedMessage;

        std::string logMessage = oss.str();
        const std::uint64_t aggregateBytesToWrite = static_cast<std::uint64_t>(logMessage.size() + 1);
        // 后端汇总文件长期被前端轮询。为能安全移动该文件，先关闭本 DLL
        // 的句柄；若轮转被其他进程占用，随后重新打开原文件并继续追加。
        RotateAggregateLogIfNeeded(filename, logFile, aggregateBytesToWrite);
        if (logFile.is_open()) {
            logFile << logMessage << std::endl;
            logFile.flush();  //将缓冲区中的数据立即刷新到磁盘，确保数据写入文件。
        }
        constexpr std::uint64_t kMaximumSessionLogBytes = 64ULL * 1024ULL * 1024ULL;
        const std::uint64_t bytesToWrite = static_cast<std::uint64_t>(logMessage.size() + 2);
        if (sessionLogFile.is_open() && !sessionLogTruncated)
        {
            if (sessionLogBytes + bytesToWrite <= kMaximumSessionLogBytes)
            {
                sessionLogFile << logMessage << "\r\n";
                sessionLogFile.flush();
                sessionLogBytes += bytesToWrite;
            }
            else
            {
                sessionLogTruncated = true;
                sessionLogFile << "[错误][会话=" << sessionId
                    << "] 会话日志达到 64 MB 上限；后续完整日志仍写入汇总日志。\r\n";
                sessionLogFile.flush();
            }
        }
        OutputDebugStringA(logMessage.c_str());
        OutputDebugStringA("\n");
    }

    std::ofstream logFile;
    std::string filename;
    std::ofstream sessionLogFile;
    std::string sessionId;
    std::string sessionFilename;
    std::uint64_t sequence{};
    std::uint64_t sessionLogBytes{};
    bool sessionLogTruncated{};
    std::wstring m_modName;
    std::mutex mutex; // 互斥锁
};

#endif // !_LOGGER_H

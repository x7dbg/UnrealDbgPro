#include "Core.h"

#include <CommCtrl.h>
#include <CommDlg.h>
#include <Richedit.h>
#include <Shellapi.h>
#include <Windowsx.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")

namespace
{
    using unrealdbg_native::DebuggerEntry;
    using unrealdbg_native::EncryptionApi;
    using unrealdbg_native::LogSink;
    using unrealdbg_native::NativeApi;

    // AIHelper.dll（以及旧版插件）通过宿主进程导出的 PrintLog 接收日志。
    // 原 Delphi 前端提供了这个导出，而新的 C++ 前端此前没有，导致插件
    // 退回到自己的旧版乱码 MessageBox。保存当前日志接收器后，导出函数
    // 可以把这些消息安全地送入同一套中文、分级、即时刷新的日志管道。
    std::atomic<LogSink*> g_activeLogSink{ nullptr };

    constexpr wchar_t kMainWindowClass[] = L"UnrealDbgNative.MainWindow";
    constexpr wchar_t kOverlayWindowClass[] = L"UnrealDbgNative.TargetOverlay";
    constexpr wchar_t kDebuggerIniName[] = L"DebuggerList.ini";
    constexpr wchar_t kConfigIniName[] = L"Config.ini";
    constexpr wchar_t kCopyrightName[] = L"copyright.db";
    constexpr wchar_t kCopyrightKey[] = L"9dd14d00f5dd71bd";
    constexpr wchar_t kDefaultCaption[] = L"虚幻调试器   by: Bug工程师   QQ群:740336586";
    constexpr wchar_t kDefaultCopyrightLog[] = L"默认版权 QQ群:740336586";
    constexpr uintptr_t kTlLastTickOffset = 0xFF0A439ULL + sizeof(DWORD);
    constexpr UINT_PTR kLogTimerId = 1;

    struct VirtualizationEnvironment
    {
        bool hypervisorPresent{};
        bool vbsEnabled{};
        bool hvciEnabled{};
    };

    [[nodiscard]] DWORD ReadDwordRegistryValue(const wchar_t* keyPath,
        const wchar_t* valueName) noexcept
    {
        HKEY key{};
        DWORD value{};
        DWORD valueSize = sizeof(value);
        DWORD valueType{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            return 0;
        }
        const LONG result = RegQueryValueExW(key, valueName, nullptr, &valueType,
            reinterpret_cast<LPBYTE>(&value), &valueSize);
        RegCloseKey(key);
        return result == ERROR_SUCCESS && valueType == REG_DWORD ? value : 0;
    }

    [[nodiscard]] VirtualizationEnvironment DetectVirtualizationEnvironment() noexcept
    {
        VirtualizationEnvironment result{};
        int registers[4]{};
        __cpuid(registers, 1);
        result.hypervisorPresent = (registers[2] & (1 << 31)) != 0;
        result.vbsEnabled = ReadDwordRegistryValue(
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
            L"EnableVirtualizationBasedSecurity") != 0;
        result.hvciEnabled = ReadDwordRegistryValue(
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
            L"Enabled") != 0;
        return result;
    }

    [[nodiscard]] bool IsDriverServiceRunning(const wchar_t* serviceName) noexcept
    {
        if (serviceName == nullptr || *serviceName == L'\0')
        {
            return false;
        }
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (manager == nullptr)
        {
            return false;
        }
        SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_QUERY_STATUS);
        if (service == nullptr)
        {
            CloseServiceHandle(manager);
            return false;
        }
        SERVICE_STATUS_PROCESS status{};
        DWORD returned{};
        const BOOL queried = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &returned);
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return queried && returned >= sizeof(status) && status.dwCurrentState == SERVICE_RUNNING;
    }

    enum ControlId : int
    {
        IdInitialize = 100,
        IdTabControl,
        IdDebuggerList,
        IdTlEnabled,
        IdTlGetTickCount,
        IdTlBlockResumeThread,
        IdTargetPicker,
        IdMenuStartDebugger = 200,
        IdMenuAddDebugger,
        IdMenuDeleteDebugger,
    };

    [[nodiscard]] std::wstring PathInApplicationDirectory(const std::wstring& directory, const wchar_t* name)
    {
        return (std::filesystem::path(directory) / name).wstring();
    }

    // 配置文件集中在 Config 子目录。读取时兼容旧版根目录文件；写入
    // 始终使用 Config，避免新运行再次把根目录污染成大量散落文件。
    [[nodiscard]] std::wstring PathInConfigDirectory(const std::wstring& directory, const wchar_t* name)
    {
        return (std::filesystem::path(directory) / L"Config" / name).wstring();
    }

    [[nodiscard]] std::wstring ExistingConfigPath(const std::wstring& directory, const wchar_t* name)
    {
        const std::wstring configPath = PathInConfigDirectory(directory, name);
        if (unrealdbg_native::FileExists(configPath))
        {
            return configPath;
        }
        return PathInApplicationDirectory(directory, name);
    }

    [[nodiscard]] bool EnsureConfigDirectory(const std::wstring& directory, LogSink& log)
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(directory) / L"Config", error);
        if (error)
        {
            log.Error(L"创建 Config 配置目录：" + (std::filesystem::path(directory) / L"Config").wstring(),
                static_cast<DWORD>(error.value()));
            return false;
        }
        return true;
    }

    [[nodiscard]] bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) noexcept
    {
        return _wcsicmp(left.c_str(), right.c_str()) == 0;
    }

    [[nodiscard]] bool ContainsDebugger(const std::vector<DebuggerEntry>& entries, const DebuggerEntry& candidate)
    {
        return std::any_of(entries.begin(), entries.end(), [&candidate](const DebuggerEntry& existing)
        {
            return EqualsInsensitive(existing.name, candidate.name) && EqualsInsensitive(existing.path, candidate.path);
        });
    }

    [[nodiscard]] std::wstring ReadIniString(const std::wstring& filePath, const wchar_t* section, const wchar_t* key)
    {
        std::vector<wchar_t> buffer(256);
        for (;;)
        {
            const DWORD copied = GetPrivateProfileStringW(section, key, L"", buffer.data(),
                static_cast<DWORD>(buffer.size()), filePath.c_str());
            if (copied < buffer.size() - 1)
            {
                return std::wstring(buffer.data(), copied);
            }
            if (buffer.size() >= 32768)
            {
                return {};
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    [[nodiscard]] std::vector<std::wstring> ReadIniSectionNames(const std::wstring& filePath)
    {
        std::vector<std::wstring> sections;
        std::vector<wchar_t> buffer(1024);
        for (;;)
        {
            const DWORD copied = GetPrivateProfileSectionNamesW(buffer.data(), static_cast<DWORD>(buffer.size()), filePath.c_str());
            if (copied < buffer.size() - 2)
            {
                const wchar_t* current = buffer.data();
                const wchar_t* const end = buffer.data() + copied;
                while (current < end && *current != L'\0')
                {
                    sections.emplace_back(current);
                    current += wcslen(current) + 1;
                }
                return sections;
            }
            if (buffer.size() >= 65536)
            {
                return sections;
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    [[nodiscard]] bool ReadIniBoolean(const std::wstring& filePath, const wchar_t* section, const wchar_t* key, const bool defaultValue)
    {
        const std::wstring value = ReadIniString(filePath, section, key);
        if (value.empty())
        {
            return defaultValue;
        }
        return value == L"1" || EqualsInsensitive(value, L"true") || EqualsInsensitive(value, L"yes") || EqualsInsensitive(value, L"on");
    }

    [[nodiscard]] bool WriteIniBoolean(const std::wstring& filePath, const wchar_t* section, const wchar_t* key, const bool value)
    {
        return WritePrivateProfileStringW(section, key, value ? L"1" : L"0", filePath.c_str()) != FALSE;
    }

    [[nodiscard]] bool ParseUnsigned(const std::wstring& input, unsigned long& value) noexcept
    {
        if (input.empty())
        {
            return false;
        }
        errno = 0;
        wchar_t* end{};
        const unsigned long parsed = wcstoul(input.c_str(), &end, 10);
        if (errno == ERANGE || end == input.c_str() || *end != L'\0')
        {
            return false;
        }
        value = parsed;
        return true;
    }

    // AIHelper.dll 是历史二进制，部分失败日志仍采用类似
    // “驱动安装失败! (error:577)”的英文错误码格式，而没有 [ERROR]
    // 标签。若只按标签分级，这类真正的失败会被错误显示为普通绿色
    // 信息，用户也无法从 PrintLog 本身看到具体的解决办法。
    struct PrintLogFailure
    {
        bool indicatesFailure{};
        std::optional<DWORD> errorCode;
    };

    [[nodiscard]] wchar_t ToLowerAscii(const wchar_t character) noexcept
    {
        return character >= L'A' && character <= L'Z'
            ? static_cast<wchar_t>(character - L'A' + L'a')
            : character;
    }

    [[nodiscard]] bool ContainsAsciiInsensitive(const std::wstring_view text, const std::wstring_view token) noexcept
    {
        if (token.empty() || text.size() < token.size())
        {
            return false;
        }

        for (size_t start = 0; start + token.size() <= text.size(); ++start)
        {
            size_t offset = 0;
            for (; offset < token.size(); ++offset)
            {
                if (ToLowerAscii(text[start + offset]) != ToLowerAscii(token[offset]))
                {
                    break;
                }
            }
            if (offset == token.size())
            {
                return true;
            }
        }
        return false;
    }

    void ReplaceAsciiInsensitive(std::wstring& text, const std::wstring_view token,
        const std::wstring_view replacement)
    {
        if (token.empty())
        {
            return;
        }
        size_t searchFrom = 0;
        while (searchFrom + token.size() <= text.size())
        {
            size_t match = searchFrom;
            for (; match + token.size() <= text.size(); ++match)
            {
                size_t offset = 0;
                for (; offset < token.size(); ++offset)
                {
                    if (ToLowerAscii(text[match + offset]) != ToLowerAscii(token[offset]))
                    {
                        break;
                    }
                }
                if (offset == token.size())
                {
                    break;
                }
            }
            if (match + token.size() > text.size())
            {
                break;
            }
            text.replace(match, token.size(), replacement);
            searchFrom = match + replacement.size();
        }
    }

    [[nodiscard]] std::wstring LocalizeLegacyPrintLog(std::wstring message)
    {
        // AIHelper 的旧文本中仍可能带英文级别和“error:”标签。日志
        // 窗口统一显示中文，但错误码解析仍使用替换前的原文。
        ReplaceAsciiInsensitive(message, L"[ERROR]", L"[错误]");
        ReplaceAsciiInsensitive(message, L"[INFO]", L"[信息]");
        ReplaceAsciiInsensitive(message, L"[DEBUG]", L"[调试]");
        ReplaceAsciiInsensitive(message, L"error code:", L"错误码：");
        ReplaceAsciiInsensitive(message, L"error code=", L"错误码：");
        ReplaceAsciiInsensitive(message, L"error:", L"错误码：");
        ReplaceAsciiInsensitive(message, L"error=", L"错误码：");
        return message;
    }

    [[nodiscard]] std::optional<DWORD> TryExtractLegacyErrorCode(const std::wstring_view text) noexcept
    {
        constexpr std::wstring_view kMarkers[] =
        {
            L"error:", L"error=", L"error code:", L"error code=", L"错误码：", L"错误码:", L"错误：", L"错误:"
        };

        for (const std::wstring_view marker : kMarkers)
        {
            for (size_t markerStart = 0; markerStart + marker.size() <= text.size(); ++markerStart)
            {
                bool markerMatches = true;
                for (size_t offset = 0; offset < marker.size(); ++offset)
                {
                    const wchar_t left = text[markerStart + offset];
                    const wchar_t right = marker[offset];
                    const bool equal = left <= 0x7F && right <= 0x7F
                        ? ToLowerAscii(left) == ToLowerAscii(right)
                        : left == right;
                    if (!equal)
                    {
                        markerMatches = false;
                        break;
                    }
                }
                if (!markerMatches)
                {
                    continue;
                }

                size_t position = markerStart + marker.size();
                while (position < text.size() && (text[position] == L' ' || text[position] == L'\t'))
                {
                    ++position;
                }

                int base = 10;
                if (position + 2 <= text.size() && text[position] == L'0' &&
                    (text[position + 1] == L'x' || text[position + 1] == L'X'))
                {
                    base = 16;
                    position += 2;
                }

                const size_t digitsStart = position;
                unsigned long long parsed{};
                while (position < text.size())
                {
                    const wchar_t character = text[position];
                    unsigned int digit{};
                    if (character >= L'0' && character <= L'9')
                    {
                        digit = static_cast<unsigned int>(character - L'0');
                    }
                    else if (base == 16 && character >= L'a' && character <= L'f')
                    {
                        digit = static_cast<unsigned int>(character - L'a' + 10);
                    }
                    else if (base == 16 && character >= L'A' && character <= L'F')
                    {
                        digit = static_cast<unsigned int>(character - L'A' + 10);
                    }
                    else
                    {
                        break;
                    }

                    if (digit >= static_cast<unsigned int>(base) ||
                        parsed > (std::numeric_limits<DWORD>::max() - digit) / static_cast<unsigned int>(base))
                    {
                        break;
                    }
                    parsed = parsed * static_cast<unsigned int>(base) + digit;
                    ++position;
                }

                if (position != digitsStart)
                {
                    return static_cast<DWORD>(parsed);
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] PrintLogFailure DiagnosePrintLogMessage(const std::wstring_view message) noexcept
    {
        PrintLogFailure result{};
        result.errorCode = TryExtractLegacyErrorCode(message);
        const bool hasLegacyErrorMarker = ContainsAsciiInsensitive(message, L"error:") ||
            ContainsAsciiInsensitive(message, L"error=") ||
            message.find(L"错误码：") != std::wstring_view::npos ||
            message.find(L"错误码:") != std::wstring_view::npos;
        // 出现错误码标记但后面的数字损坏/溢出，同样必须按错误处理，
        // 不能因为解析失败而把真实失败降级成绿色普通信息。
        result.indicatesFailure = (result.errorCode.has_value() && *result.errorCode != ERROR_SUCCESS) ||
            (hasLegacyErrorMarker && !result.errorCode.has_value());
        result.indicatesFailure = result.indicatesFailure ||
            message.find(L"[错误]") != std::wstring_view::npos ||
            ContainsAsciiInsensitive(message, L"[error]") ||
            message.find(L"失败") != std::wstring_view::npos ||
            ContainsAsciiInsensitive(message, L"failed");
        return result;
    }

    // AIHelper.dll 旧接口在驱动签名/服务启动失败时经常统一返回
    // ERROR_GEN_FAILURE(31)，导致前端无法告诉用户真正原因。读取服务控制
    // 管理器保留的退出码，可以把常见的 577（签名无效）等具体错误还原出来。
    [[nodiscard]] DWORD QueryDriverServiceFailure(const wchar_t* serviceName) noexcept
    {
        if (serviceName == nullptr || *serviceName == L'\0')
        {
            return ERROR_SUCCESS;
        }

        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (manager == nullptr)
        {
            return ERROR_SUCCESS;
        }
        SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_QUERY_STATUS);
        if (service == nullptr)
        {
            CloseServiceHandle(manager);
            return ERROR_SUCCESS;
        }

        SERVICE_STATUS_PROCESS status{};
        DWORD returned{};
        const BOOL queried = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &returned);
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        if (!queried || returned < sizeof(status) || status.dwCurrentState != SERVICE_STOPPED)
        {
            return ERROR_SUCCESS;
        }
        if (status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR &&
            status.dwServiceSpecificExitCode != ERROR_SUCCESS)
        {
            return status.dwServiceSpecificExitCode;
        }
        return status.dwWin32ExitCode == ERROR_SUCCESS ? ERROR_SUCCESS : status.dwWin32ExitCode;
    }

    [[nodiscard]] std::wstring BuildInitializationFailureMessage(const DWORD error)
    {
        const DWORD diagnosticError = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
        const unrealdbg_native::ErrorExplanation explanation = unrealdbg_native::ExplainWin32Error(diagnosticError);
        const std::wstring systemMessage = unrealdbg_native::GetSystemErrorMessage(diagnosticError);
        std::wstringstream message;
        message << L"设备或驱动初始化失败。\r\n\r\n"
            << L"错误码：" << diagnosticError << L" (0x" << std::hex << std::uppercase
            << std::setw(8) << std::setfill(L'0') << diagnosticError << std::dec << L")\r\n"
            << L"系统原文：" << systemMessage << L"\r\n"
            << L"原因：" << explanation.reason << L"\r\n"
            << L"解决方案：" << explanation.solution << L"\r\n\r\n"
            << L"发生阶段：VT_Driver 服务加载 / UnrealDbgDll.Initialize（符号表和设备握手）\r\n"
            << L"详细日志：Log\\log.ini、Log\\UnrealDbgDll.log";
        if (diagnosticError == ERROR_GEN_FAILURE)
        {
            message << L"\r\n\r\n补充诊断：错误码 31（ERROR_GEN_FAILURE）只是 Windows 的通用失败码，"
                << L"不代表具体根因。程序已经尝试读取 VT_Driver 服务退出码；请以日志中更具体的退出码为准。";
        }
        else if (diagnosticError == ERROR_INVALID_IMAGE_HASH)
        {
            message << L"\r\n\r\n补充诊断：577（ERROR_INVALID_IMAGE_HASH）表示 Windows 在服务启动阶段拒绝驱动映像。"
                << L"常见根因是测试证书未导入到本地计算机证书存储、证书链不受信任、"
                << L"测试签名未启用，或 HVCI/内存完整性策略阻止该驱动。"
                << L"因此 DbgkSysWin11 设备尚未创建，并非缺少需要虚拟化的设备。";
        }
        return message.str();
    }

    [[nodiscard]] std::wstring BuildWin32FailureMessage(const std::wstring_view operation, const DWORD error)
    {
        const DWORD diagnosticError = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
        const unrealdbg_native::ErrorExplanation explanation = unrealdbg_native::ExplainWin32Error(diagnosticError);
        std::wstringstream message;
        message << L"操作失败：" << operation << L"\r\n\r\n"
            << L"错误码：" << diagnosticError << L" (0x" << std::hex << std::uppercase
            << std::setw(8) << std::setfill(L'0') << diagnosticError << std::dec << L")\r\n"
            << L"系统原文：" << unrealdbg_native::GetSystemErrorMessage(diagnosticError) << L"\r\n"
            << L"原因：" << explanation.reason << L"\r\n"
            << L"解决方案：" << explanation.solution;
        return message.str();
    }

    void SetControlFont(const HWND window, const HFONT font) noexcept
    {
        if (window != nullptr && font != nullptr)
        {
            SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    [[nodiscard]] bool HasCommandLineArgument(const wchar_t* expectedArgument)
    {
        int argumentCount{};
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return false;
        }
        bool found = false;
        for (int index = 1; index < argumentCount; ++index)
        {
            if (EqualsInsensitive(arguments[index], expectedArgument))
            {
                found = true;
                break;
            }
        }
        LocalFree(arguments);
        return found;
    }

    [[nodiscard]] std::wstring Utf8ToWide(const std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        if (required <= 0)
        {
            required = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (required <= 0)
            {
                return {};
            }
            std::wstring result(static_cast<size_t>(required), L'\0');
            if (MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), result.data(), required) != required)
            {
                return {};
            }
            return result;
        }

        std::wstring result(static_cast<size_t>(required), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            result.data(), required) != required)
        {
            return {};
        }
        return result;
    }

    class BackendLogTail final
    {
    public:
        explicit BackendLogTail(std::filesystem::path path)
            : path_(std::move(path))
        {
        }

        void StartAtEnd() noexcept
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path_, error);
            offset_ = error ? 0 : size;
            partialLine_.clear();
        }

        void Poll(LogSink& log)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path_, error);
            if (error)
            {
                return;
            }
            if (size < offset_)
            {
                // 后端日志已被轮换或截断；从头开始读取，不保留旧文件的片段。
                offset_ = 0;
                partialLine_.clear();
            }
            if (size == offset_)
            {
                return;
            }

            std::ifstream file(path_, std::ios::binary);
            if (!file.is_open())
            {
                return;
            }
            file.seekg(static_cast<std::streamoff>(offset_), std::ios::beg);
            if (!file)
            {
                return;
            }

            std::string chunk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (chunk.empty())
            {
                return;
            }
            offset_ += chunk.size();
            partialLine_ += std::move(chunk);

            size_t lineEnd = 0;
            while ((lineEnd = partialLine_.find('\n')) != std::string::npos)
            {
                std::string line = partialLine_.substr(0, lineEnd);
                partialLine_.erase(0, lineEnd + 1);
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                EmitLine(line, log);
            }

            // 损坏的后端日志不能在没有换行符的情况下无限增长此内存尾部。
            constexpr size_t kMaximumPartialLine = 1024 * 1024;
            if (partialLine_.size() > kMaximumPartialLine)
            {
                partialLine_.clear();
                log.Error(L"读取 UnrealDbgDll 后端日志时发现超长未结束行，已丢弃该行", ERROR_INVALID_DATA);
            }
        }

    private:
        static void EmitLine(const std::string& line, LogSink& log)
        {
            if (line.empty())
            {
                return;
            }

            unrealdbg_native::LogLevel level = unrealdbg_native::LogLevel::Info;
            if (line.find("[错误]") != std::string::npos || line.find("[ERROR]") != std::string::npos)
            {
                level = unrealdbg_native::LogLevel::Error;
            }
            else if (line.find("[调试]") != std::string::npos || line.find("[DEBUG]") != std::string::npos)
            {
                level = unrealdbg_native::LogLevel::Debug;
            }

            const std::wstring wideLine = Utf8ToWide(line);
            if (wideLine.empty())
            {
                log.Error(L"读取 UnrealDbgDll 后端日志时 UTF-8 解码失败", ERROR_INVALID_DATA);
                return;
            }
            log.External(level, L"[后端] " + wideLine);
        }

        std::filesystem::path path_;
        std::uintmax_t offset_{};
        std::string partialLine_;
    };

    class TlTickWorker final
    {
    public:
        explicit TlTickWorker(LogSink& log) noexcept
            : log_(log)
        {
        }

        TlTickWorker(const TlTickWorker&) = delete;
        TlTickWorker& operator=(const TlTickWorker&) = delete;

        ~TlTickWorker()
        {
            Stop();
        }

        [[nodiscard]] bool Start(const DWORD processId, const uintptr_t address)
        {
            Stop();
            if (processId == 0 || address == 0)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                log_.Error(L"TL GetTickCount 工作线程请求被拒绝：PID 或地址为空", ERROR_INVALID_PARAMETER);
                return false;
            }

            stopRequested_.store(false, std::memory_order_release);
            try
            {
                worker_ = std::thread(&TlTickWorker::ThreadMain, this, processId, address);
                return true;
            }
            catch (const std::system_error& error)
            {
                std::wstringstream stream;
                stream << L"无法创建 TL GetTickCount 工作线程；C++ 错误码=" << error.code().value();
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                log_.Error(stream.str(), ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
        }

        void Stop() noexcept
        {
            stopRequested_.store(true, std::memory_order_release);
            waitCondition_.notify_all();
            if (worker_.joinable())
            {
                worker_.join();
            }
        }

    private:
        [[nodiscard]] bool WaitForStop(const std::chrono::milliseconds duration) noexcept
        {
            std::unique_lock lock(waitMutex_);
            return waitCondition_.wait_for(lock, duration, [this]
            {
                return stopRequested_.load(std::memory_order_acquire);
            });
        }

        void ThreadMain(const DWORD processId, const uintptr_t address) noexcept
        {
            HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                FALSE, processId);
            if (process == nullptr)
            {
                log_.Error(L"打开 TL GetTickCount 目标进程", GetLastError());
                return;
            }

            bool reportedSuccess = false;
            while (!stopRequested_.load(std::memory_order_acquire))
            {
                if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
                {
                    log_.Info(L"[TL.exe] 目标进程已退出，正在停止 GetTickCount 工作线程");
                    break;
                }
                const DWORD zero{};
                SIZE_T bytesWritten{};
                const BOOL writeResult = WriteProcessMemory(process, reinterpret_cast<void*>(address), &zero, sizeof(zero), &bytesWritten);
                if (!writeResult || bytesWritten != sizeof(zero))
                {
                    const DWORD error = writeResult ? ERROR_PARTIAL_COPY : GetLastError();
                    log_.Error(L"写入 TL GetTickCount 目标内存", error);
                    if (WaitForStop(std::chrono::milliseconds(1500)))
                    {
                        break;
                    }
                    continue;
                }

                if (!reportedSuccess)
                {
                    reportedSuccess = true;
                    log_.Info(L"[TL.exe] GetTickCount 检测处理已启用");
                }
                if (WaitForStop(std::chrono::milliseconds(50)))
                {
                    break;
                }
            }
            CloseHandle(process);
        }

        LogSink& log_;
        std::atomic_bool stopRequested_{ true };
        std::mutex waitMutex_;
        std::condition_variable waitCondition_;
        std::thread worker_;
    };

    class Application final
    {
    public:
        Application(const HINSTANCE instance, std::wstring applicationDirectory)
            : instance_(instance), applicationDirectory_(std::move(applicationDirectory)), log_(applicationDirectory_),
              nativeApi_(log_), encryptionApi_(log_), tlTickWorker_(log_),
              backendLogTail_(std::filesystem::path(applicationDirectory_) / L"Log" / L"UnrealDbgDll.log")
        {
            g_activeLogSink.store(&log_, std::memory_order_release);
        }

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        ~Application()
        {
            Shutdown();
        }

        void SetWindowHandle(const HWND window) noexcept
        {
            window_ = window;
        }

        [[nodiscard]] bool Initialize()
        {
            if (!CreateControls())
            {
                return false;
            }

            log_.AttachRichEdit(logView_);
            // 50 ms 刷新一次，后台线程产生的新日志可近乎即时显示。
            SetTimer(window_, kLogTimerId, 50, nullptr);
            log_.Info(L"=== UnrealDbgNative 启动 ===");
            log_.Info(L"原生 Win32 前端已初始化；后端驱动和 DLL 协议保持不变");
            log_.Info(L"符号表加载阶段将在点击“进入 VT 调试模式”后执行；程序启动阶段不会提前加载驱动");

            // 在启动时验证旧版回调，在任何后端代码请求之前。
            // 这将缺失导出的弹窗转换为确定性的、可操作的日志记录，
            // 并确认 AIHelper.dll 稍后在驱动初始化期间将使用的 ABI。
            const FARPROC printLog = GetProcAddress(instance_, "PrintLog");
            if (printLog == nullptr)
            {
                log_.Error(L"宿主未导出兼容日志入口 PrintLog；VT 初始化将被拒绝", ERROR_PROC_NOT_FOUND);
            }
            else
            {
                log_.Info(L"兼容日志入口 PrintLog 已验证；AIHelper 日志将回传到中文日志窗口");
            }

            LoadCopyright();
            SetWindowTextW(window_, caption_.c_str());

            const VirtualizationEnvironment virtualization = DetectVirtualizationEnvironment();
            log_.Info(virtualization.hypervisorPresent
                ? L"系统虚拟化检查：检测到当前运行在 Hyper-V 或其他虚拟机监控程序下"
                : L"系统虚拟化检查：未检测到外部虚拟机监控程序");
            log_.Info(virtualization.vbsEnabled
                ? L"系统虚拟化检查：VBS（基于虚拟化的安全）已启用"
                : L"系统虚拟化检查：VBS 未启用");
            log_.Info(virtualization.hvciEnabled
                ? L"系统虚拟化检查：内存完整性（HVCI）已启用"
                : L"系统虚拟化检查：内存完整性（HVCI）未启用");

            // 跳过之前运行的旧记录，仅将当前 DLL 调用流式传输到同一彩色 UI 日志。
            backendLogTail_.StartAtEnd();
            log_.Info(L"后端日志同步已启用；符号表、IOCTL 和驱动握手详情将实时显示。内核日志使用 UDBG-UTF8/1，不再读取 C:\\Logs\\driver.xml");
            nativeApiLoaded_ = nativeApi_.Load(applicationDirectory_);
            if (!nativeApiLoaded_)
            {
                log_.Error(L"UnrealDbgDll 不可用；部署 DLL 前，初始化和调试器启动操作都会失败", ERROR_DLL_NOT_FOUND);
            }

            // 当前的 Delphi KeyVerification.pas 是一个本地存根，其 cdkeyLogin 实现
            // 返回成功而不联系服务。保留该行为，同时避免旧版 bug 中成功结果从未反映在标志中的问题。
            authorized_ = true;
            log_.Info(L"本地授权兼容模式已接受当前 KeyVerification 模拟结果");

            if (!unrealdbg_native::EnableDebugPrivilege(log_))
            {
                log_.Error(L"SeDebugPrivilege 未启用；目标进程操作可能失败。请在需要时以管理员身份运行",
                    ERROR_NOT_ALL_ASSIGNED);
            }

            LoadDebuggerList();
            LoadTlSettings();
            UpdateSystemInformation();
            // 旧 Win32 前端保留与 WinUI 相同的只读开发诊断基线，避免
            // 用旧发布包复现问题时缺少系统、服务与部署文件证据。
            unrealdbg_native::WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_,
                L"Win32 启动完成、驱动加载前");
            log_.Info(L"=== UnrealDbgNative 启动完成 ===");
            return true;
        }

        [[nodiscard]] LRESULT HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
        {
            switch (message)
            {
            case WM_COMMAND:
                HandleCommand(LOWORD(wParam), HIWORD(wParam));
                return 0;
            case WM_NOTIFY:
                if (reinterpret_cast<const NMHDR*>(lParam) != nullptr && reinterpret_cast<const NMHDR*>(lParam)->idFrom == IdTabControl &&
                    reinterpret_cast<const NMHDR*>(lParam)->code == TCN_SELCHANGE)
                {
                    UpdateTabVisibility();
                    return 0;
                }
                break;
            case WM_CONTEXTMENU:
                if (reinterpret_cast<HWND>(wParam) == debuggerList_)
                {
                    ShowDebuggerContextMenu(lParam);
                    return 0;
                }
                break;
            case WM_TIMER:
                if (wParam == kLogTimerId)
                {
                    backendLogTail_.Poll(log_);
                    log_.DrainToRichEdit();
                    return 0;
                }
                break;
            case WM_MOUSEMOVE:
                if (targetSelectionActive_)
                {
                    UpdateTargetSelection();
                    return 0;
                }
                break;
            case WM_LBUTTONUP:
                if (targetSelectionActive_)
                {
                    FinishTargetSelection();
                    return 0;
                }
                break;
            case WM_CANCELMODE:
            case WM_CAPTURECHANGED:
                if (targetSelectionActive_)
                {
                    CancelTargetSelection();
                    return 0;
                }
                break;
            case WM_CTLCOLORSTATIC:
                if (reinterpret_cast<HWND>(lParam) == tlWarning_)
                {
                    const HDC hdc = reinterpret_cast<HDC>(wParam);
                    SetTextColor(hdc, RGB(210, 40, 40));
                    SetBkMode(hdc, TRANSPARENT);
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
                }
                break;
            case WM_CLOSE:
                if (serviceStarted_)
                {
                    const int choice = MessageBoxW(window_,
                        L"VT 调试模式已启动。关闭窗口不会保证后端驱动完全退出。是否仍要关闭？",
                        L"确认关闭", MB_YESNO | MB_ICONWARNING | MB_SYSTEMMODAL);
                    if (choice != IDYES)
                    {
                        return 0;
                    }
                }
                break;
            case WM_DESTROY:
                Shutdown();
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(window_, message, wParam, lParam);
        }

        [[nodiscard]] bool BeginTargetSelection()
        {
            if (!tlEnabled_)
            {
                log_.Error(L"目标选择被拒绝：定制化 TL 对抗功能未启用", ERROR_ACCESS_DENIED);
                MessageBoxW(window_, L"请先勾选“启用定制化对抗”。", L"无法选择目标", MB_OK | MB_ICONINFORMATION);
                return false;
            }
            if (targetSelectionActive_)
            {
                return true;
            }

            selectedTargetWindow_ = nullptr;
            selectedTargetProcessId_ = 0;
            targetSelectionActive_ = true;
            SetCapture(window_);
            SetWindowTextW(tlTargetStatus_, L"拖动到目标窗口后松开鼠标");
            log_.Info(L"TL 目标选择已开始");
            return true;
        }

    private:
        HWND CreateControl(const DWORD extendedStyle, const wchar_t* className, const wchar_t* text, const DWORD style,
            const int x, const int y, const int width, const int height, const int identifier)
        {
            const HWND result = CreateWindowExW(extendedStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                x, y, width, height, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance_, nullptr);
            if (result == nullptr)
            {
                log_.Error(std::wstring(L"创建控件失败：CreateWindowExW，控件类型=") + className, GetLastError());
            }
            SetControlFont(result, mainFont_);
            return result;
        }

        [[nodiscard]] bool CreateControls()
        {
            mainFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            logFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            if (logFont_ == nullptr)
            {
                logFont_ = mainFont_;
                log_.Error(L"创建 Consolas 日志字体 CreateFontW", GetLastError());
            }

            CreateControl(0, L"BUTTON", L"系统信息", BS_GROUPBOX, 8, 8, 401, 121, 0);
            systemName_ = CreateControl(0, L"STATIC", L"系统名称：读取中", SS_LEFT, 24, 32, 376, 20, 0);
            systemVersion_ = CreateControl(0, L"STATIC", L"系统版本：读取中", SS_LEFT, 24, 64, 376, 20, 0);
            cpuName_ = CreateControl(0, L"STATIC", L"CPU 型号：读取中", SS_LEFT, 24, 96, 376, 20, 0);

            CreateControl(0, L"BUTTON", L"功能区域", BS_GROUPBOX, 8, 135, 401, 137, 0);
            const HWND protectDebugger = CreateControl(0, L"BUTTON", L"保护调试器", BS_AUTOCHECKBOX | WS_DISABLED, 25, 157, 120, 20, 0);
            const HWND softwareBreakpoint = CreateControl(0, L"BUTTON", L"无痕软件断点", BS_AUTOCHECKBOX | WS_DISABLED, 25, 203, 120, 20, 0);
            const HWND hardwareBreakpoint = CreateControl(0, L"BUTTON", L"无痕硬件断点", BS_AUTOCHECKBOX | WS_DISABLED, 25, 180, 120, 20, 0);
            const HWND exceptionFilter = CreateControl(0, L"BUTTON", L"异常过滤", BS_AUTOCHECKBOX | WS_DISABLED, 25, 226, 120, 20, 0);
            CreateControl(0, L"BUTTON", L"随机进程名", BS_AUTOCHECKBOX | WS_DISABLED, 25, 249, 120, 20, 0);
            const HWND forceDetach = CreateControl(0, L"BUTTON", L"强制剥离调试器", BS_AUTOCHECKBOX | WS_DISABLED, 166, 157, 150, 20, 0);
            SendMessageW(protectDebugger, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(softwareBreakpoint, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(hardwareBreakpoint, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(exceptionFilter, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(forceDetach, BM_SETCHECK, BST_CHECKED, 0);
            initializeButton_ = CreateControl(0, L"BUTTON", L"进入 VT 调试模式", BS_PUSHBUTTON, 163, 194, 166, 63, IdInitialize);

            tabControl_ = CreateControl(0, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, 432, 8, 442, 268, IdTabControl);
            TCITEMW tabItem{};
            tabItem.mask = TCIF_TEXT;
            tabItem.pszText = const_cast<LPWSTR>(L"调试器列表");
            TabCtrl_InsertItem(tabControl_, 0, &tabItem);
            tabItem.pszText = const_cast<LPWSTR>(L"定制化对抗");
            TabCtrl_InsertItem(tabControl_, 1, &tabItem);

            debuggerList_ = CreateControl(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                438, 38, 430, 232, IdDebuggerList);
            ListView_SetExtendedListViewStyle(debuggerList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            AddDebuggerListColumn(L"序号", 56);
            AddDebuggerListColumn(L"调试器", 150);
            AddDebuggerListColumn(L"文件路径", 500);

            tlEnabledCheckbox_ = CreateControl(0, L"BUTTON", L"启用定制化对抗", BS_AUTOCHECKBOX, 450, 42, 150, 20, IdTlEnabled);
            tlOptionsGroup_ = CreateControl(0, L"BUTTON", L"定制化对抗", BS_GROUPBOX, 450, 72, 220, 120, 0);
            tlGetTickCountCheckbox_ = CreateControl(0, L"BUTTON", L"处理 GetTickCount 检测", BS_AUTOCHECKBOX, 466, 104, 180, 20, IdTlGetTickCount);
            tlBlockResumeCheckbox_ = CreateControl(0, L"BUTTON", L"阻止游戏恢复线程", BS_AUTOCHECKBOX | WS_DISABLED, 466, 130, 180, 20, IdTlBlockResumeThread);
            tlWarning_ = CreateControl(0, L"STATIC", L"注意：定制化对抗需要随游戏版本更新验证；\r\n更新后固定偏移可能已失效，请先停止功能并联系维护人员。", SS_LEFT,
                450, 203, 280, 48, 0);
            // 目标选择是一个真正的功能，因此将其放在专用的组框中，而不是在选项卡边缘
            // 留下松散的标签/控件对。之前的静态标签在某些系统字体上可能看起来像
            // 孤立的或格式错误的控件。
            tlTargetGroup_ = CreateControl(0, L"BUTTON", L"目标窗口", BS_GROUPBOX, 690, 72, 172, 120, 0);
            tlTargetStatus_ = CreateControl(0, L"STATIC", L"尚未选择目标进程", SS_LEFT, 704, 100, 144, 36, 0);
            targetPickerButton_ = CreateControl(0, L"BUTTON", L"选择目标窗口", BS_PUSHBUTTON, 704, 150, 144, 32, IdTargetPicker);
            if (targetPickerButton_ != nullptr && !SetWindowSubclass(targetPickerButton_, TargetPickerSubclass, 1,
                reinterpret_cast<DWORD_PTR>(this)))
            {
                log_.Error(L"设置目标选择按钮子类 SetWindowSubclass", GetLastError());
                return false;
            }

            logView_ = CreateControl(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                8, 282, 866, 220, 0);
            SetWindowLongPtrW(logView_, GWL_STYLE, GetWindowLongPtrW(logView_, GWL_STYLE) | ES_NOHIDESEL);
            SendMessageW(logView_, EM_SETBKGNDCOLOR, 0, RGB(0, 0, 0));
            SetControlFont(logView_, logFont_);

            if (systemName_ == nullptr || systemVersion_ == nullptr || cpuName_ == nullptr || initializeButton_ == nullptr ||
                tabControl_ == nullptr || debuggerList_ == nullptr || tlEnabledCheckbox_ == nullptr || tlGetTickCountCheckbox_ == nullptr ||
                tlBlockResumeCheckbox_ == nullptr || tlWarning_ == nullptr || tlOptionsGroup_ == nullptr || tlTargetGroup_ == nullptr ||
                tlTargetStatus_ == nullptr || targetPickerButton_ == nullptr ||
                logView_ == nullptr)
            {
                log_.Error(L"原生界面未能创建全部必需控件", ERROR_INVALID_WINDOW_HANDLE);
                return false;
            }
            UpdateTabVisibility();
            return true;
        }

        void AddDebuggerListColumn(const wchar_t* caption, const int width) const noexcept
        {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<LPWSTR>(caption);
            column.cx = width;
            ListView_InsertColumn(debuggerList_, Header_GetItemCount(ListView_GetHeader(debuggerList_)), &column);
        }

        void UpdateTabVisibility() noexcept
        {
            const bool debuggerTabActive = TabCtrl_GetCurSel(tabControl_) != 1;
            ShowWindow(debuggerList_, debuggerTabActive ? SW_SHOW : SW_HIDE);
            const int tlVisibility = debuggerTabActive ? SW_HIDE : SW_SHOW;
            for (const HWND control : { tlEnabledCheckbox_, tlGetTickCountCheckbox_, tlBlockResumeCheckbox_, tlWarning_, tlOptionsGroup_,
                tlTargetGroup_, tlTargetStatus_, targetPickerButton_ })
            {
                ShowWindow(control, tlVisibility);
            }
        }

        void LoadCopyright()
        {
            caption_ = kDefaultCaption;
            copyrightLog_ = kDefaultCopyrightLog;
            const std::wstring copyrightPath = ExistingConfigPath(applicationDirectory_, kCopyrightName);
            if (!unrealdbg_native::FileExists(copyrightPath))
            {
                log_.Info(L"未找到 copyright.db（已检查 Config 和旧版根目录）；该文件是可选版权配置，使用内置标题和日志前缀");
                return;
            }

            if (!encryptionApi_.Load(applicationDirectory_))
            {
                log_.Error(L"copyright.db 已找到，但无法加载 D-encryption.dll；使用内置标题和日志前缀", ERROR_MOD_NOT_FOUND);
                return;
            }

            std::wstring decryptedText;
            if (!encryptionApi_.DecryptFile(copyrightPath, kCopyrightKey, decryptedText))
            {
                log_.Error(L"copyright.db 解密失败；使用内置标题和日志前缀", GetLastError());
                return;
            }

            std::wstring parsedCaption;
            std::wstring parsedLog;
            // 兼容仓库中现有 copyright.db 使用的旧键名“虚幻调试器标题”，
            // 同时支持新格式的“版权来源”，避免文件明明存在却被误报为损坏。
            const bool hasCaption = unrealdbg_native::ExtractJsonString(decryptedText, L"版权来源", parsedCaption) ||
                unrealdbg_native::ExtractJsonString(decryptedText, L"虚幻调试器标题", parsedCaption);
            if (!hasCaption || !unrealdbg_native::ExtractJsonString(decryptedText, L"QQ群日志", parsedLog) ||
                parsedCaption.empty() || parsedLog.empty())
            {
                log_.Error(L"copyright.db JSON 缺少非空的“版权来源/虚幻调试器标题”或“QQ群日志”；使用内置值", ERROR_INVALID_DATA);
                return;
            }
            caption_ = std::move(parsedCaption);
            copyrightLog_ = std::move(parsedLog);
            log_.Info(L"copyright.db 已解密并通过校验（兼容旧版标题字段）");
            log_.Info(L"版权日志前缀：" + copyrightLog_);
        }

        void UpdateSystemInformation()
        {
            const std::wstring osCaption = unrealdbg_native::QueryWmiString(L"Win32_OperatingSystem", L"Caption", log_);
            const std::wstring osVersion = unrealdbg_native::QueryWmiString(L"Win32_OperatingSystem", L"Version", log_);
            const std::wstring addressWidth = unrealdbg_native::QueryWmiString(L"Win32_Processor", L"AddressWidth", log_);
            const std::wstring cpu = unrealdbg_native::QueryWmiString(L"Win32_Processor", L"Name", log_);
            const std::wstring systemName = osCaption.empty() ? L"不可用" : osCaption;
            const std::wstring systemVersion = osVersion.empty() ? L"不可用" : osVersion;
            const std::wstring architecture = addressWidth.empty() ? L"未知位数" : addressWidth + L" 位";

            SetWindowTextW(systemName_, (L"系统名称：" + systemName + L" " + systemVersion + L"  " + architecture).c_str());
            SetWindowTextW(cpuName_, (L"CPU 型号：" + (cpu.empty() ? L"不可用" : cpu)).c_str());

            wchar_t systemDirectory[MAX_PATH]{};
            const UINT directoryLength = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
            if (directoryLength == 0 || directoryLength >= std::size(systemDirectory))
            {
                log_.Error(L"获取系统目录 GetSystemDirectoryW", GetLastError());
                SetWindowTextW(systemVersion_, L"系统版本：ntoskrnl.exe 版本不可用");
                return;
            }
            const std::wstring kernelPath = (std::filesystem::path(systemDirectory) / L"ntoskrnl.exe").wstring();
            const std::wstring kernelVersion = unrealdbg_native::GetFileVersionString(kernelPath, log_);
            SetWindowTextW(systemVersion_, (L"系统版本：ntoskrnl.exe  " + (kernelVersion.empty() ? L"不可用" : kernelVersion)).c_str());
        }

        void LoadDebuggerList()
        {
            debuggerEntries_.clear();
            const std::wstring iniPath = ExistingConfigPath(applicationDirectory_, kDebuggerIniName);
            if (!unrealdbg_native::FileExists(iniPath))
            {
                log_.Info(L"未找到 DebuggerList.ini；当前没有已添加的调试器，首次添加时会自动创建");
                RefreshDebuggerListView();
                return;
            }

            auto readSection = [this, &iniPath](const std::wstring& section)
            {
                unsigned long count{};
                const std::wstring countText = ReadIniString(iniPath, section.c_str(), L"Count");
                if (!ParseUnsigned(countText, count) || count > 1024)
                {
                    return false;
                }
                for (unsigned long index = 0; index < count; ++index)
                {
                    const std::wstring key = L"Debugger" + std::to_wstring(index);
                    const std::wstring serialized = ReadIniString(iniPath, section.c_str(), key.c_str());
                    DebuggerEntry entry;
                    if (serialized.empty())
                    {
                        log_.Error(L"DebuggerList 条目缺失：[" + section + L"] " + key, ERROR_FILE_NOT_FOUND);
                        continue;
                    }
                    if (!unrealdbg_native::ParseDebuggerEntry(serialized, entry))
                    {
                        log_.Error(L"DebuggerList 条目 name&path 格式无效：[" + section + L"] " + key, ERROR_INVALID_DATA);
                        continue;
                    }
                    if (ContainsDebugger(debuggerEntries_, entry))
                    {
                        log_.Error(L"DebuggerList 条目重复，已忽略：" + entry.path, ERROR_DUPLICATE_TAG);
                        continue;
                    }
                    debuggerEntries_.push_back(std::move(entry));
                }
                return true;
            };

            const std::wstring nativeCount = ReadIniString(iniPath, L"DebuggerList", L"Count");
            if (!nativeCount.empty())
            {
                if (!readSection(L"DebuggerList"))
                {
                    log_.Error(L"[DebuggerList] Count 无效，该节未加载任何调试器条目", ERROR_INVALID_DATA);
                }
            }
            else
            {
                bool foundLegacySection = false;
                for (const std::wstring& section : ReadIniSectionNames(iniPath))
                {
                    if (ReadIniString(iniPath, section.c_str(), L"Count").empty())
                    {
                        continue;
                    }
                    if (readSection(section))
                    {
                        log_.Info(L"已加载旧版调试器配置节：" + section);
                        foundLegacySection = true;
                        break;
                    }
                }
                if (!foundLegacySection)
                {
                    log_.Info(L"DebuggerList.ini 不包含有效的调试器配置节");
                }
            }
            RefreshDebuggerListView();
            log_.Info(L"调试器列表已加载：" + std::to_wstring(debuggerEntries_.size()) + L" 项");
        }

        [[nodiscard]] bool SaveDebuggerList()
        {
            if (!EnsureConfigDirectory(applicationDirectory_, log_))
            {
                return false;
            }
            const std::wstring iniPath = PathInConfigDirectory(applicationDirectory_, kDebuggerIniName);
            // 只替换原生配置节；保留其他用户节和旧版节作为可恢复备份。
            if (!WritePrivateProfileStringW(L"DebuggerList", nullptr, nullptr, iniPath.c_str()))
            {
                log_.Error(L"清理 DebuggerList 配置节 WritePrivateProfileStringW", GetLastError());
                return false;
            }
            if (!WritePrivateProfileStringW(L"DebuggerList", L"Count", std::to_wstring(debuggerEntries_.size()).c_str(), iniPath.c_str()))
            {
                log_.Error(L"写入 DebuggerList 条目数量 WritePrivateProfileStringW", GetLastError());
                return false;
            }
            for (size_t index = 0; index < debuggerEntries_.size(); ++index)
            {
                const std::wstring key = L"Debugger" + std::to_wstring(index);
                const std::wstring value = unrealdbg_native::SerializeDebuggerEntry(debuggerEntries_[index]);
                if (!WritePrivateProfileStringW(L"DebuggerList", key.c_str(), value.c_str(), iniPath.c_str()))
                {
                    log_.Error(L"写入 DebuggerList 条目 " + key + L"（WritePrivateProfileStringW）", GetLastError());
                    return false;
                }
            }
            log_.Info(L"调试器列表已保存：" + std::to_wstring(debuggerEntries_.size()) + L" 项");
            return true;
        }

        void RefreshDebuggerListView() const noexcept
        {
            ListView_DeleteAllItems(debuggerList_);
            for (size_t index = 0; index < debuggerEntries_.size(); ++index)
            {
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = static_cast<int>(index);
                const std::wstring number = std::to_wstring(index + 1);
                item.pszText = const_cast<LPWSTR>(number.c_str());
                const int itemIndex = ListView_InsertItem(debuggerList_, &item);
                if (itemIndex >= 0)
                {
                    ListView_SetItemText(debuggerList_, itemIndex, 1, const_cast<LPWSTR>(debuggerEntries_[index].name.c_str()));
                    ListView_SetItemText(debuggerList_, itemIndex, 2, const_cast<LPWSTR>(debuggerEntries_[index].path.c_str()));
                }
            }
        }

        void LoadTlSettings()
        {
            const std::wstring iniPath = ExistingConfigPath(applicationDirectory_, kConfigIniName);
            tlEnabled_ = ReadIniBoolean(iniPath, L"TL", L"enabled_tl_confrontation", false);
            tlGetTickCount_ = ReadIniBoolean(iniPath, L"TL", L"handler_gettickcount_check", false);
            tlBlockResumeThread_ = ReadIniBoolean(iniPath, L"TL", L"BlockResumeThread", false);
            SendMessageW(tlEnabledCheckbox_, BM_SETCHECK, tlEnabled_ ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(tlGetTickCountCheckbox_, BM_SETCHECK, tlGetTickCount_ ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(tlBlockResumeCheckbox_, BM_SETCHECK, tlBlockResumeThread_ ? BST_CHECKED : BST_UNCHECKED, 0);
            log_.Info(L"TL 设置已加载：启用=" + std::to_wstring(tlEnabled_) + L"，GetTickCount=" +
                std::to_wstring(tlGetTickCount_) + L"，BlockResumeThread=" + std::to_wstring(tlBlockResumeThread_));
        }

        bool SaveTlSettings()
        {
            if (!EnsureConfigDirectory(applicationDirectory_, log_))
            {
                return false;
            }
            const std::wstring iniPath = PathInConfigDirectory(applicationDirectory_, kConfigIniName);
            const bool enabledWritten = WriteIniBoolean(iniPath, L"TL", L"enabled_tl_confrontation", tlEnabled_);
            const bool tickWritten = WriteIniBoolean(iniPath, L"TL", L"handler_gettickcount_check", tlGetTickCount_);
            const bool blockWritten = WriteIniBoolean(iniPath, L"TL", L"BlockResumeThread", tlBlockResumeThread_);
            if (!enabledWritten || !tickWritten || !blockWritten)
            {
                log_.Error(L"Config.ini 无法保存一个或多个 TL 设置", GetLastError());
                return false;
            }
            log_.Info(L"TL 设置已保存");
            return true;
        }

        void HandleCommand(const int identifier, const int notificationCode)
        {
            if (notificationCode == BN_CLICKED)
            {
                switch (identifier)
                {
                case IdInitialize:
                    InitializeBackend();
                    return;
                case IdTlEnabled:
                    tlEnabled_ = SendMessageW(tlEnabledCheckbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    if (!tlEnabled_)
                    {
                        tlTickWorker_.Stop();
                    }
                    SaveTlSettings();
                    return;
                case IdTlGetTickCount:
                    tlGetTickCount_ = SendMessageW(tlGetTickCountCheckbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    if (!tlGetTickCount_)
                    {
                        tlTickWorker_.Stop();
                    }
                    SaveTlSettings();
                    return;
                case IdTlBlockResumeThread:
                    tlBlockResumeThread_ = SendMessageW(tlBlockResumeCheckbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    SaveTlSettings();
                    return;
                case IdTargetPicker:
                    // 鼠标激活由子类处理，因此用户可以在桌面上拖动。同时处理常规
                    // 按钮通知，使键盘激活（空格/回车和无障碍工具）也能工作。
                    (void)BeginTargetSelection();
                    return;
                }
            }

            switch (identifier)
            {
            case IdMenuAddDebugger:
                AddDebugger();
                break;
            case IdMenuDeleteDebugger:
                DeleteSelectedDebugger();
                break;
            case IdMenuStartDebugger:
                StartSelectedDebugger();
                break;
            }
        }

        void InitializeBackend()
        {
            if (!authorized_)
            {
                log_.Error(L"VT 初始化被拒绝：授权不可用", ERROR_ACCESS_DENIED);
                const std::wstring message = BuildInitializationFailureMessage(ERROR_ACCESS_DENIED);
                MessageBoxW(window_, message.c_str(), L"初始化失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            if (!nativeApiLoaded_)
            {
                log_.Error(L"VT 初始化被拒绝：UnrealDbgProDll.dll 不可用", ERROR_DLL_NOT_FOUND);
                const std::wstring message = BuildInitializationFailureMessage(ERROR_DLL_NOT_FOUND);
                MessageBoxW(window_, message.c_str(), L"初始化失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }

            log_.Info(L"用户请求初始化 VT 后端");
            unrealdbg_native::WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_,
                L"Win32 用户请求进入 VT 前");
            const VirtualizationEnvironment virtualization = DetectVirtualizationEnvironment();
            if ((virtualization.hypervisorPresent || virtualization.vbsEnabled || virtualization.hvciEnabled) &&
                !IsDriverServiceRunning(L"VT_Driver"))
            {
                log_.Error(L"VT 初始化已停止：Hyper-V/VBS 已占用 VMX 根模式，VT_Driver 无法再启动",
                    ERROR_NOT_SUPPORTED);
                unrealdbg_native::WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_,
                    L"Win32 虚拟化环境阻止 VT 初始化后");
                log_.Info(L"这是虚拟化资源冲突，不是缺少 \"VT_Driver\" 设备；请在测试机关闭 Hyper-V、VBS 和内存完整性后重启，或使用已经由本程序配套的 VT 实例");
                MessageBoxW(window_,
                    L"检测到 Hyper-V/VBS 正在占用处理器 VMX 根模式，VT_Driver 无法启动。\r\n\r\n"
                    L"这不是缺少虚拟设备：VT_Driver 是 VMX/VMM 核心，用户态设备由 DbgkSysWin11 创建。\r\n\r\n"
                    L"请在测试环境关闭 Hyper-V、VBS 和“内存完整性”，重启后再试；正式环境请使用兼容的签名和虚拟化方案。",
                    L"VT 初始化失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            log_.Info(L"VT 初始化阶段 1/4：正在初始化驱动设备");
            log_.Info(L"VT 初始化阶段 2/4：正在加载符号表并进行驱动握手");
            log_.Info(L"符号表详细日志前缀为“[后端]”，失败时会同时显示错误码、原因和解决方案");
            const bool initialized = nativeApi_.Initialize(0x9dd14d00f5dd71bdULL);
            const DWORD initializationError = initialized ? ERROR_SUCCESS : GetLastError();
            // DLL 在 Initialize 运行时将其详细的符号/IOCTL 诊断写入自己的文件。
            // 在显示任何失败对话框之前拉取它们，使用户能首先看到具体原因。
            backendLogTail_.Poll(log_);
            if (!initialized)
            {
                DWORD diagnosticError = initializationError;
                if (diagnosticError == ERROR_GEN_FAILURE || diagnosticError == ERROR_SERVICE_EXISTS ||
                    diagnosticError == ERROR_SERVICE_SPECIFIC_ERROR)
                {
                    const DWORD serviceError = QueryDriverServiceFailure(L"VT_Driver");
                    if (serviceError != ERROR_SUCCESS)
                    {
                        diagnosticError = serviceError;
                        log_.Error(L"VT_Driver 服务已停止，已从服务退出状态还原具体错误码", serviceError);
                    }
                }
                log_.Error(L"VT 后端初始化失败；已保留初始化按钮，请根据上方错误原因修复后重试", diagnosticError);
                unrealdbg_native::WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_,
                    L"Win32 VT 初始化失败后");
                const std::wstring failureMessage = BuildInitializationFailureMessage(diagnosticError);
                MessageBoxW(window_, failureMessage.c_str(), L"初始化失败", MB_OK | MB_ICONERROR);
                return;
            }
            serviceStarted_ = true;
            EnableWindow(initializeButton_, FALSE);
            log_.Info(L"VT 初始化阶段 3/4：符号表握手成功");
            log_.Info(L"VT 初始化阶段 4/4：驱动设备和符号表初始化完成");
            log_.Info(L"VT 后端初始化成功");
            unrealdbg_native::WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_,
                L"Win32 VT 初始化成功后");
        }

        void ShowDebuggerContextMenu(const LPARAM lParam)
        {
            const HMENU menu = CreatePopupMenu();
            if (menu == nullptr)
            {
                log_.Error(L"创建调试器右键菜单 CreatePopupMenu", GetLastError());
                return;
            }
            AppendMenuW(menu, MF_STRING, IdMenuStartDebugger, L"启动");
            AppendMenuW(menu, MF_STRING, IdMenuAddDebugger, L"添加");
            AppendMenuW(menu, MF_STRING, IdMenuDeleteDebugger, L"移除");
            if (GetSelectedDebuggerIndex() < 0)
            {
                EnableMenuItem(menu, IdMenuStartDebugger, MF_BYCOMMAND | MF_GRAYED);
                EnableMenuItem(menu, IdMenuDeleteDebugger, MF_BYCOMMAND | MF_GRAYED);
            }

            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (point.x == -1 && point.y == -1)
            {
                GetCursorPos(&point);
            }
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
            DestroyMenu(menu);
        }

        [[nodiscard]] int GetSelectedDebuggerIndex() const noexcept
        {
            return ListView_GetNextItem(debuggerList_, -1, LVNI_SELECTED);
        }

        void AddDebugger()
        {
            std::vector<wchar_t> filePath(32768, L'\0');
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = window_;
            dialog.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0\0";
            dialog.lpstrFile = filePath.data();
            dialog.nMaxFile = static_cast<DWORD>(filePath.size());
            dialog.lpstrTitle = L"请选择调试器";
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            if (!GetOpenFileNameW(&dialog))
            {
                const DWORD error = CommDlgExtendedError();
                if (error != 0)
                {
                    log_.Error(L"GetOpenFileNameW", error);
                }
                return;
            }

            const std::filesystem::path path(filePath.data());
            DebuggerEntry entry{ path.filename().wstring(), path.wstring() };
            if (entry.name.empty() || entry.path.empty() || !unrealdbg_native::FileExists(entry.path))
            {
                log_.Error(L"所选调试器路径不是可用文件：" + entry.path, ERROR_FILE_NOT_FOUND);
                const std::wstring message = BuildWin32FailureMessage(L"验证调试器可执行文件路径", ERROR_FILE_NOT_FOUND);
                MessageBoxW(window_, message.c_str(), L"添加失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            if (ContainsDebugger(debuggerEntries_, entry))
            {
                log_.Error(L"所选调试器已存在于列表中：" + entry.path, ERROR_DUPLICATE_TAG);
                const std::wstring message = BuildWin32FailureMessage(L"添加调试器条目", ERROR_DUPLICATE_TAG);
                MessageBoxW(window_, message.c_str(), L"重复项", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
                return;
            }
            debuggerEntries_.push_back(std::move(entry));
            if (!SaveDebuggerList())
            {
                debuggerEntries_.pop_back();
                const DWORD error = GetLastError();
                log_.Error(L"未添加调试器：DebuggerList.ini 保存失败", error);
                const std::wstring message = BuildWin32FailureMessage(L"保存 DebuggerList.ini", error);
                MessageBoxW(window_, message.c_str(), L"添加失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            RefreshDebuggerListView();
        }

        void DeleteSelectedDebugger()
        {
            const int index = GetSelectedDebuggerIndex();
            if (index < 0 || static_cast<size_t>(index) >= debuggerEntries_.size())
            {
                log_.Error(L"移除调试器请求无效：没有选中有效条目", ERROR_INVALID_PARAMETER);
                return;
            }
            if (MessageBoxW(window_, L"确定要移除选中的调试器吗？", L"确认移除", MB_YESNO | MB_ICONQUESTION) != IDYES)
            {
                return;
            }
            const DebuggerEntry removed = debuggerEntries_[static_cast<size_t>(index)];
            debuggerEntries_.erase(debuggerEntries_.begin() + index);
            if (!SaveDebuggerList())
            {
                debuggerEntries_.insert(debuggerEntries_.begin() + index, removed);
                const DWORD error = GetLastError();
                log_.Error(L"调试器移除已回滚：DebuggerList.ini 保存失败", error);
                const std::wstring message = BuildWin32FailureMessage(L"保存 DebuggerList.ini（移除回滚）", error);
                MessageBoxW(window_, message.c_str(), L"移除失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            RefreshDebuggerListView();
        }

        void StartSelectedDebugger()
        {
            const int index = GetSelectedDebuggerIndex();
            if (index < 0 || static_cast<size_t>(index) >= debuggerEntries_.size())
            {
                log_.Error(L"启动调试器请求无效：没有选中有效条目", ERROR_INVALID_PARAMETER);
                return;
            }
            const DebuggerEntry& entry = debuggerEntries_[static_cast<size_t>(index)];
            if (!unrealdbg_native::FileExists(entry.path))
            {
                log_.Error(L"调试器可执行文件已不存在：" + entry.path, ERROR_FILE_NOT_FOUND);
                const std::wstring message = BuildWin32FailureMessage(L"验证调试器可执行文件", ERROR_FILE_NOT_FOUND);
                MessageBoxW(window_, message.c_str(), L"启动失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            if (!nativeApiLoaded_)
            {
                log_.Error(L"调试器启动被拒绝：UnrealDbgProDll.dll 不可用", ERROR_DLL_NOT_FOUND);
                const std::wstring message = BuildWin32FailureMessage(L"加载 UnrealDbgProDll.dll", ERROR_DLL_NOT_FOUND);
                MessageBoxW(window_, message.c_str(), L"启动失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            if (!nativeApi_.StartProcess(entry.path, applicationDirectory_))
            {
                const DWORD error = GetLastError();
                log_.Error(L"调试器启动请求失败：" + entry.path, error);
                const std::wstring message = BuildWin32FailureMessage(L"启动调试器进程", error);
                MessageBoxW(window_, message.c_str(), L"启动失败", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return;
            }
            log_.Info(L"调试器启动请求已接受：" + entry.path);
        }

        void UpdateTargetSelection()
        {
            POINT point{};
            if (!GetCursorPos(&point))
            {
                log_.Error(L"获取目标选择时调用 GetCursorPos", GetLastError());
                return;
            }
            HideTargetOverlay();
            const HWND candidate = WindowFromPoint(point);
            if (candidate == nullptr)
            {
                return;
            }
            DWORD processId{};
            if (GetWindowThreadProcessId(candidate, &processId) == 0)
            {
                log_.Error(L"获取目标选择时调用 GetWindowThreadProcessId", GetLastError());
                return;
            }
            if (processId == GetCurrentProcessId())
            {
                selectedTargetWindow_ = nullptr;
                selectedTargetProcessId_ = 0;
                return;
            }
            if (candidate != selectedTargetWindow_)
            {
                selectedTargetWindow_ = candidate;
                selectedTargetProcessId_ = processId;
                SetWindowTextW(tlTargetStatus_, (L"候选目标 PID: " + std::to_wstring(processId)).c_str());
            }
            // 覆盖层在 WindowFromPoint 之前被故意隐藏，因此它不能选择自己。
            // 即使光标停留在同一目标窗口上，也要恢复它。
            ShowTargetOverlay(candidate);
        }

        void FinishTargetSelection()
        {
            const DWORD processId = selectedTargetProcessId_;
            targetSelectionActive_ = false;
            if (GetCapture() == window_)
            {
                ReleaseCapture();
            }
            HideTargetOverlay();
            selectedTargetWindow_ = nullptr;
            selectedTargetProcessId_ = 0;
            if (processId == 0)
            {
                SetWindowTextW(tlTargetStatus_, L"未选择目标进程");
                log_.Info(L"TL 目标选择已取消：未选中目标");
                return;
            }
            if (MessageBoxW(window_, (L"确认将 PID " + std::to_wstring(processId) + L" 作为 TL 目标？").c_str(),
                L"确认目标", MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL) != IDYES)
            {
                SetWindowTextW(tlTargetStatus_, L"未确认目标进程");
                log_.Info(L"用户未确认 TL 目标选择");
                return;
            }
            targetProcessId_ = processId;
            SetWindowTextW(tlTargetStatus_, (L"当前目标 PID: " + std::to_wstring(targetProcessId_)).c_str());
            log_.Info(L"[TL.exe] 已选择目标 PID：" + std::to_wstring(targetProcessId_));
            ApplyTlConfrontation();
        }

        void CancelTargetSelection() noexcept
        {
            targetSelectionActive_ = false;
            if (GetCapture() == window_)
            {
                ReleaseCapture();
            }
            selectedTargetWindow_ = nullptr;
            selectedTargetProcessId_ = 0;
            HideTargetOverlay();
            SetWindowTextW(tlTargetStatus_, L"目标选择已取消");
            log_.Info(L"TL 目标选择已取消");
        }

        void ApplyTlConfrontation()
        {
            if (!tlEnabled_ || targetProcessId_ == 0)
            {
                log_.Error(L"TL 对抗请求被拒绝：功能未启用或尚未选择目标 PID", ERROR_INVALID_PARAMETER);
                return;
            }
            if (tlGetTickCount_)
            {
                log_.Info(L"[TL.exe] 启动 GetTickCount 处理前正在定位模块基址");
                const uintptr_t base = unrealdbg_native::FindRemoteModuleBase(targetProcessId_, L"TL.exe", log_);
                if (base == 0)
                {
                    log_.Error(L"[TL.exe] 未找到模块基址；请确认选中的进程是受支持版本的 TL.exe", ERROR_MOD_NOT_FOUND);
                }
                else if (base > std::numeric_limits<uintptr_t>::max() - kTlLastTickOffset)
                {
                    SetLastError(ERROR_ARITHMETIC_OVERFLOW);
                    log_.Error(L"[TL.exe] 模块基址加固定偏移发生溢出，已拒绝写入目标内存", ERROR_ARITHMETIC_OVERFLOW);
                }
                else if (tlTickWorker_.Start(targetProcessId_, base + kTlLastTickOffset))
                {
                    log_.Info(L"[TL.exe] GetTickCount 工作线程已在校验后的固定偏移上启动");
                }
            }
            else
            {
                tlTickWorker_.Stop();
            }
            if (tlBlockResumeThread_)
            {
                if (!nativeApiLoaded_)
                {
                    log_.Error(L"[TL.exe] 无法阻止恢复线程：UnrealDbgProDll.dll 不可用", ERROR_DLL_NOT_FOUND);
                }
                else if (nativeApi_.BlockGameResumeThread(targetProcessId_))
                {
                    log_.Info(L"[TL.exe] 阻止恢复线程请求成功");
                }
            }
        }

        [[nodiscard]] bool EnsureTargetOverlay()
        {
            if (targetOverlay_ != nullptr)
            {
                return true;
            }
            targetOverlay_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                kOverlayWindowClass, L"", WS_POPUP, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
            if (targetOverlay_ == nullptr)
            {
                log_.Error(L"创建目标选择覆盖窗口 CreateWindowExW", GetLastError());
                return false;
            }
            if (!SetLayeredWindowAttributes(targetOverlay_, RGB(0, 0, 0), 0, LWA_COLORKEY))
            {
                log_.Error(L"设置目标选择覆盖窗口属性 SetLayeredWindowAttributes", GetLastError());
                DestroyWindow(targetOverlay_);
                targetOverlay_ = nullptr;
                return false;
            }
            return true;
        }

        void ShowTargetOverlay(const HWND target) noexcept
        {
            if (!EnsureTargetOverlay())
            {
                return;
            }
            RECT rectangle{};
            if (!GetWindowRect(target, &rectangle))
            {
                log_.Error(L"获取目标选择窗口矩形 GetWindowRect", GetLastError());
                return;
            }
            SetWindowPos(targetOverlay_, HWND_TOPMOST, rectangle.left, rectangle.top, rectangle.right - rectangle.left,
                rectangle.bottom - rectangle.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }

        void HideTargetOverlay() noexcept
        {
            if (targetOverlay_ != nullptr)
            {
                ShowWindow(targetOverlay_, SW_HIDE);
            }
        }

        void Shutdown() noexcept
        {
            if (shutDown_)
            {
                return;
            }
            shutDown_ = true;
            // 先停止新的 PrintLog 调用，再卸载后端 DLL，避免 DLL 线程在
            // 日志接收器销毁后仍访问悬空指针。
            g_activeLogSink.store(nullptr, std::memory_order_release);
            if (window_ != nullptr)
            {
                KillTimer(window_, kLogTimerId);
            }
            if (targetSelectionActive_)
            {
                targetSelectionActive_ = false;
                if (GetCapture() == window_)
                {
                    ReleaseCapture();
                }
            }
            tlTickWorker_.Stop();
            if (targetOverlay_ != nullptr)
            {
                DestroyWindow(targetOverlay_);
                targetOverlay_ = nullptr;
            }
            log_.Info(L"=== UnrealDbgNative 关闭 ===");
            log_.DrainToRichEdit();
            log_.DetachRichEdit();
            encryptionApi_.Unload();
            nativeApi_.Unload();
            if (logFont_ != nullptr && logFont_ != mainFont_)
            {
                DeleteObject(logFont_);
                logFont_ = nullptr;
            }
        }

        static LRESULT CALLBACK TargetPickerSubclass(const HWND window, const UINT message, const WPARAM wParam,
            const LPARAM lParam, const UINT_PTR, const DWORD_PTR referenceData)
        {
            auto* application = reinterpret_cast<Application*>(referenceData);
            if (message == WM_LBUTTONDOWN && application != nullptr)
            {
                (void)application->BeginTargetSelection();
                return 0;
            }
            return DefSubclassProc(window, message, wParam, lParam);
        }

        HINSTANCE instance_{};
        std::wstring applicationDirectory_;
        LogSink log_;
        NativeApi nativeApi_;
        EncryptionApi encryptionApi_;
        TlTickWorker tlTickWorker_;
        BackendLogTail backendLogTail_;
        HWND window_{};
        HWND systemName_{};
        HWND systemVersion_{};
        HWND cpuName_{};
        HWND initializeButton_{};
        HWND tabControl_{};
        HWND debuggerList_{};
        HWND tlEnabledCheckbox_{};
        HWND tlGetTickCountCheckbox_{};
        HWND tlBlockResumeCheckbox_{};
        HWND tlWarning_{};
        HWND tlOptionsGroup_{};
        HWND tlTargetGroup_{};
        HWND tlTargetStatus_{};
        HWND targetPickerButton_{};
        HWND logView_{};
        HWND targetOverlay_{};
        HFONT mainFont_{};
        HFONT logFont_{};
        std::vector<DebuggerEntry> debuggerEntries_;
        std::wstring caption_;
        std::wstring copyrightLog_;
        DWORD targetProcessId_{};
        DWORD selectedTargetProcessId_{};
        HWND selectedTargetWindow_{};
        bool nativeApiLoaded_{};
        bool authorized_{};
        bool serviceStarted_{};
        bool tlEnabled_{};
        bool tlGetTickCount_{};
        bool tlBlockResumeThread_{};
        bool targetSelectionActive_{};
        bool shutDown_{};
    };

    LRESULT CALLBACK OverlayWindowProcedure(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            const HDC hdc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            const HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
            const HBRUSH green = CreateSolidBrush(RGB(0, 220, 90));
            FillRect(hdc, &client, black);
            constexpr int border = 3;
            RECT top{ client.left, client.top, client.right, std::min(client.bottom, client.top + border) };
            RECT bottom{ client.left, std::max(client.top, client.bottom - border), client.right, client.bottom };
            RECT left{ client.left, client.top, std::min(client.right, client.left + border), client.bottom };
            RECT right{ std::max(client.left, client.right - border), client.top, client.right, client.bottom };
            FillRect(hdc, &top, green);
            FillRect(hdc, &bottom, green);
            FillRect(hdc, &left, green);
            FillRect(hdc, &right, green);
            DeleteObject(green);
            DeleteObject(black);
            EndPaint(window, &paint);
            return 0;
        }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindowProcedure(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            application = static_cast<Application*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
            if (application != nullptr)
            {
                application->SetWindowHandle(window);
            }
        }
        if (application == nullptr)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        try
        {
            const LRESULT result = application->HandleMessage(message, wParam, lParam);
            if (message == WM_NCDESTROY)
            {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            }
            return result;
        }
        catch (const std::exception& error)
        {
            OutputDebugStringA(error.what());
            MessageBoxW(window, L"原生界面遇到未处理异常；请查看日志后重新启动。", L"UnrealDbgNative 错误", MB_OK | MB_ICONERROR);
            return 0;
        }
        catch (...)
        {
            MessageBoxW(window, L"原生界面遇到未知异常；请查看日志后重新启动。", L"UnrealDbgNative 错误", MB_OK | MB_ICONERROR);
            return 0;
        }
    }

    [[nodiscard]] bool RegisterWindowClasses(const HINSTANCE instance)
    {
        WNDCLASSEXW mainClass{};
        mainClass.cbSize = sizeof(mainClass);
        mainClass.hInstance = instance;
        mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        mainClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        mainClass.lpszClassName = kMainWindowClass;
        mainClass.lpfnWndProc = MainWindowProcedure;
        if (RegisterClassExW(&mainClass) == 0)
        {
            return false;
        }

        WNDCLASSEXW overlayClass{};
        overlayClass.cbSize = sizeof(overlayClass);
        overlayClass.hInstance = instance;
        overlayClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        overlayClass.lpszClassName = kOverlayWindowClass;
        overlayClass.lpfnWndProc = OverlayWindowProcedure;
        if (RegisterClassExW(&overlayClass) == 0)
        {
            UnregisterClassW(kMainWindowClass, instance);
            return false;
        }
        return true;
    }
}

// 与旧 Delphi 前端保持 ABI 兼容。AIHelper.dll 会通过 GetProcAddress(\"PrintLog\")
// 查找该入口；如果找不到，它会显示旧版 ANSI 乱码弹窗。所有文本在这里
// 转为宽字符串后进入 Native 日志系统，不再弹出插件自带的阻塞式提示框。
extern "C" __declspec(dllexport) void __stdcall PrintLog(TCHAR* text)
{
    unrealdbg_native::LogSink* sink = g_activeLogSink.load(std::memory_order_acquire);
    if (sink == nullptr)
    {
        OutputDebugStringW(L"[UnrealDbgNative] PrintLog 收到消息，但日志接收器尚未就绪\n");
        return;
    }

    if (text == nullptr)
    {
        sink->Error(L"兼容日志入口 PrintLog 收到空指针", ERROR_INVALID_PARAMETER);
        return;
    }

    // 限制扫描长度，避免损坏插件传入未终止字符串时越界读取。
    constexpr size_t kMaximumPrintLogLength = 32768;
    const size_t length = wcsnlen_s(text, kMaximumPrintLogLength);
    if (length == 0)
    {
        return;
    }
    if (length >= kMaximumPrintLogLength)
    {
        sink->Error(L"兼容日志入口 PrintLog 消息超过 32767 个字符，已拒绝接收", ERROR_BUFFER_OVERFLOW);
        return;
    }

    const std::wstring message(text, length);
    const PrintLogFailure diagnosis = DiagnosePrintLogMessage(message);
    const std::wstring forwarded = L"[兼容 PrintLog] " + LocalizeLegacyPrintLog(message);

    if (diagnosis.indicatesFailure)
    {
        // 先保留 AIHelper 的原始文本，再追加结构化原因和解决方案。
        // 这样既不会隐藏原始错误，也不会再把“error:577”这类旧格式
        // 当成普通信息显示为绿色。
        sink->External(unrealdbg_native::LogLevel::Error, forwarded);
        const DWORD errorCode = diagnosis.errorCode.value_or(ERROR_GEN_FAILURE);
        sink->Error(L"AIHelper.dll 通过 PrintLog 报告操作失败", errorCode);
    }
    else if (message.find(L"[DEBUG]") != std::wstring::npos || message.find(L"[调试]") != std::wstring::npos)
    {
        sink->External(unrealdbg_native::LogLevel::Debug, forwarded);
    }
    else
    {
        sink->External(unrealdbg_native::LogLevel::Info, forwarded);
    }
}

int APIENTRY wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    if (HasCommandLineArgument(L"--self-test"))
    {
        std::wstring failure;
        if (!unrealdbg_native::RunCoreSelfTests(failure))
        {
            OutputDebugStringW((L"UnrealDbgNative 自测失败：" + failure + L"\n").c_str());
            return 2;
        }
        OutputDebugStringW(L"UnrealDbgNative 自测通过\n");
        return 0;
    }

    // 构建/诊断管道使用的无头集成检查。它执行与按钮流程相同的 DLL 加载、
    // AIHelper PrintLog 查找、驱动请求和错误传播，而不创建窗口。
    if (HasCommandLineArgument(L"--backend-self-test"))
    {
        const std::wstring applicationDirectory = unrealdbg_native::GetApplicationDirectory();
        if (applicationDirectory.empty())
        {
            return 2;
        }
        LogSink log(applicationDirectory);
        g_activeLogSink.store(&log, std::memory_order_release);
        NativeApi api(log);
        const bool loaded = api.Load(applicationDirectory);
        const bool initialized = loaded && api.Initialize(0x9dd14d00f5dd71bdULL);
        if (initialized)
        {
            api.Unload();
        }
        g_activeLogSink.store(nullptr, std::memory_order_release);
        return initialized ? 0 : 1;
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&controls))
    {
        const std::wstring message = BuildWin32FailureMessage(L"初始化 Windows 公共控件 InitCommonControlsEx", GetLastError());
        MessageBoxW(nullptr, message.c_str(), L"UnrealDbgNative", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return 1;
    }
    if (LoadLibraryW(L"Msftedit.dll") == nullptr)
    {
        const std::wstring message = BuildWin32FailureMessage(L"加载 Msftedit.dll（日志控件）", GetLastError());
        MessageBoxW(nullptr, message.c_str(), L"UnrealDbgNative", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return 1;
    }
    if (!RegisterWindowClasses(instance))
    {
        const std::wstring message = BuildWin32FailureMessage(L"注册原生窗口类 RegisterClassExW", GetLastError());
        MessageBoxW(nullptr, message.c_str(), L"UnrealDbgNative", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return 1;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitializeCom = SUCCEEDED(comResult);
    const std::wstring applicationDirectory = unrealdbg_native::GetApplicationDirectory();
    if (applicationDirectory.empty())
    {
        if (shouldUninitializeCom)
        {
            CoUninitialize();
        }
        const std::wstring message = BuildWin32FailureMessage(L"确定程序目录 GetModuleFileNameW", ERROR_PATH_NOT_FOUND);
        MessageBoxW(nullptr, message.c_str(), L"UnrealDbgNative", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return 1;
    }

    Application application(instance, applicationDirectory);
    RECT desiredClient{ 0, 0, 882, 510 };
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&desiredClient, style, FALSE, 0);
    const HWND window = CreateWindowExW(0, kMainWindowClass, kDefaultCaption, style,
        CW_USEDEFAULT, CW_USEDEFAULT, desiredClient.right - desiredClient.left, desiredClient.bottom - desiredClient.top,
        nullptr, nullptr, instance, &application);
    if (window == nullptr)
    {
        if (shouldUninitializeCom)
        {
            CoUninitialize();
        }
        const std::wstring message = BuildWin32FailureMessage(L"创建主窗口 CreateWindowExW", GetLastError());
        MessageBoxW(nullptr, message.c_str(), L"UnrealDbgNative", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return 1;
    }
    if (!application.Initialize())
    {
        DestroyWindow(window);
        if (shouldUninitializeCom)
        {
            CoUninitialize();
        }
        return 1;
    }

    const bool uiSmokeTest = HasCommandLineArgument(L"--ui-smoke-test");
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    if (uiSmokeTest)
    {
        // This validates real window/control creation and startup integration
        // without invoking driver initialization, injection, target selection,
        // or target-process memory writes.
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (shouldUninitializeCom)
    {
        CoUninitialize();
    }
    return static_cast<int>(message.wParam);
}
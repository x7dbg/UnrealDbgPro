#include "Core.h"
#include <winioctl.h>
#include "../Common/Shared/WindowsBuildSupport.h"
#include "../Common/Shared/IOCTLs.h"

static_assert(((IOCTL_LOAD_SYMBOLS_TABLE_V2 >> 14) & 3u) == (FILE_READ_DATA | FILE_WRITE_DATA),
    "secure symbol IOCTL must require read/write access");
static_assert(IOCTL_LOAD_SYMBOLS_TABLE_V2 != IOCTL_LOAD_SYMBOLS_TABLE,
    "secure symbol IOCTL must use a distinct function number");

#include <CommCtrl.h>
#include <Psapi.h>
#include <Richedit.h>
#include <Wbemidl.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Wbemuuid.lib")
#pragma comment(lib, "Advapi32.lib")

namespace
{
    constexpr size_t kMaximumCopyrightBytes = 1024 * 1024;

    [[nodiscard]] std::wstring GetTimePrefix()
    {
        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);

        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
            localTime.wYear, localTime.wMonth, localTime.wDay,
            localTime.wHour, localTime.wMinute, localTime.wSecond,
            localTime.wMilliseconds);
        return buffer;
    }

    [[nodiscard]] const wchar_t* GetLevelText(const unrealdbg_native::LogLevel level) noexcept
    {
        switch (level)
        {
        case unrealdbg_native::LogLevel::Debug:
            return L"调试";
        case unrealdbg_native::LogLevel::Info:
            return L"信息";
        case unrealdbg_native::LogLevel::Error:
            return L"错误";
        }
        return L"未知";
    }

    [[nodiscard]] COLORREF GetLevelColor(const unrealdbg_native::LogLevel level) noexcept
    {
        switch (level)
        {
        case unrealdbg_native::LogLevel::Debug:
            return RGB(110, 185, 255); // 调试：蓝色
        case unrealdbg_native::LogLevel::Info:
            return RGB(110, 235, 145); // 正常：绿色
        case unrealdbg_native::LogLevel::Error:
            return RGB(255, 95, 95);   // 严重错误：红色
        }
        return RGB(220, 220, 220);
    }

    // 只有用户已经在日志末尾时才自动跟随滚动；用户拖到历史位置查看时
    // 不抢回滚动条，但回到底部后会继续自动显示新日志。
    [[nodiscard]] bool IsRichEditAtBottom(const HWND richEdit) noexcept
    {
        if (!IsWindow(richEdit))
        {
            return true;
        }

        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_ALL;
        if (GetScrollInfo(richEdit, SB_VERT, &scrollInfo) == FALSE)
        {
            return true;
        }

        return scrollInfo.nPos + static_cast<int>(scrollInfo.nPage) >= scrollInfo.nMax;
    }

    [[nodiscard]] std::string ToUtf8(const std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            result.data(), required, nullptr, nullptr) != required)
        {
            return {};
        }
        return result;
    }

    [[nodiscard]] bool IsJsonWhitespace(const wchar_t value) noexcept
    {
        return value == L' ' || value == L'\t' || value == L'\r' || value == L'\n';
    }

    void SkipJsonWhitespace(const std::wstring_view input, size_t& position) noexcept
    {
        while (position < input.size() && IsJsonWhitespace(input[position]))
        {
            ++position;
        }
    }

    [[nodiscard]] bool AppendJsonUnicodeEscape(const std::wstring_view input, size_t& position, std::wstring& output)
    {
        if (position + 4 > input.size())
        {
            return false;
        }

        unsigned int codeUnit = 0;
        for (size_t index = 0; index < 4; ++index)
        {
            const wchar_t character = input[position + index];
            codeUnit <<= 4;
            if (character >= L'0' && character <= L'9')
            {
                codeUnit |= static_cast<unsigned int>(character - L'0');
            }
            else if (character >= L'a' && character <= L'f')
            {
                codeUnit |= static_cast<unsigned int>(character - L'a' + 10);
            }
            else if (character >= L'A' && character <= L'F')
            {
                codeUnit |= static_cast<unsigned int>(character - L'A' + 10);
            }
            else
            {
                return false;
            }
        }
        position += 4;

        const auto first = static_cast<wchar_t>(codeUnit);
        if (first >= 0xD800 && first <= 0xDBFF)
        {
            if (position + 6 > input.size() || input[position] != L'\\' || input[position + 1] != L'u')
            {
                return false;
            }

            position += 2;
            unsigned int lowUnit = 0;
            for (size_t index = 0; index < 4; ++index)
            {
                const wchar_t character = input[position + index];
                lowUnit <<= 4;
                if (character >= L'0' && character <= L'9')
                {
                    lowUnit |= static_cast<unsigned int>(character - L'0');
                }
                else if (character >= L'a' && character <= L'f')
                {
                    lowUnit |= static_cast<unsigned int>(character - L'a' + 10);
                }
                else if (character >= L'A' && character <= L'F')
                {
                    lowUnit |= static_cast<unsigned int>(character - L'A' + 10);
                }
                else
                {
                    return false;
                }
            }
            position += 4;
            if (lowUnit < 0xDC00 || lowUnit > 0xDFFF)
            {
                return false;
            }
            output.push_back(first);
            output.push_back(static_cast<wchar_t>(lowUnit));
            return true;
        }

        if (first >= 0xDC00 && first <= 0xDFFF)
        {
            return false;
        }
        output.push_back(first);
        return true;
    }

    [[nodiscard]] bool ParseJsonStringAt(const std::wstring_view input, size_t& position, std::wstring& output)
    {
        if (position >= input.size() || input[position] != L'"')
        {
            return false;
        }
        ++position;
        output.clear();

        while (position < input.size())
        {
            const wchar_t character = input[position++];
            if (character == L'"')
            {
                return true;
            }
            if (character < 0x20)
            {
                return false;
            }
            if (character != L'\\')
            {
                output.push_back(character);
                continue;
            }
            if (position >= input.size())
            {
                return false;
            }

            switch (input[position++])
            {
            case L'"': output.push_back(L'"'); break;
            case L'\\': output.push_back(L'\\'); break;
            case L'/': output.push_back(L'/'); break;
            case L'b': output.push_back(L'\b'); break;
            case L'f': output.push_back(L'\f'); break;
            case L'n': output.push_back(L'\n'); break;
            case L'r': output.push_back(L'\r'); break;
            case L't': output.push_back(L'\t'); break;
            case L'u':
                if (!AppendJsonUnicodeEscape(input, position, output))
                {
                    return false;
                }
                break;
            default:
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] std::wstring MakeMutableCopy(const std::wstring& value)
    {
        return value;
    }

    // 运行时依赖集中放在应用目录的 bin 子目录。为了兼容旧版发布包，
    // 只有在 bin 中确实存在至少一个本程序依赖文件时才选择 bin；否则
    // 回退到旧的根目录布局。这样升级时不会因为残留空目录而误判路径。
    [[nodiscard]] std::filesystem::path ResolveRuntimeDirectory(const std::wstring& applicationDirectory)
    {
        const std::filesystem::path root(applicationDirectory);
        const std::filesystem::path bin = root / L"bin";
        std::error_code error;
        if (std::filesystem::is_directory(bin, error) && !error)
        {
            constexpr std::wstring_view candidates[] = {
                L"UnrealDbgProDll.dll", L"D-encryption.dll", L"AIHelper.dll",
                L"Hook64.dll", L"VT_Driver.sys", L"DbgkSysWin11.sys",
                L"DbgkSysWin10.sys" };
            for (const std::wstring_view candidate : candidates)
            {
                if (std::filesystem::is_regular_file(bin / candidate, error) && !error)
                {
                    return bin;
                }
                error.clear();
            }
        }
        return root;
    }

    [[nodiscard]] std::filesystem::path ResolveRuntimeFile(const std::wstring& applicationDirectory,
        const wchar_t* fileName)
    {
        const std::filesystem::path runtime = ResolveRuntimeDirectory(applicationDirectory);
        const std::filesystem::path candidate = runtime / fileName;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error)
        {
            return candidate;
        }
        // 按文件回退也兼容部分迁移的安装环境。
        return std::filesystem::path(applicationDirectory) / fileName;
    }

    [[nodiscard]] std::wstring MakeDiagnosticSessionId()
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t value[96]{};
        swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u-%03u-pid%lu-%08llX",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            now.wMilliseconds, GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64() & 0xFFFFFFFFULL));
        return value;
    }

    void RotateLogIfOversized(const std::filesystem::path& logPath, const std::uintmax_t maximumBytes,
        const std::uintmax_t incomingBytes = 0) noexcept
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(logPath, error);
        if (error || (size <= maximumBytes && incomingBytes <= maximumBytes - size))
        {
            return;
        }

        // 轮换失败无害：我们继续追加而不是丢失诊断信息。
        // 例如另一个进程保持文件打开时，轮换会失败，但不会导致数据丢失。
        const std::filesystem::path previous = logPath.parent_path() /
            (logPath.stem().wstring() + L".previous" + logPath.extension().wstring());
        (void)MoveFileExW(logPath.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    [[nodiscard]] std::wstring FileTimeText(const FILETIME& fileTime)
    {
        SYSTEMTIME utc{};
        SYSTEMTIME local{};
        if (!FileTimeToSystemTime(&fileTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        {
            return L"不可用";
        }
        wchar_t output[64]{};
        swprintf_s(output, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
            local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
            local.wMilliseconds);
        return output;
    }

    [[nodiscard]] std::wstring FileSha256(const std::filesystem::path& filePath)
    {
        HCRYPTPROV provider{};
        HCRYPTHASH hash{};
        HANDLE file = INVALID_HANDLE_VALUE;
        std::wstring result;

        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
            !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
        {
            if (hash != 0)
            {
                CryptDestroyHash(hash);
            }
            if (provider != 0)
            {
                CryptReleaseContext(provider, 0);
            }
            return result;
        }

        file = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            CryptDestroyHash(hash);
            CryptReleaseContext(provider, 0);
            return result;
        }

        std::array<BYTE, 64 * 1024> buffer{};
        bool complete = true;
        for (;;)
        {
            DWORD read{};
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            {
                complete = false;
                break;
            }
            if (read == 0)
            {
                break;
            }
            if (!CryptHashData(hash, buffer.data(), read, 0))
            {
                complete = false;
                break;
            }
        }
        CloseHandle(file);

        std::array<BYTE, 32> bytes{};
        DWORD length = static_cast<DWORD>(bytes.size());
        if (complete && CryptGetHashParam(hash, HP_HASHVAL, bytes.data(), &length, 0) && length == bytes.size())
        {
            static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
            result.reserve(bytes.size() * 2);
            for (const BYTE value : bytes)
            {
                result.push_back(kHex[value >> 4]);
                result.push_back(kHex[value & 0x0F]);
            }
        }
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return result;
    }

    [[nodiscard]] const wchar_t* ServiceStateText(const DWORD state) noexcept
    {
        switch (state)
        {
        case SERVICE_STOPPED: return L"已停止";
        case SERVICE_START_PENDING: return L"正在启动";
        case SERVICE_STOP_PENDING: return L"正在停止";
        case SERVICE_RUNNING: return L"运行中";
        case SERVICE_CONTINUE_PENDING: return L"正在继续";
        case SERVICE_PAUSE_PENDING: return L"正在暂停";
        case SERVICE_PAUSED: return L"已暂停";
        default: return L"未知";
        }
    }

    [[nodiscard]] const wchar_t* ServiceStartTypeText(const DWORD startType) noexcept
    {
        switch (startType)
        {
        case SERVICE_BOOT_START: return L"引导启动";
        case SERVICE_SYSTEM_START: return L"系统启动";
        case SERVICE_AUTO_START: return L"自动启动";
        case SERVICE_DEMAND_START: return L"手动启动";
        case SERVICE_DISABLED: return L"已禁用";
        default: return L"未知";
        }
    }

    [[nodiscard]] const wchar_t* ArchitectureText(const WORD architecture) noexcept
    {
        switch (architecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
        default: return L"未知";
        }
    }

    [[nodiscard]] bool ReadRegistryDword(const wchar_t* subKey, const wchar_t* valueName, DWORD& value) noexcept
    {
        DWORD size = sizeof(value);
        return RegGetValueW(HKEY_LOCAL_MACHINE, subKey, valueName, RRF_RT_REG_DWORD,
            nullptr, &value, &size) == ERROR_SUCCESS;
    }

    void WriteServiceDiagnostic(const SC_HANDLE manager, const wchar_t* serviceName,
        unrealdbg_native::LogSink& log)
    {
        const SC_HANDLE service = OpenServiceW(manager, serviceName,
            SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
        if (service == nullptr)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_SERVICE_DOES_NOT_EXIST)
            {
                log.Info(L"[开发诊断快照][服务] " + std::wstring(serviceName) + L"：未注册");
            }
            else
            {
                log.Error(L"[开发诊断快照] 打开服务 " + std::wstring(serviceName), error);
            }
            return;
        }

        SERVICE_STATUS_PROCESS status{};
        DWORD returned{};
        const BOOL statusOk = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &returned);
        const DWORD statusError = statusOk ? ERROR_SUCCESS : GetLastError();

        DWORD required{};
        (void)QueryServiceConfigW(service, nullptr, 0, &required);
        std::vector<BYTE> configBuffer(required);
        const BOOL configOk = required != 0 && QueryServiceConfigW(service,
            reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configBuffer.data()), required, &required);
        const DWORD configError = configOk ? ERROR_SUCCESS : GetLastError();

        std::wostringstream output;
        output << L"[开发诊断快照][服务] 名称=" << serviceName;
        if (statusOk && returned >= sizeof(status))
        {
            output << L"；状态=" << ServiceStateText(status.dwCurrentState)
                << L"(" << status.dwCurrentState << L")"
                << L"；进程PID=" << status.dwProcessId
                << L"；Win32退出码=" << status.dwWin32ExitCode
                << L"；服务专用退出码=" << status.dwServiceSpecificExitCode
                << L"；检查点=" << status.dwCheckPoint
                << L"；等待提示=" << status.dwWaitHint;
        }
        else
        {
            output << L"；状态查询失败，错误码=" << statusError;
        }
        if (configOk)
        {
            const auto* config = reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(configBuffer.data());
            output << L"；启动类型=" << ServiceStartTypeText(config->dwStartType)
                << L"(" << config->dwStartType << L")"
                << L"；服务类型=" << config->dwServiceType
                << L"；二进制路径=" << (config->lpBinaryPathName == nullptr ? L"<空>" : config->lpBinaryPathName)
                << L"；显示名=" << (config->lpDisplayName == nullptr ? L"<空>" : config->lpDisplayName);
            log.Info(output.str());
        }
        else
        {
            output << L"；配置查询失败，错误码=" << configError;
            log.Error(output.str(), configError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : configError);
        }
        CloseServiceHandle(service);
    }

    template <typename T>
    void ReleaseCom(T*& value) noexcept
    {
        if (value != nullptr)
        {
            value->Release();
            value = nullptr;
        }
    }
}

namespace unrealdbg_native
{
    LogSink::LogSink(std::wstring applicationDirectory)
        : applicationDirectory_(std::move(applicationDirectory))
    {
        // 汇总日志便于快速查看；会话日志用于把一次测试完整固定下来，避免
        // 多次启动的记录交错后无法还原问题发生前后的状态。
        try
        {
            const std::filesystem::path logDirectory = std::filesystem::path(applicationDirectory_) / L"Log";
            const std::filesystem::path sessionsDirectory = logDirectory / L"Sessions";
            std::filesystem::create_directories(sessionsDirectory);
            RotateLogIfOversized(logDirectory / L"log.ini", 16ULL * 1024ULL * 1024ULL);
            sessionId_ = MakeDiagnosticSessionId();
            sessionLogPath_ = (sessionsDirectory / (L"UnrealDbg-" + sessionId_ + L".log")).wstring();

            const std::wstring header = L"=== UnrealDbg 开发诊断会话开始 ===\r\n会话=" + sessionId_ +
                L"\r\n进程=" + std::to_wstring(GetCurrentProcessId()) +
                L"\r\n程序目录=" + applicationDirectory_ + L"\r\n编码=UTF-8\r\n\r\n";
            const std::string utf8 = ToUtf8(header);
            std::ofstream session(sessionLogPath_, std::ios::binary | std::ios::app);
            if (session.is_open() && !utf8.empty())
            {
                session.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
                session.flush();
                sessionLogBytes_ = utf8.size();
            }
        }
        catch (...)
        {
            // 即使无法创建会话目录（例如只读包），总日志仍然可用。
            sessionId_ = L"会话目录不可用";
            sessionLogPath_.clear();
        }
    }

    void LogSink::AttachRichEdit(const HWND richEdit) noexcept
    {
        std::scoped_lock lock(mutex_);
        richEdit_ = richEdit;
    }

    void LogSink::DetachRichEdit() noexcept
    {
        std::scoped_lock lock(mutex_);
        richEdit_ = nullptr;
    }

    void LogSink::Debug(const std::wstring_view text)
    {
        Write(LogLevel::Debug, text);
    }

    void LogSink::Info(const std::wstring_view text)
    {
        Write(LogLevel::Info, text);
    }

    void LogSink::Error(const std::wstring_view text)
    {
        Write(LogLevel::Error, text);
    }

    void LogSink::External(const LogLevel level, const std::wstring_view text)
    {
        Write(level, text);
    }

    void LogSink::Error(const std::wstring_view operation, const DWORD error)
    {
        // 失败的 Win32 调用可能将 GetLastError() 留为零。绝不为失败打印"成功 (0)"，
        // 使用通用设备失败码代替。
        const DWORD diagnosticError = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
        const ErrorExplanation explanation = ExplainWin32Error(diagnosticError);
        const std::wstring systemMessage = GetSystemErrorMessage(diagnosticError);
        std::wstringstream stream;
        stream << L"操作：" << operation << L"\r\n"
            << L"错误码：" << diagnosticError << L" (0x" << std::hex << std::uppercase
            << std::setw(8) << std::setfill(L'0') << diagnosticError << std::dec << L")\r\n"
            << L"系统原文：" << systemMessage << L"\r\n"
            << L"原因：" << explanation.reason << L"\r\n"
            << L"解决方案：" << explanation.solution;
        if (diagnosticError == ERROR_GEN_FAILURE)
        {
            stream << L"\r\n补充诊断：错误码 31（ERROR_GEN_FAILURE）是通用失败码，不能单独作为根因。"
                << L"请同时查看同一时间点的服务退出码、设备状态和 Log\\UnrealDbgDll.log；"
                << L"如果服务报告 577，应按“驱动签名/测试证书不受信任”处理。";
        }
        else if (diagnosticError == ERROR_INVALID_IMAGE_HASH)
        {
            stream << L"\r\n补充诊断：这是驱动映像签名链校验失败，不是设备不存在。"
                << L"服务会在加载驱动映像之前退出，因此后续 DbgkSysWin11 设备不会创建。";
        }
        Error(stream.str());
    }

    void LogSink::ErrorHresult(const std::wstring_view operation, const HRESULT status)
    {
        const ErrorExplanation explanation = ExplainHresult(status);
        const std::wstring systemMessage = GetSystemErrorMessage(static_cast<DWORD>(status));
        std::wstringstream stream;
        stream << L"操作：" << operation << L"\r\n"
            << L"HRESULT：0x" << std::hex << std::uppercase << std::setw(8) << std::setfill(L'0')
            << static_cast<unsigned long>(status) << std::dec << L"\r\n"
            << L"系统原文：" << systemMessage << L"\r\n"
            << L"原因：" << explanation.reason << L"\r\n"
            << L"解决方案：" << explanation.solution;
        Error(stream.str());
    }

    void LogSink::Write(const LogLevel level, const std::wstring_view text)
    {
        if (text.empty())
        {
            return;
        }

        std::scoped_lock lock(mutex_);
        // 保持文件记录和待处理 UI 记录的顺序一致，即使工作线程和 UI 线程同时报告错误。
        AppendToFile(level, text);
        pending_.push_back({ level, std::wstring(text) });
    }

    void LogSink::AppendToFile(const LogLevel level, const std::wstring_view text) noexcept
    {
        try
        {
            const std::filesystem::path directory = std::filesystem::path(applicationDirectory_) / L"Log";
            std::filesystem::create_directories(directory);

            std::wstringstream stream;
            stream << GetTimePrefix() << L" [" << GetLevelText(level) << L"][pid=" << GetCurrentProcessId()
                << L"][tid=" << GetCurrentThreadId() << L"][会话=" << sessionId_ << L"][序号=" << ++sequence_
                << L"] " << text << L"\r\n";
            const std::string utf8 = ToUtf8(stream.str());
            if (utf8.empty())
            {
                return;
            }

            // 每次写入前按“现有字节 + 本条记录”检查上限。前端的汇总
            // 文件是逐条打开/关闭的，因此可在运行中的长时间测试里安全
            // 轮转，而不是只能等到下次启动才处理超大日志。
            RotateLogIfOversized(directory / L"log.ini", 16ULL * 1024ULL * 1024ULL, utf8.size());
            std::ofstream file(directory / L"log.ini", std::ios::binary | std::ios::app);
            if (file.is_open())
            {
                file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
                file.flush();
            }

            // 单会话日志有独立 64 MB 上限；达到上限后汇总日志继续完整
            // 写入并留下明确标记，避免长期高频 IOCTL 审计耗尽磁盘。
            constexpr std::uintmax_t kMaximumSessionLogBytes = 64ULL * 1024ULL * 1024ULL;
            if (!sessionLogPath_.empty() && !sessionLogTruncated_)
            {
                if (sessionLogBytes_ + utf8.size() <= kMaximumSessionLogBytes)
                {
                    std::ofstream session(sessionLogPath_, std::ios::binary | std::ios::app);
                    if (session.is_open())
                    {
                        session.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
                        session.flush();
                        sessionLogBytes_ += utf8.size();
                    }
                }
                else
                {
                    sessionLogTruncated_ = true;
                    const std::wstring warning = GetTimePrefix() + L" [错误][会话=" + sessionId_ +
                        L"] 会话日志达到 64 MB 上限；后续完整日志仍写入 Log\\log.ini。\r\n";
                    const std::string warningUtf8 = ToUtf8(warning);
                    std::ofstream session(sessionLogPath_, std::ios::binary | std::ios::app);
                    if (session.is_open() && !warningUtf8.empty())
                    {
                        session.write(warningUtf8.data(), static_cast<std::streamsize>(warningUtf8.size()));
                        session.flush();
                    }
                }
            }
        }
        catch (...)
        {
            // 文件日志绝不能导致 UI 或工作线程崩溃。
        }
    }

    void LogSink::DrainToRichEdit()
    {
        std::vector<LogRecord> records;
        HWND richEdit{};
        {
            std::scoped_lock lock(mutex_);
            richEdit = richEdit_;
            records.swap(pending_);
        }

        if (!IsWindow(richEdit))
        {
            return;
        }

        const bool followTail = IsRichEditAtBottom(richEdit);

        for (const LogRecord& record : records)
        {
            const std::wstring text = GetTimePrefix() + L" " + record.text + L"\r\n";
            CHARFORMAT2W format{};
            format.cbSize = sizeof(format);
            format.dwMask = CFM_COLOR;
            format.crTextColor = GetLevelColor(record.level);

            SendMessageW(richEdit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
            SendMessageW(richEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
            SendMessageW(richEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
        }

        constexpr LRESULT kMaximumVisibleLines = 2000;
        if (SendMessageW(richEdit, EM_GETLINECOUNT, 0, 0) > kMaximumVisibleLines)
        {
            SetWindowTextW(richEdit, L"");
        }

        if (followTail)
        {
            // 先明确滚动到最后，再移动插入点，兼容不同版本的 RichEdit。
            SendMessageW(richEdit, EM_SCROLL, SB_BOTTOM, 0);
            SendMessageW(richEdit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
            SendMessageW(richEdit, EM_SCROLLCARET, 0, 0);
        }
    }

    void LogSink::DrainPending(std::vector<LogRecord>& records)
    {
        std::scoped_lock lock(mutex_);
        records.clear();
        records.swap(pending_);
    }

    std::wstring GetApplicationDirectory()
    {
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;)
        {
            const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (written == 0)
            {
                return {};
            }
            if (written < buffer.size() - 1)
            {
                std::filesystem::path path(std::wstring_view(buffer.data(), written));
                return path.parent_path().wstring();
            }
            if (buffer.size() >= 32768)
            {
                return {};
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::wstring GetRuntimeDirectory()
    {
        const std::wstring exeDirectory = GetApplicationDirectory();
        if (exeDirectory.empty())
        {
            return {};
        }

        // 运行目录判定：主控 DLL 必须存在，避免误判到只有部分文件的目录。
        // Config 目录可能在首次运行时创建，因此不作为判定条件。
        const auto isRuntimeDirectory = [](const std::filesystem::path& candidate)
        {
            std::error_code error;
            return std::filesystem::exists(candidate / L"bin" / L"UnrealDbgProDll.dll", error);
        };

        std::vector<std::filesystem::path> candidates;
        const std::filesystem::path exeRoot(exeDirectory);
        candidates.push_back(exeRoot);

        // 当前工作目录：从解决方案根目录调用 exe 时的常见形态。
        std::vector<wchar_t> currentDirectory(MAX_PATH);
        const DWORD cwdLength = GetCurrentDirectoryW(static_cast<DWORD>(currentDirectory.size()),
            currentDirectory.data());
        if (cwdLength > 0 && cwdLength < currentDirectory.size())
        {
            candidates.emplace_back(std::wstring(currentDirectory.data(), cwdLength));
        }

        // exe 上级目录 + 构建输出相对路径：覆盖 exe 被单独放到项目根目录运行的情况。
        static const wchar_t* const kRuntimeRelativePaths[] = {
            L"x64\\WinUI\\Release",
            L"x64\\WinUI\\Debug",
            L"x64\\Release",
        };
        std::filesystem::path ancestor = exeRoot;
        for (int depth = 0; depth < 4; ++depth)
        {
            for (const wchar_t* relative : kRuntimeRelativePaths)
            {
                candidates.push_back(ancestor / relative);
            }
            const std::filesystem::path parent = ancestor.parent_path();
            if (parent.empty() || parent == ancestor)
            {
                break;
            }
            ancestor = parent;
        }

        for (const std::filesystem::path& candidate : candidates)
        {
            if (isRuntimeDirectory(candidate))
            {
                return candidate.wstring();
            }
        }

        // 全部探测失败时保持历史行为：以 exe 同级目录作为运行目录。
        return exeDirectory;
    }

    bool IsProcessElevated() noexcept
    {
        HANDLE token{};
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
        {
            return false;
        }
        TOKEN_ELEVATION elevation{};
        DWORD returned{};
        const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned);
        CloseHandle(token);
        return ok != FALSE && elevation.TokenIsElevated != 0;
    }

    std::wstring GetSystemErrorMessage(const DWORD error)
    {
        LPWSTR message{};
        const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
        if (length == 0 || message == nullptr)
        {
            return L"系统未提供错误文本";
        }

        std::wstring result(message, length);
        LocalFree(message);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
        {
            result.pop_back();
        }
        return result;
    }

    ErrorExplanation ExplainWin32Error(const DWORD error)
    {
        switch (error)
        {
        case ERROR_SUCCESS:
            return { L"操作已成功返回。", L"无需处理。" };
        case ERROR_FILE_NOT_FOUND:
            return { L"找不到指定文件，文件可能被删除、改名，或当前目录不正确。", L"确认文件路径和文件名正确，并确认文件已部署到程序目录；然后重试。" };
        case ERROR_PATH_NOT_FOUND:
            return { L"找不到指定目录或路径中的某一级目录。", L"检查程序目录、驱动目录和配置文件路径，创建缺失目录后重试。" };
        case ERROR_ACCESS_DENIED:
            return { L"当前进程没有执行该操作所需的访问权限，或对象被安全策略保护。", L"使用管理员权限运行，确认文件/设备未被安全软件拦截，并检查目标对象的权限。" };
        case ERROR_INVALID_HANDLE:
            return { L"使用了无效、已关闭或未初始化的句柄。", L"确认前置初始化已成功，避免重复释放句柄；失败后重新初始化再重试。" };
        case ERROR_NOT_ENOUGH_MEMORY:
            return { L"系统或进程无法分配足够内存。", L"关闭无关程序、检查内存占用，并重启本程序后重试。" };
        case ERROR_GEN_FAILURE:
            return { L"系统连接的设备未正常工作；常见于驱动设备对象不可用、驱动未启动或设备初始化失败。", L"确认已使用管理员权限运行；检查 VT/Dbgk 驱动服务、设备符号链接和 Windows 版本兼容性，并查看驱动日志后重试。" };
        case ERROR_SHARING_VIOLATION:
            return { L"文件正被其他进程占用，当前共享方式不允许访问。", L"关闭占用该文件的程序或服务，确认日志文件未被锁定后重试。" };
        case ERROR_INVALID_PARAMETER:
            return { L"传入的参数不符合 API 要求，可能为空、越界或格式错误。", L"检查路径、PID、地址、缓冲区长度和配置项；修正参数后重试。" };
        case ERROR_INVALID_DATA:
            return { L"数据格式、校验值或驱动返回内容无效，常见于配置损坏、符号表不匹配或协议版本不一致。", L"检查配置文件、PDB/符号表、驱动与 DLL 版本是否配套；删除损坏的临时配置后重试。" };
        case ERROR_REVISION_MISMATCH:
            return { L"正在运行的同名驱动服务指向的二进制文件与当前发布包不同。继续复用会把不同版本的 DLL、符号表和驱动混在一起，可能造成错误断点或系统不稳定。", L"不要强制停止运行中的驱动。关闭使用它的旧程序并重启 Windows；确认服务 ImagePath 与当前 bin 目录一致后，再重新进入 VT 模式。" };
        case ERROR_NOT_SUPPORTED:
            return { L"当前 Windows Build/UBR 不在精确兼容性支持表中，程序已在加载任何驱动之前拒绝继续。", L"在对应 Windows Build、CPU 和安全策略的快照虚拟机中完成加载、符号握手、退出和重启回归后，再将该版本加入支持表；不要用“Build >= 22000”绕过检查。" };
        case ERROR_SUCCESS_REBOOT_REQUIRED:
            return { L"VT 核心可能已经进入 VMX 状态，但后续桥接或握手失败。为避免未验证的内核运行时卸载，程序要求重启恢复。", L"关闭程序并重启 Windows 后再重试；随后检查前置日志中的签名、服务路径、Windows 版本和符号表失败原因。" };
        case ERROR_DUPLICATE_TAG:
            return { L"请求的条目已经存在，操作会造成重复配置。", L"检查现有列表并使用已有条目，或先移除重复项后再保存。" };
        case ERROR_ARITHMETIC_OVERFLOW:
            return { L"地址或长度计算发生整数溢出，继续操作可能访问错误内存。", L"检查模块基址、固定偏移和位数；确认计算未越界后再重试。" };
        case ERROR_UNHANDLED_EXCEPTION:
            return { L"底层组件抛出了未处理异常，内部状态可能已经不可靠。", L"查看对应组件日志和异常文本，确认 DLL、驱动及目标进程版本匹配后重启程序。" };
        case ERROR_INSUFFICIENT_BUFFER:
            return { L"提供的输出缓冲区太小，无法容纳完整结果。", L"增大缓冲区并按 API 返回的所需长度重新调用。" };
        case ERROR_MOD_NOT_FOUND:
            return { L"找不到指定 DLL 或模块，依赖文件可能缺失或架构不匹配。", L"确认 DLL 位于程序目录或系统搜索路径，且与当前程序同为 x64；检查依赖项后重试。" };
        case ERROR_DLL_NOT_FOUND:
            return { L"找不到所需 DLL，依赖文件可能缺失或架构不匹配。", L"确认 UnrealDbgProDll.dll、D-encryption.dll 及其依赖项已部署到程序目录，并确认全部为 x64。" };
        case ERROR_PROC_NOT_FOUND:
            return { L"DLL 中不存在所需的导出函数，DLL 版本可能不匹配。", L"部署与本程序配套的 UnrealDbgProDll.dll/D-encryption.dll，并确认导出名称和位数一致。" };
        case ERROR_BAD_EXE_FORMAT:
            return { L"可执行文件或 DLL 格式无效，常见原因是 32/64 位不匹配或文件损坏。", L"确认程序、DLL 和驱动均为 x64，重新部署完整文件并检查依赖项。" };
        case ERROR_FILENAME_EXCED_RANGE:
            return { L"文件路径或名称超过 Windows 允许的长度。", L"缩短程序目录、目标程序路径或调试器名称后重试。" };
        case ERROR_NOT_ALL_ASSIGNED:
            return { L"令牌中未分配 SeDebugPrivilege，当前进程无法获得调试权限。", L"使用管理员权限运行，并在本地安全策略中允许当前账户调试程序；重新启动后重试。" };
        case ERROR_PARTIAL_COPY:
            return { L"系统只完成了部分内存读取或写入，目标进程可能已退出、地址无效或权限不足。", L"确认目标 PID 仍存活、模块版本和地址正确，并以管理员权限重试；不要继续使用部分结果。" };
        case ERROR_INVALID_ADDRESS:
            return { L"提供的内存地址无效或不在目标进程的有效地址空间内。", L"重新枚举目标模块并校验偏移、位数和地址溢出，确认目标进程版本匹配。" };
        case ERROR_NOACCESS:
            return { L"访问的内存区域无效或没有相应保护权限。", L"确认目标进程仍在运行、地址可读写且句柄权限足够；必要时重新获取句柄。" };
        case ERROR_NO_TOKEN:
            return { L"当前线程或进程没有可用的访问令牌。", L"确认线程未模拟无效令牌，并以正常用户令牌或管理员权限重新启动程序。" };
        case ERROR_DLL_INIT_FAILED:
            return { L"DLL 的进程初始化入口返回失败，依赖环境或初始化顺序不满足要求。", L"检查 DLL 依赖、驱动状态和运行库版本，查看 Log\\UnrealDbgDll.log 后重启程序。" };
        case ERROR_NOT_FOUND:
            return { L"系统找不到请求的对象，可能是设备、进程、模块或配置项不存在。", L"确认目标对象已创建且名称正确，检查驱动和目标进程状态后重试。" };
        case ERROR_CANCELLED:
            return { L"操作被用户或系统取消。", L"重新执行操作；若未主动取消，检查安全策略、超时设置和目标状态。" };
        case ERROR_TIMEOUT:
            return { L"操作在规定时间内没有完成。对于符号缓存，连续 20 秒未收到任何下载数据会立即停止；只要数据持续到达，最多允许下载 3 分钟。程序已主动停止本次准备，未加载驱动，也不会使用未验证的旧 PDB。", L"确认可访问 https://msdl.microsoft.com/download/symbols/，检查代理/VPN、防火墙和 DNS；确认网络恢复后点击“重试并进入 VT”。如果企业网络要求代理，请让网络管理员允许 Microsoft 符号服务器及其 Azure Blob HTTPS 重定向。" };
        case ERROR_NETWORK_UNREACHABLE:
            return { L"当前网络无法到达目标服务器。", L"检查网络连接、DNS、代理/VPN 和防火墙规则；确认 Microsoft 符号服务器可访问后重试。" };
        case ERROR_ACCESS_DISABLED_BY_POLICY:
            return { L"该操作被组策略或安全策略阻止。", L"检查本机组策略、应用控制和安全软件规则，允许本程序及驱动后重试。" };
        case ERROR_INVALID_IMAGE_HASH:
            return { L"Windows 拒绝加载驱动映像：驱动签名无效、测试证书不受信任，或当前系统未启用测试签名。", L"开发测试版请以管理员身份把运行目录 Certificates 中对应的 .cer 导入“本地计算机\\受信任的根证书颁发机构”和“受信任的发布者”，并在确认测试环境后启用 TESTSIGNING、重启系统；正式环境请改用受 Microsoft 信任的正式驱动签名。" };
        case ERROR_DRIVER_BLOCKED:
            return { L"驱动被 Windows 安全策略、签名要求或内存完整性功能阻止加载。", L"确认驱动签名和 WDK/Windows 版本匹配，检查内存完整性与代码完整性日志；不要绕过安全策略。" };
        case ERROR_INVALID_WINDOW_HANDLE:
            return { L"窗口句柄无效，窗口可能已经销毁。", L"确认窗口仍存在后再操作，并在销毁时清理相关句柄。" };
        case ERROR_NOT_ENOUGH_QUOTA:
            return { L"系统分页文件或进程配额不足。", L"关闭无关程序、增加可用虚拟内存，并降低单次分配大小后重试。" };
        case ERROR_SERVICE_NOT_ACTIVE:
            return { L"目标 Windows 服务未运行。", L"启动对应驱动服务，确认服务状态正常后重新初始化。" };
        case ERROR_SERVICE_DISABLED:
            return { L"目标 Windows 服务已被禁用。", L"在服务管理器中启用服务，并检查组策略或安全软件是否阻止启动。" };
        case ERROR_SERVICE_EXISTS:
            return { L"服务注册项已经存在；这不是最终驱动故障，必须继续检查现有服务是否正在运行以及上一次启动的退出码。", L"如果服务正在运行，程序会复用它；如果服务已停止，请查看日志中的服务退出码（例如 577），修复签名或驱动环境后再重试。" };
        case ERROR_SERVICE_ALREADY_RUNNING:
            return { L"目标 Windows 服务已经运行。", L"无需重复启动；如果设备仍不可用，请检查设备符号链接和驱动日志。" };
        case ERROR_SERVICE_MARKED_FOR_DELETE:
            return { L"目标 Windows 服务已标记为删除，服务控制管理器尚未完成清理。", L"关闭占用服务的程序，必要时重启系统后再安装或启动驱动。" };
        case ERROR_SERVICE_SPECIFIC_ERROR:
            return { L"服务返回了专用退出错误，通用错误码不足以说明根因。", L"查询该服务的具体退出码并结合驱动日志排查；程序会优先记录服务报告的具体退出码。" };
        default:
            return { L"Windows 返回了未在诊断表中收录的错误，具体原因由系统错误码决定。", L"记录完整错误码和调用步骤，检查权限、路径、目标进程与驱动日志；仍无法解决时提供该日志进行进一步分析。" };
        }
    }

    ErrorExplanation ExplainHresult(const HRESULT status)
    {
        switch (status)
        {
        case S_OK:
            return { L"操作成功。", L"无需处理。" };
        case E_ACCESSDENIED:
            return { L"COM/WMI 访问被拒绝，当前令牌没有访问命名空间或对象的权限。", L"以管理员权限运行，确认 WMI 服务正常，并检查命名空间安全权限。" };
        case E_OUTOFMEMORY:
            return { L"COM 无法分配所需内存。", L"关闭无关程序、释放重复创建的 COM 对象后重试。" };
        case RPC_E_TOO_LATE:
            return { L"COM 安全已经由其他组件初始化，当前设置无法再次修改。", L"通常不影响已完成的 WMI 查询；若查询仍失败，确保在首次 COM 调用前初始化安全设置。" };
        case WBEM_E_INVALID_NAMESPACE:
            return { L"WMI 命名空间不存在或不可访问。", L"确认 ROOT\\CIMV2 存在且 WMI 服务已启动，修复 WMI 存储库后重试。" };
        case WBEM_E_INVALID_QUERY:
            return { L"WMI 查询语句或属性名无效。", L"检查类名、属性名和 WQL 语法，确认目标 Windows 版本提供该属性。" };
        case WBEM_E_NOT_FOUND:
            return { L"WMI 找不到请求的类、属性或对象。", L"确认 WMI 类和属性在当前系统中存在，并检查系统版本兼容性。" };
        case WBEM_E_ACCESS_DENIED:
            return { L"WMI 服务拒绝访问请求。", L"使用管理员权限运行并检查 WMI 命名空间权限及安全软件拦截。" };
        default:
            return { L"COM/WMI 返回了未在诊断表中收录的 HRESULT。", L"记录 HRESULT、查询类名和属性名，检查 COM 初始化、WMI 服务及系统事件日志。" };
        }
    }

    bool FileExists(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool DirectoryExists(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool ParseDebuggerEntry(const std::wstring_view input, DebuggerEntry& entry)
    {
        const size_t divider = input.find(L'&');
        if (divider == std::wstring_view::npos || divider == 0 || divider + 1 >= input.size())
        {
            return false;
        }

        DebuggerEntry parsed{ std::wstring(input.substr(0, divider)), std::wstring(input.substr(divider + 1)) };
        if (parsed.name.empty() || parsed.path.empty())
        {
            return false;
        }
        entry = std::move(parsed);
        return true;
    }

    std::wstring SerializeDebuggerEntry(const DebuggerEntry& entry)
    {
        return entry.name + L"&" + entry.path;
    }

    bool ExtractJsonString(const std::wstring_view json, const std::wstring_view key, std::wstring& value)
    {
        size_t position = 0;
        while (position < json.size())
        {
            if (json[position] != L'"')
            {
                ++position;
                continue;
            }

            std::wstring candidateKey;
            if (!ParseJsonStringAt(json, position, candidateKey))
            {
                return false;
            }
            SkipJsonWhitespace(json, position);
            if (position >= json.size() || json[position] != L':')
            {
                continue;
            }
            ++position;
            SkipJsonWhitespace(json, position);
            if (candidateKey == key)
            {
                return ParseJsonStringAt(json, position, value);
            }
        }
        return false;
    }

    std::wstring QueryWmiString(const std::wstring_view className, const std::wstring_view propertyName, LogSink& log)
    {
        IWbemLocator* locator{};
        IWbemServices* services{};
        IEnumWbemClassObject* enumerator{};
        IWbemClassObject* object{};
        BSTR namespaceName{};
        BSTR queryLanguage{};
        BSTR query{};
        VARIANT value{};
        VARIANT converted{};
        std::wstring result;
        std::wstring queryText;

        const HRESULT initializeSecurity = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if (FAILED(initializeSecurity) && initializeSecurity != RPC_E_TOO_LATE)
        {
            log.ErrorHresult(L"CoInitializeSecurity", initializeSecurity);
        }

        HRESULT status = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(&locator));
        if (FAILED(status))
        {
            log.ErrorHresult(L"创建 WMI 定位器", status);
            goto cleanup;
        }

        namespaceName = SysAllocString(L"ROOT\\CIMV2");
        if (namespaceName == nullptr)
        {
            log.Error(L"分配 WMI 命名空间字符串", ERROR_NOT_ENOUGH_MEMORY);
            goto cleanup;
        }
        status = locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
        if (FAILED(status))
        {
            log.ErrorHresult(L"连接 WMI 命名空间 ROOT\\CIMV2", status);
            goto cleanup;
        }
        status = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        if (FAILED(status))
        {
            log.ErrorHresult(L"设置 WMI 代理安全", status);
            goto cleanup;
        }

        queryLanguage = SysAllocString(L"WQL");
        queryText = L"SELECT " + std::wstring(propertyName) + L" FROM " + std::wstring(className);
        query = SysAllocString(queryText.c_str());
        if (queryLanguage == nullptr || query == nullptr)
        {
            log.Error(L"分配 WMI 查询字符串", ERROR_NOT_ENOUGH_MEMORY);
            goto cleanup;
        }
        status = services->ExecQuery(queryLanguage, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &enumerator);
        if (FAILED(status))
        {
            log.ErrorHresult(L"执行 WMI 查询：" + queryText, status);
            goto cleanup;
        }
        {
            ULONG returned{};
            status = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
            if (FAILED(status) || returned == 0 || object == nullptr)
            {
                if (FAILED(status))
                {
                    log.ErrorHresult(L"读取 WMI 查询结果", status);
                }
                else
                {
                    log.Info(L"WMI 查询未返回任何记录：" + queryText);
                }
                goto cleanup;
            }
        }

        status = object->Get(std::wstring(propertyName).c_str(), 0, &value, nullptr, nullptr);
        if (FAILED(status) || value.vt == VT_NULL || value.vt == VT_EMPTY)
        {
            if (FAILED(status))
            {
                log.ErrorHresult(L"读取 WMI 属性：" + std::wstring(propertyName), status);
            }
            else
            {
                log.Info(L"WMI 属性为空：" + std::wstring(propertyName));
            }
            goto cleanup;
        }
        VariantInit(&converted);
        if (SUCCEEDED(VariantChangeType(&converted, &value, 0, VT_BSTR)) && converted.bstrVal != nullptr)
        {
            result = converted.bstrVal;
        }

    cleanup:
        VariantClear(&converted);
        VariantClear(&value);
        if (query != nullptr) SysFreeString(query);
        if (queryLanguage != nullptr) SysFreeString(queryLanguage);
        if (namespaceName != nullptr) SysFreeString(namespaceName);
        ReleaseCom(object);
        ReleaseCom(enumerator);
        ReleaseCom(services);
        ReleaseCom(locator);
        return result;
    }

    std::wstring GetFileVersionString(const std::wstring& filePath, LogSink& log)
    {
        DWORD ignored{};
        const DWORD size = GetFileVersionInfoSizeW(filePath.c_str(), &ignored);
        if (size == 0)
        {
            log.Error(L"GetFileVersionInfoSizeW", GetLastError());
            return {};
        }
        std::vector<std::byte> buffer(size);
        if (!GetFileVersionInfoW(filePath.c_str(), 0, size, buffer.data()))
        {
            log.Error(L"GetFileVersionInfoW", GetLastError());
            return {};
        }
        VS_FIXEDFILEINFO* information{};
        UINT informationSize{};
        if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&information), &informationSize) ||
            information == nullptr || informationSize < sizeof(VS_FIXEDFILEINFO))
        {
            log.Error(L"VerQueryValueW", GetLastError());
            return {};
        }

        wchar_t version[64]{};
        swprintf_s(version, L"%u.%u.%u.%u", HIWORD(information->dwFileVersionMS), LOWORD(information->dwFileVersionMS),
            HIWORD(information->dwFileVersionLS), LOWORD(information->dwFileVersionLS));
        return version;
    }

    bool EnableDebugPrivilege(LogSink& log)
    {
        HANDLE token{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        {
            log.Error(L"OpenProcessToken", GetLastError());
            return false;
        }

        LUID luid{};
        if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid))
        {
            const DWORD error = GetLastError();
            CloseHandle(token);
            log.Error(L"LookupPrivilegeValueW(SeDebugPrivilege)", error);
            return false;
        }

        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Luid = luid;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        const BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
        const DWORD error = GetLastError();
        CloseHandle(token);
        if (!adjusted || error == ERROR_NOT_ALL_ASSIGNED)
        {
            log.Error(L"AdjustTokenPrivileges(SeDebugPrivilege)", error == ERROR_SUCCESS ? ERROR_NOT_ALL_ASSIGNED : error);
            return false;
        }

        log.Info(L"SeDebugPrivilege 已启用");
        return true;
    }

    void WriteDevelopmentDiagnosticsSnapshot(const std::wstring& applicationDirectory, LogSink& log,
        const std::wstring_view checkpoint)
    {
        // 此函数故意设计为只读。在驱动事务周围调用它，使未来报告可复现，
        // 而不改变服务、驱动、安全或虚拟化状态。
        log.Info(L"=== 开发诊断快照开始：" + std::wstring(checkpoint) + L" ===");
        log.Info(L"[开发诊断快照] 会话=" + log.SessionId() + L"；独立日志=" +
            (log.SessionLogPath().empty() ? L"不可用" : log.SessionLogPath()));
        log.Info(L"[开发诊断快照] 程序目录=" + applicationDirectory + L"；运行时目录=" +
            ResolveRuntimeDirectory(applicationDirectory).wstring());

        wchar_t executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (executableLength == 0 || executableLength >= std::size(executable))
        {
            log.Error(L"[开发诊断快照] 获取当前进程路径 GetModuleFileNameW", GetLastError());
        }
        else
        {
            log.Info(L"[开发诊断快照] 进程PID=" + std::to_wstring(GetCurrentProcessId()) +
                L"；映像=" + std::wstring(executable, executableLength));
        }

        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        log.Info(L"[开发诊断快照] 系统架构=" + std::wstring(ArchitectureText(system.wProcessorArchitecture)) +
            L"；逻辑处理器数=" + std::to_wstring(system.dwNumberOfProcessors) +
            L"；页大小=" + std::to_wstring(system.dwPageSize));

        bool elevated = false;
        HANDLE token{};
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            TOKEN_ELEVATION elevation{};
            DWORD returned{};
            elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned) != FALSE &&
                elevation.TokenIsElevated != 0;
            CloseHandle(token);
        }
        log.Info(L"[开发诊断快照] 进程提升状态=" + std::wstring(elevated ? L"已提升（管理员）" : L"未提升"));

        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto rtlGetVersion = ntdll == nullptr ? nullptr :
            reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        DWORD ubr{};
        const bool hasUbr = ReadRegistryDword(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", ubr);
        if (rtlGetVersion != nullptr && rtlGetVersion(&version) == 0)
        {
            const auto* support = unrealdbg::windows_support::Find(version.dwBuildNumber, hasUbr ? ubr : 0);
            log.Info(L"[开发诊断快照] Windows=" + std::to_wstring(version.dwMajorVersion) + L"." +
                std::to_wstring(version.dwMinorVersion) + L"；Build=" + std::to_wstring(version.dwBuildNumber) +
                L"；UBR=" + (hasUbr ? std::to_wstring(ubr) : L"未读取到") +
                L"；支持表=" + (support == nullptr ? L"未列入" : unrealdbg::windows_support::LevelText(support->level)));
        }
        else
        {
            log.Error(L"[开发诊断快照] 读取 RtlGetVersion", ERROR_PROC_NOT_FOUND);
        }

        const bool firmwareVirtualization = IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != FALSE;
#ifdef PF_HYPERVISOR_PRESENT
        const bool hypervisorPresent = IsProcessorFeaturePresent(PF_HYPERVISOR_PRESENT) != FALSE;
#else
        const bool hypervisorPresent = false;
#endif
        DWORD vbs{};
        const bool hasVbs = ReadRegistryDword(L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
            L"EnableVirtualizationBasedSecurity", vbs);
        DWORD hvci{};
        const bool hasHvci = ReadRegistryDword(
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
            L"Enabled", hvci);
        DWORD secureBoot{};
        const bool hasSecureBoot = ReadRegistryDword(L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
            L"UEFISecureBootEnabled", secureBoot);
        log.Info(L"[开发诊断快照] 虚拟化：固件VT=" + std::wstring(firmwareVirtualization ? L"可用" : L"不可用") +
            L"；外部Hypervisor=" + (hypervisorPresent ? L"检测到" : L"未检测到") +
            L"；VBS=" + (hasVbs ? (vbs != 0 ? L"启用" : L"关闭") : L"未知") +
            L"；HVCI=" + (hasHvci ? (hvci != 0 ? L"启用" : L"关闭") : L"未知") +
            L"；安全启动=" + (hasSecureBoot ? (secureBoot != 0 ? L"启用" : L"关闭") : L"未知"));

        static constexpr std::wstring_view artifacts[] = {
            L"UnrealDbgProDll.dll", L"AIHelper.dll", L"D-encryption.dll", L"Hook64.dll",
            L"VT_Driver.sys", L"DbgkSysWin10.sys", L"DbgkSysWin11.sys" };
        for (const std::wstring_view artifact : artifacts)
        {
            const std::filesystem::path artifactPath = ResolveRuntimeFile(applicationDirectory, artifact.data());
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(artifactPath.c_str(), GetFileExInfoStandard, &data))
            {
                log.Error(L"[开发诊断快照] 读取运行文件属性：" + artifactPath.wstring(), GetLastError());
                continue;
            }
            const ULARGE_INTEGER size{ data.nFileSizeLow, data.nFileSizeHigh };
            const std::wstring sha256 = FileSha256(artifactPath);
            log.Info(L"[开发诊断快照][文件] 名称=" + std::wstring(artifact) + L"；路径=" + artifactPath.wstring() +
                L"；字节=" + std::to_wstring(size.QuadPart) + L"；修改时间=" + FileTimeText(data.ftLastWriteTime) +
                L"；SHA-256=" + (sha256.empty() ? L"读取失败" : sha256));
        }

        const SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (manager == nullptr)
        {
            log.Error(L"[开发诊断快照] 打开服务控制管理器 OpenSCManagerW", GetLastError());
        }
        else
        {
            WriteServiceDiagnostic(manager, L"VT_Driver", log);
            WriteServiceDiagnostic(manager, L"UnrealDevice", log);
            CloseServiceHandle(manager);
        }
        log.Info(L"=== 开发诊断快照结束：" + std::wstring(checkpoint) + L" ===");
    }

    uintptr_t FindRemoteModuleBase(const DWORD processId, const std::wstring_view moduleName, LogSink& log)
    {
        if (processId == 0 || moduleName.empty())
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            log.Error(L"查找远程模块基址（PID 或模块名为空）", ERROR_INVALID_PARAMETER);
            return 0;
        }

        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        if (process == nullptr)
        {
            log.Error(L"OpenProcess(module lookup)", GetLastError());
            return 0;
        }

        DWORD required{};
        if (!K32EnumProcessModulesEx(process, nullptr, 0, &required, LIST_MODULES_ALL) || required == 0)
        {
            const DWORD error = GetLastError();
            CloseHandle(process);
            log.Error(L"K32EnumProcessModulesEx(size)", error);
            return 0;
        }

        std::vector<HMODULE> modules((required + sizeof(HMODULE) - 1) / sizeof(HMODULE));
        if (!K32EnumProcessModulesEx(process, modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
            &required, LIST_MODULES_ALL))
        {
            const DWORD error = GetLastError();
            CloseHandle(process);
            log.Error(L"K32EnumProcessModulesEx(data)", error);
            return 0;
        }

        for (const HMODULE module : modules)
        {
            std::array<wchar_t, 32768> path{};
            if (K32GetModuleFileNameExW(process, module, path.data(), static_cast<DWORD>(path.size())) == 0)
            {
                continue;
            }
            const std::filesystem::path candidate(path.data());
            if (_wcsicmp(candidate.filename().c_str(), std::wstring(moduleName).c_str()) == 0)
            {
                MODULEINFO information{};
                if (GetModuleInformation(process, module, &information, sizeof(information)))
                {
                    CloseHandle(process);
                    return reinterpret_cast<uintptr_t>(information.lpBaseOfDll);
                }
                const DWORD error = GetLastError();
                CloseHandle(process);
                log.Error(L"GetModuleInformation", error);
                return 0;
            }
        }

        CloseHandle(process);
        SetLastError(ERROR_MOD_NOT_FOUND);
        log.Error(L"未找到目标模块：" + std::wstring(moduleName), ERROR_MOD_NOT_FOUND);
        return 0;
    }

    NativeApi::NativeApi(LogSink& log) noexcept
        : log_(log)
    {
    }

    NativeApi::~NativeApi()
    {
        Unload();
    }

    bool NativeApi::Load(const std::wstring& applicationDirectory)
    {
        Unload();
        const std::filesystem::path libraryPath = ResolveRuntimeFile(applicationDirectory, L"UnrealDbgProDll.dll");
        log_.Info(L"正在加载 UnrealDbgProDll.dll：" + libraryPath.wstring());
        // 移动到 bin 后，LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 很重要：
        // 否则 Windows 可能只在 EXE 目录搜索 VMProtectSDK64.dll，并报告误导性的错误 126。
        module_ = LoadLibraryExW(libraryPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (module_ == nullptr)
        {
            log_.Error(L"加载 UnrealDbgProDll.dll：LoadLibraryW(" + libraryPath.wstring() + L")", GetLastError());
            return false;
        }

        initialize_ = reinterpret_cast<InitializeFn>(GetProcAddress(module_, "Initialize"));
        startProcess_ = reinterpret_cast<StartProcessFn>(GetProcAddress(module_, "StartProcess"));
        getFileVersion_ = reinterpret_cast<GetFileVersionFn>(GetProcAddress(module_, "GetFileVersion"));
        blockResumeThread_ = reinterpret_cast<BlockResumeThreadFn>(GetProcAddress(module_, "TL_BlockGameResumeThread"));
        shutdown_ = reinterpret_cast<ShutdownFn>(GetProcAddress(module_, "Shutdown"));
        if (initialize_ == nullptr || startProcess_ == nullptr || getFileVersion_ == nullptr || blockResumeThread_ == nullptr)
        {
            const DWORD lastError = GetLastError();
            const DWORD error = lastError == ERROR_SUCCESS ? ERROR_PROC_NOT_FOUND : lastError;
            log_.Error(L"获取 UnrealDbgDll 必需导出函数：GetProcAddress", error);
            SetLastError(error);
            Unload();
            return false;
        }

        log_.Info(shutdown_ != nullptr
            ? L"UnrealDbgDll API 已加载，四个必需导出函数和 Shutdown 收尾接口均已验证"
            : L"UnrealDbgDll API 已加载，四个必需导出函数已验证；旧 DLL 未提供 Shutdown，退出时将提示重启恢复");
        return true;
    }

    void NativeApi::Unload() noexcept
    {
        initialize_ = nullptr;
        startProcess_ = nullptr;
        getFileVersion_ = nullptr;
        blockResumeThread_ = nullptr;
        if (initializedByThisApi_ && shutdown_ != nullptr)
        {
            const BOOL result = shutdown_();
            if (!result)
            {
                const DWORD error = GetLastError() == ERROR_SUCCESS ? ERROR_SUCCESS_REBOOT_REQUIRED : GetLastError();
                log_.Error(L"卸载 UnrealDbgDll 驱动事务：Shutdown", error);
            }
            initializedByThisApi_ = false;
        }
        else if (initializedByThisApi_)
        {
            // 旧版 DLL 可能没有 Shutdown 导出。不要静默声称干净退出，
            // 当 Initialize 可能已部分加载虚拟机管理程序时，应 surfaced 需要重启的状态。
            log_.Error(L"旧版 UnrealDbgDll 未提供 Shutdown；驱动事务状态未知，退出后请重启 Windows", ERROR_SUCCESS_REBOOT_REQUIRED);
            initializedByThisApi_ = false;
        }
        shutdown_ = nullptr;
        if (module_ != nullptr)
        {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    bool NativeApi::IsLoaded() const noexcept
    {
        return module_ != nullptr;
    }

    bool NativeApi::Initialize(const std::uint64_t key)
    {
        if (initialize_ == nullptr)
        {
            SetLastError(ERROR_DLL_NOT_FOUND);
            log_.Error(L"初始化请求被拒绝：UnrealDbgDll 不可用", ERROR_DLL_NOT_FOUND);
            return false;
        }
        // 在进入 DLL 之前标记此调用。DLL 可以加载 VT 然后在符号握手期间失败；
        // 这仍然需要 Shutdown 在进程退出时运行事务补偿路径。
        initializedByThisApi_ = true;
        log_.Info(L"正在调用 UnrealDbgDll.Initialize；初始化密钥已传递（按脱敏策略不写入日志）");
        const BOOL result = initialize_(key);
        const DWORD apiError = result ? ERROR_SUCCESS : GetLastError();
        if (!result)
        {
            log_.Error(L"调用 UnrealDbgDll.Initialize", apiError);
            // 为调用者保留 DLL 的原始失败码。日志路径可能自身调用 Win32/文件 API，
            // 并覆盖线程的最后错误值。
            SetLastError(apiError);
        }
        else
        {
            log_.Info(L"UnrealDbgDll.Initialize 已成功返回；将由后端日志继续记录符号表与 IOCTL 细节");
        }
        return result != FALSE;
    }

    bool NativeApi::Shutdown()
    {
        if (!initializedByThisApi_)
        {
            return true;
        }
        if (shutdown_ == nullptr)
        {
            log_.Error(L"UnrealDbgDll 未导出 Shutdown；为避免遗留驱动状态，已要求重启恢复", ERROR_SUCCESS_REBOOT_REQUIRED);
            SetLastError(ERROR_SUCCESS_REBOOT_REQUIRED);
            return false;
        }
        const BOOL result = shutdown_();
        if (!result)
        {
            const DWORD error = GetLastError() == ERROR_SUCCESS ? ERROR_SUCCESS_REBOOT_REQUIRED : GetLastError();
            log_.Error(L"调用 UnrealDbgDll.Shutdown", error);
            SetLastError(error);
            return false;
        }
        initializedByThisApi_ = false;
        return true;
    }

    bool NativeApi::StartProcess(const std::wstring& executablePath, const std::wstring& applicationDirectory)
    {
        if (startProcess_ == nullptr)
        {
            SetLastError(ERROR_DLL_NOT_FOUND);
            log_.Error(L"启动进程请求被拒绝：UnrealDbgDll 不可用", ERROR_DLL_NOT_FOUND);
            return false;
        }
        // StartProcess 使用 wcscpy 将两个参数复制到 TCHAR[256]。在调用者边界保护
        // 这个固定的旧版 ABI，而不是允许过长的路径在现有 DLL 内部溢出内存。
        // 注入 DLL 决定通过旧版 ABI 传递的目录。按文件解析它，使部分迁移的安装
        // （Hook64 仍在旧根目录而其他文件已在 bin 中）在升级期间保持可用。
        const std::filesystem::path runtimeDirectory =
            ResolveRuntimeFile(applicationDirectory, L"Hook64.dll").parent_path();
        std::wstring legacyDirectory = runtimeDirectory.wstring();
        if (!legacyDirectory.empty() && legacyDirectory.back() != L'\\' && legacyDirectory.back() != L'/')
        {
            legacyDirectory.push_back(L'\\');
        }
        // StartProcess 将目录复制到 TCHAR[256]，然后在创建目标之前追加
        // Hook.dll/Hook64.dll。在此 ABI 边界为该后缀和终止符预留空间。
        constexpr size_t kLegacyBufferCharacters = 256;
        constexpr size_t kMaximumInjectionDllCharacters = sizeof(L"Hook64.dll") / sizeof(wchar_t) - 1;
        if (executablePath.empty() || legacyDirectory.empty() || executablePath.size() >= kLegacyBufferCharacters ||
            legacyDirectory.size() + kMaximumInjectionDllCharacters >= kLegacyBufferCharacters)
        {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            log_.Error(L"启动进程请求被拒绝：路径为空或超过旧版接口长度限制", ERROR_FILENAME_EXCED_RANGE);
            return false;
        }
        std::wstring mutableExecutable = MakeMutableCopy(executablePath);
        std::wstring mutableDirectory = MakeMutableCopy(legacyDirectory);
        const BOOL result = startProcess_(mutableExecutable.data(), mutableDirectory.data());
        if (!result)
        {
            const DWORD apiError = GetLastError();
            log_.Error(L"调用 UnrealDbgDll.StartProcess", apiError);
            SetLastError(apiError);
        }
        return result != FALSE;
    }

    bool NativeApi::BlockGameResumeThread(const DWORD processId)
    {
        if (blockResumeThread_ == nullptr || processId == 0)
        {
            const DWORD error = processId == 0 ? ERROR_INVALID_PARAMETER : ERROR_DLL_NOT_FOUND;
            SetLastError(error);
            log_.Error(L"TL_BlockGameResumeThread 请求被拒绝：DLL 未加载或 PID 无效", error);
            return false;
        }
        const BOOL result = blockResumeThread_(processId);
        if (!result)
        {
            const DWORD apiError = GetLastError();
            log_.Error(L"调用 UnrealDbgDll.TL_BlockGameResumeThread", apiError);
            SetLastError(apiError);
        }
        return result != FALSE;
    }

    EncryptionApi::EncryptionApi(LogSink& log) noexcept
        : log_(log)
    {
    }

    EncryptionApi::~EncryptionApi()
    {
        Unload();
    }

    bool EncryptionApi::Load(const std::wstring& applicationDirectory)
    {
        Unload();
        const std::filesystem::path libraryPath = ResolveRuntimeFile(applicationDirectory, L"D-encryption.dll");
        log_.Info(L"正在加载 D-encryption.dll：" + libraryPath.wstring());
        module_ = LoadLibraryExW(libraryPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (module_ == nullptr)
        {
            log_.Error(L"加载 D-encryption.dll：LoadLibraryW(" + libraryPath.wstring() + L")", GetLastError());
            return false;
        }
        decryptFile_ = reinterpret_cast<DecryptFileFn>(GetProcAddress(module_, "DecryptDataFromFile"));
        if (decryptFile_ == nullptr)
        {
            log_.Error(L"获取解密函数 DecryptDataFromFile：GetProcAddress", GetLastError());
            Unload();
            return false;
        }
        log_.Info(L"D-encryption API 已加载");
        return true;
    }

    void EncryptionApi::Unload() noexcept
    {
        decryptFile_ = nullptr;
        if (module_ != nullptr)
        {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    bool EncryptionApi::IsLoaded() const noexcept
    {
        return module_ != nullptr && decryptFile_ != nullptr;
    }

    bool EncryptionApi::DecryptFile(const std::wstring& filePath, const std::wstring& key, std::wstring& plaintext)
    {
        plaintext.clear();
        if (!IsLoaded() || filePath.empty() || key.empty())
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            log_.Error(L"解密请求被拒绝：解密 DLL 未加载、文件路径为空或密钥为空", ERROR_INVALID_PARAMETER);
            return false;
        }

        std::wstring mutablePath = MakeMutableCopy(filePath);
        std::wstring mutableKey = MakeMutableCopy(key);
        // D-encryption.dll 使用旧版"返回字节数 + GetLastError" ABI。
        // 先清空线程错误状态，才能区分"解密失败但 DLL 忘记设置错误码"
        // 与一次真正成功的空结果，并把底层 CryptoAPI 的错误码保留下来。
        SetLastError(ERROR_SUCCESS);
        const int byteCount = decryptFile_(mutablePath.data(), mutableKey.data(), nullptr);
        const DWORD decryptError = GetLastError();
        if (byteCount < static_cast<int>(sizeof(wchar_t)) || byteCount > static_cast<int>(kMaximumCopyrightBytes) ||
            byteCount % static_cast<int>(sizeof(wchar_t)) != 0)
        {
            const DWORD diagnosticError = decryptError == ERROR_SUCCESS ? ERROR_INVALID_DATA : decryptError;
            SetLastError(diagnosticError);
            log_.Error(L"DecryptDataFromFile 返回的明文长度无效；已保留解密模块原始错误码", diagnosticError);
            return false;
        }

        const size_t characterCount = static_cast<size_t>(byteCount) / sizeof(wchar_t);
        std::vector<wchar_t> buffer(characterCount + 1, L'\0');
        const int copiedBytes = decryptFile_(mutablePath.data(), mutableKey.data(), buffer.data());
        if (copiedBytes != byteCount)
        {
            const DWORD secondError = GetLastError();
            const DWORD diagnosticError = secondError == ERROR_SUCCESS ? ERROR_INVALID_DATA : secondError;
            SetLastError(diagnosticError);
            log_.Error(L"DecryptDataFromFile 两次返回的明文长度不一致", diagnosticError);
            return false;
        }

        buffer[characterCount] = L'\0';
        plaintext.assign(buffer.data());
        if (plaintext.empty())
        {
            SetLastError(ERROR_INVALID_DATA);
            log_.Error(L"DecryptDataFromFile 返回空明文", ERROR_INVALID_DATA);
            return false;
        }
        return true;
    }

    bool RunCoreSelfTests(std::wstring& failure)
    {
        if (unrealdbg::windows_support::Find(26200, 0) == nullptr ||
            unrealdbg::windows_support::Find(22631, 500) == nullptr ||
            unrealdbg::windows_support::Find(99999, 0) != nullptr)
        {
            failure = L"精确 Windows 支持表未按 Build/UBR 选择或错误接受了未知版本";
            return false;
        }
        DebuggerEntry entry;
        if (!ParseDebuggerEntry(L"x64dbg.exe&C:\\Tools\\x64dbg.exe", entry) ||
            entry.name != L"x64dbg.exe" || entry.path != L"C:\\Tools\\x64dbg.exe")
        {
            failure = L"调试器列表解析器未保留有效的旧格式条目";
            return false;
        }
        if (ParseDebuggerEntry(L"missing-divider", entry) || ParseDebuggerEntry(L"&C:\\Tool.exe", entry))
        {
            failure = L"调试器列表解析器错误接受了无效输入";
            return false;
        }
        if (SerializeDebuggerEntry({ L"tool.exe", L"C:\\Tool.exe" }) != L"tool.exe&C:\\Tool.exe")
        {
            failure = L"调试器列表序列化器改变了旧格式";
            return false;
        }

        std::wstring value;
        if (!ExtractJsonString(LR"({"caption":"Native \u8c03\u8bd5\"","escaped":"line\nnext"})", L"caption", value) ||
            value != L"Native 调试\"")
        {
            failure = L"JSON 解析器未正确解码 Unicode 字符串";
            return false;
        }
        if (!ExtractJsonString(L"{\"escaped\":\"line\\nnext\"}", L"escaped", value) || value != L"line\nnext")
        {
            failure = L"JSON 解析器未正确解码转义换行符";
            return false;
        }
        if (ExtractJsonString(L"{\"value\":42}", L"value", value))
        {
            failure = L"JSON 解析器错误地接受了非字符串值";
            return false;
        }

        const ErrorExplanation signatureError = ExplainWin32Error(ERROR_INVALID_IMAGE_HASH);
        if (signatureError.reason.find(L"签名") == std::wstring::npos ||
            signatureError.solution.find(L"证书") == std::wstring::npos)
        {
            failure = L"577 驱动签名错误缺少中文原因或证书解决方案";
            return false;
        }
        const ErrorExplanation genericError = ExplainWin32Error(ERROR_GEN_FAILURE);
        if (genericError.reason.find(L"设备") == std::wstring::npos ||
            genericError.solution.find(L"驱动") == std::wstring::npos)
        {
            failure = L"31 通用错误缺少根因提示或驱动排查方案";
            return false;
        }
        const ErrorExplanation symbolTimeout = ExplainWin32Error(ERROR_TIMEOUT);
        if (symbolTimeout.reason.find(L"20 秒") == std::wstring::npos || symbolTimeout.reason.find(L"3 分钟") == std::wstring::npos ||
            symbolTimeout.solution.find(L"Microsoft") == std::wstring::npos)
        {
            failure = L"符号下载超时缺少中文原因或网络解决方案";
            return false;
        }

        failure.clear();
        return true;
    }
}
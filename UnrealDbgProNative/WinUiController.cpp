#include "WinUiController.h"

#include <CommDlg.h>
#include <Psapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include "../Common/Shared/WindowsBuildSupport.h"

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Psapi.lib")

namespace
{
    using unrealdbg_native::DebuggerEntry;
    using unrealdbg_native::LogLevel;
    using unrealdbg_native::LogSink;

    constexpr wchar_t kDebuggerIniName[] = L"DebuggerList.ini";
    constexpr wchar_t kConfigIniName[] = L"Config.ini";
    constexpr wchar_t kBackendLogName[] = L"Log\\UnrealDbgDll.log";
    constexpr uintptr_t kTlLastTickOffset = 0xFF0A439ULL + sizeof(DWORD);

    std::wstring PathInApplicationDirectory(const std::wstring& directory, const wchar_t* name)
    {
        return (std::filesystem::path(directory) / name).wstring();
    }

    std::wstring PathInConfigDirectory(const std::wstring& directory, const wchar_t* name)
    {
        return (std::filesystem::path(directory) / L"Config" / name).wstring();
    }

    std::wstring ExistingConfigPath(const std::wstring& directory, const wchar_t* name)
    {
        const std::wstring current = PathInConfigDirectory(directory, name);
        return unrealdbg_native::FileExists(current) ? current : PathInApplicationDirectory(directory, name);
    }

    bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) noexcept
    {
        return _wcsicmp(left.c_str(), right.c_str()) == 0;
    }

    bool ContainsDebugger(const std::vector<DebuggerEntry>& entries, const DebuggerEntry& candidate)
    {
        return std::any_of(entries.begin(), entries.end(), [&candidate](const DebuggerEntry& item)
        {
            return EqualsInsensitive(item.name, candidate.name) && EqualsInsensitive(item.path, candidate.path);
        });
    }

    std::wstring ReadIniString(const std::wstring& path, const wchar_t* section, const wchar_t* key)
    {
        std::vector<wchar_t> buffer(512);
        for (;;)
        {
            const DWORD copied = GetPrivateProfileStringW(section, key, L"", buffer.data(),
                static_cast<DWORD>(buffer.size()), path.c_str());
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

    bool ReadIniBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback)
    {
        const std::wstring value = ReadIniString(path, section, key);
        if (value.empty())
        {
            return fallback;
        }
        return value == L"1" || EqualsInsensitive(value, L"true") || EqualsInsensitive(value, L"yes") || EqualsInsensitive(value, L"on");
    }

    bool WriteIniBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool value)
    {
        return WritePrivateProfileStringW(section, key, value ? L"1" : L"0", path.c_str()) != FALSE;
    }

    std::wstring Utf8ToWide(const std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        if (length <= 0)
        {
            codePage = CP_ACP;
            flags = 0;
            length = MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
        }
        if (length <= 0)
        {
            return {};
        }
        std::wstring result(static_cast<size_t>(length), L'\0');
        return MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), result.data(), length) == length ? result : std::wstring{};
    }

    DWORD QueryDriverServiceFailure(const wchar_t* serviceName) noexcept
    {
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!manager)
        {
            return ERROR_SUCCESS;
        }
        SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_QUERY_STATUS);
        if (!service)
        {
            CloseServiceHandle(manager);
            return ERROR_SUCCESS;
        }
        SERVICE_STATUS_PROCESS status{};
        DWORD returned{};
        const BOOL ok = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &returned);
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        if (!ok || returned < sizeof(status) || status.dwCurrentState != SERVICE_STOPPED)
        {
            return ERROR_SUCCESS;
        }
        if (status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR && status.dwServiceSpecificExitCode != ERROR_SUCCESS)
        {
            return status.dwServiceSpecificExitCode;
        }
        return status.dwWin32ExitCode == ERROR_SUCCESS ? ERROR_SUCCESS : status.dwWin32ExitCode;
    }

    bool QueryExactBuildUbr(DWORD& build, DWORD& ubr) noexcept
    {
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto fn = ntdll == nullptr ? nullptr :
            reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn == nullptr)
        {
            return false;
        }
        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        if (fn(&version) != 0)
        {
            return false;
        }
        build = version.dwBuildNumber;
        ubr = 0;
        DWORD size = sizeof(ubr);
        (void)RegGetValueW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD,
            nullptr, &ubr, &size);
        return true;
    }

    // ---------------------------------------------------------------- 快速系统信息
    // 需求：系统信息必须"快速、稳定"地显示真实值，不允许长时间停留在读取中。
    // WMI（Win32_OperatingSystem / Win32_Processor）首次查询在部分机器上需要数秒，
    // 因此这里全部改为本地的注册表与 Win32 API 读取（毫秒级且不会挂起）。
    std::wstring ReadRegistryStringValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName)
    {
        DWORD type{};
        DWORD size{};
        if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
            &type, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t))
        {
            return {};
        }
        std::wstring buffer(static_cast<size_t>(size) / sizeof(wchar_t), L'\0');
        if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
            &type, buffer.data(), &size) != ERROR_SUCCESS)
        {
            return {};
        }
        while (!buffer.empty() && buffer.back() == L'\0')
        {
            buffer.pop_back();
        }
        return buffer;
    }

    const wchar_t* ArchitectureLabel(const WORD architecture) noexcept
    {
        switch (architecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
        case PROCESSOR_ARCHITECTURE_ARM: return L"ARM";
        default: return L"未知";
        }
    }

    std::wstring FormatBytesAsGigabytes(const std::uint64_t bytes)
    {
        if (bytes == 0) return L"不可用";
        wchar_t buffer[48]{};
        const double gigabytes = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        swprintf_s(buffer, L"%.1f GB", gigabytes);
        return buffer;
    }
}

namespace unrealdbg_native
{
    std::atomic<LogSink*> g_winUiLogSink{ nullptr };

    void SetWinUiLogSink(LogSink* sink) noexcept
    {
        g_winUiLogSink.store(sink, std::memory_order_release);
    }

    LogSink* GetWinUiLogSink() noexcept
    {
        return g_winUiLogSink.load(std::memory_order_acquire);
    }

    WinUiController::WinUiController(std::wstring applicationDirectory)
        : applicationDirectory_(std::move(applicationDirectory)), log_(applicationDirectory_), nativeApi_(log_),
          backendLogPath_(std::filesystem::path(applicationDirectory_) / kBackendLogName)
    {
        SetWinUiLogSink(&log_);
    }

    WinUiController::~WinUiController()
    {
        StopWorkers();
        SetWinUiLogSink(nullptr);
        nativeApi_.Unload();
    }

    bool WinUiController::Start()
    {
        log_.Info(L"=== UnrealDbg WinUI 启动 ===");
        log_.Info(L"原生 WinUI 3 前端已初始化；后端驱动和 UnrealDbgDll 协议保持不变");
        log_.Info(L"启动阶段将预准备系统 PDB 符号缓存；点击“进入 VT 调试模式”后才会加载驱动并执行符号表握手");
        std::error_code error;
        backendLogOffset_ = std::filesystem::file_size(backendLogPath_, error);
        if (error) backendLogOffset_ = 0;
        log_.Info(L"内核日志协议已切换为 UDBG-UTF8/1；不再读取或创建 C:\\Logs\\driver.xml");
        nativeApiLoaded_ = nativeApi_.Load(applicationDirectory_);
        if (nativeApiLoaded_) log_.Info(L"WinUI 前端已连接 UnrealDbgProDll；驱动初始化仍等待用户点击");
        else log_.Error(L"UnrealDbgProDll.dll 加载失败；请检查 bin 目录和依赖文件", ERROR_DLL_NOT_FOUND);
        if (!EnableDebugPrivilege(log_))
            log_.Error(L"SeDebugPrivilege 未启用；目标进程操作可能失败，请以管理员身份运行", ERROR_NOT_ALL_ASSIGNED);
        LoadDebuggerList();
        LoadTlSettings();
        LoadSystemInformation();
        // 仅采集只读信息：这份启动快照应当成为每次测试的基线，可与
        // 后续“进入 VT 前/后”的快照按会话号和时间顺序直接对照。
        WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_, L"WinUI 启动完成、驱动加载前");
        StartSymbolPreparationAsync();
        log_.Info(L"=== UnrealDbg WinUI 启动完成 ===");
        return nativeApiLoaded_;
    }

    void WinUiController::PollBackendLogs()
    {
        auto poll = [this](const std::filesystem::path& path, std::uintmax_t& offset, std::string& partial)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error) return;
            if (size < offset) { offset = 0; partial.clear(); }
            if (size == offset) return;
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return;
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!file) return;
            std::string chunk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            offset += chunk.size();
            partial += std::move(chunk);
            size_t end{};
            while ((end = partial.find('\n')) != std::string::npos)
            {
                std::string line = partial.substr(0, end);
                partial.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                const LogLevel level = line.find("[ERROR]") != std::string::npos || line.find("[错误]") != std::string::npos
                    ? LogLevel::Error : (line.find("[DEBUG]") != std::string::npos || line.find("[调试]") != std::string::npos ? LogLevel::Debug : LogLevel::Info);
                const std::wstring wide = Utf8ToWide(line);
                if (wide.empty()) log_.Error(L"读取后端日志时 UTF-8 解码失败", ERROR_INVALID_DATA);
                else log_.External(level, L"[后端] " + wide);
            }
            if (partial.size() > 1024 * 1024)
            {
                partial.clear();
                log_.Error(L"读取后端日志时发现超长未结束行，已丢弃", ERROR_INVALID_DATA);
            }
        };
        poll(backendLogPath_, backendLogOffset_, backendPartialLine_);
    }

    void WinUiController::DrainLogs(std::vector<LogRecord>& records)
    {
        log_.DrainPending(records);
    }

    void WinUiController::ReloadFromDisk()
    {
        LoadSystemInformation();
        LoadDebuggerList();
        LoadTlSettings();
    }

    void WinUiController::Shutdown() noexcept
    {
        StopWorkers();
    }

    void WinUiController::LoadSystemInformation()
    {
        // 全部改为本地注册表 / Win32 API 读取：耗时在毫秒级，界面首帧即可显示真实
        // 信息，不再出现"读取中"长时间不消失的问题（原来的 WMI 查询已从这里移除，
        // 它在部分机器上首次调用需要数秒，正是系统信息一直停在读取中的根因）。
        constexpr wchar_t kVersionKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        std::wstring productName = ReadRegistryStringValue(HKEY_LOCAL_MACHINE, kVersionKey, L"ProductName");
        if (productName.empty()) productName = ReadRegistryStringValue(HKEY_LOCAL_MACHINE, kVersionKey, L"EditionID");
        if (productName.empty()) productName = L"Windows（产品名未读取到）";
        const std::wstring displayVersion = ReadRegistryStringValue(HKEY_LOCAL_MACHINE, kVersionKey, L"DisplayVersion");
        systemName_ = displayVersion.empty() ? productName : productName + L" " + displayVersion;

        std::wstring processor = ReadRegistryStringValue(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
        if (processor.empty()) processor = L"处理器名称未读取到";
        SYSTEM_INFO nativeInfo{};
        GetNativeSystemInfo(&nativeInfo);
        const DWORD logicalProcessors = nativeInfo.dwNumberOfProcessors == 0 ? 1 : nativeInfo.dwNumberOfProcessors;
        cpuName_ = processor + L"（" + std::to_wstring(logicalProcessors) + L" 个逻辑处理器）";

        DWORD build{}, ubr{};
        if (QueryExactBuildUbr(build, ubr))
        {
            const auto* support = unrealdbg::windows_support::Find(build, ubr);
            systemVersion_ = L"10.0." + std::to_wstring(build) + L"." + std::to_wstring(ubr) + L"（" +
                (support == nullptr ? L"未列入支持表" : unrealdbg::windows_support::LevelText(support->level)) + L"）";
        }
        else
        {
            systemVersion_ = L"内核版本未读取到";
        }

        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        memoryText_ = GlobalMemoryStatusEx(&memory) != FALSE ? FormatBytesAsGigabytes(memory.ullTotalPhys) : L"不可用";

        SYSTEM_INFO processInfo{};
        GetSystemInfo(&processInfo);
        archText_ = std::wstring(ArchitectureLabel(nativeInfo.wProcessorArchitecture)) + L" 系统 · " +
            ArchitectureLabel(processInfo.wProcessorArchitecture) + L" 进程";

        log_.Info(L"[系统信息] " + systemName_ + L"；内核=" + systemVersion_ + L"；处理器=" + cpuName_ +
            L"；物理内存=" + memoryText_ + L"；架构=" + archText_ + L"；采集方式=注册表/Win32 API（不依赖 WMI）");
    }

    void WinUiController::LoadDebuggerList()
    {
        debuggerEntries_.clear();
        const std::wstring path = ExistingConfigPath(applicationDirectory_, kDebuggerIniName);
        const std::wstring countText = ReadIniString(path, L"DebuggerList", L"Count");
        unsigned long count{};
        if (countText.empty())
        {
            log_.Info(L"未找到 DebuggerList.ini；当前没有已添加的调试器");
            return;
        }
        try { count = std::stoul(countText); } catch (...) { count = 0; }
        if (count > 1024)
        {
            log_.Error(L"DebuggerList.ini 的 Count 超出上限，已拒绝加载", ERROR_INVALID_DATA);
            return;
        }
        for (unsigned long index = 0; index < count; ++index)
        {
            const std::wstring key = L"Debugger" + std::to_wstring(index);
            DebuggerEntry entry;
            if (!ParseDebuggerEntry(ReadIniString(path, L"DebuggerList", key.c_str()), entry))
            {
                log_.Error(L"DebuggerList 条目格式无效：" + key, ERROR_INVALID_DATA);
                continue;
            }
            if (ContainsDebugger(debuggerEntries_, entry))
            {
                log_.Error(L"DebuggerList 条目重复，已忽略：" + entry.path, ERROR_DUPLICATE_TAG);
                continue;
            }
            debuggerEntries_.push_back(std::move(entry));
        }
        log_.Info(L"调试器列表已加载：" + std::to_wstring(debuggerEntries_.size()) + L" 项");
    }

    bool WinUiController::SaveDebuggerList()
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(applicationDirectory_) / L"Config", error);
        if (error)
        {
            log_.Error(L"创建 Config 配置目录", static_cast<DWORD>(error.value()));
            return false;
        }
        const std::wstring path = PathInConfigDirectory(applicationDirectory_, kDebuggerIniName);
        WritePrivateProfileStringW(L"DebuggerList", nullptr, nullptr, path.c_str());
        if (!WritePrivateProfileStringW(L"DebuggerList", L"Count", std::to_wstring(debuggerEntries_.size()).c_str(), path.c_str()))
        {
            log_.Error(L"保存 DebuggerList.ini 条目数量", GetLastError());
            return false;
        }
        for (size_t index = 0; index < debuggerEntries_.size(); ++index)
        {
            const std::wstring key = L"Debugger" + std::to_wstring(index);
            if (!WritePrivateProfileStringW(L"DebuggerList", key.c_str(), SerializeDebuggerEntry(debuggerEntries_[index]).c_str(), path.c_str()))
            {
                log_.Error(L"保存 DebuggerList.ini 条目：" + key, GetLastError());
                return false;
            }
        }
        log_.Info(L"调试器列表已保存：" + std::to_wstring(debuggerEntries_.size()) + L" 项");
        return true;
    }

    bool WinUiController::AddDebugger(const std::wstring& path)
    {
        const std::filesystem::path file(path);
        DebuggerEntry entry{ file.filename().wstring(), file.wstring() };
        if (entry.name.empty() || !FileExists(entry.path))
        {
            log_.Error(L"所选调试器路径不是可用文件：" + entry.path, ERROR_FILE_NOT_FOUND);
            return false;
        }
        if (ContainsDebugger(debuggerEntries_, entry))
        {
            log_.Error(L"所选调试器已存在于列表中：" + entry.path, ERROR_DUPLICATE_TAG);
            return false;
        }
        debuggerEntries_.push_back(std::move(entry));
        if (!SaveDebuggerList())
        {
            debuggerEntries_.pop_back();
            return false;
        }
        return true;
    }

    bool WinUiController::DeleteDebugger(const size_t index)
    {
        if (index >= debuggerEntries_.size())
        {
            log_.Error(L"移除调试器请求无效：没有选中有效条目", ERROR_INVALID_PARAMETER);
            return false;
        }
        const DebuggerEntry removed = debuggerEntries_[index];
        debuggerEntries_.erase(debuggerEntries_.begin() + static_cast<std::ptrdiff_t>(index));
        if (!SaveDebuggerList())
        {
            debuggerEntries_.insert(debuggerEntries_.begin() + static_cast<std::ptrdiff_t>(index), removed);
            return false;
        }
        return true;
    }

    bool WinUiController::StartDebugger(const size_t index)
    {
        if (index >= debuggerEntries_.size())
        {
            log_.Error(L"启动调试器请求无效：没有选中有效条目", ERROR_INVALID_PARAMETER);
            return false;
        }
        const auto& entry = debuggerEntries_[index];
        if (!FileExists(entry.path))
        {
            log_.Error(L"调试器可执行文件已不存在：" + entry.path, ERROR_FILE_NOT_FOUND);
            return false;
        }
        if (!nativeApiLoaded_ || !nativeApi_.StartProcess(entry.path, applicationDirectory_))
        {
            log_.Error(L"启动调试器失败：" + entry.path, GetLastError());
            return false;
        }
        log_.Info(L"调试器启动请求已接受：" + entry.path);
        return true;
    }

    void WinUiController::LoadTlSettings()
    {
        const std::wstring path = ExistingConfigPath(applicationDirectory_, kConfigIniName);
        tlEnabled_ = ReadIniBoolean(path, L"TL", L"enabled_tl_confrontation", false);
        tlGetTickCount_ = ReadIniBoolean(path, L"TL", L"handler_gettickcount_check", false);
        tlBlockResumeThread_ = ReadIniBoolean(path, L"TL", L"BlockResumeThread", false);
        log_.Info(L"TL 设置已加载：启用=" + std::to_wstring(tlEnabled_) + L"，GetTickCount=" + std::to_wstring(tlGetTickCount_) + L"，BlockResumeThread=" + std::to_wstring(tlBlockResumeThread_));
    }

    bool WinUiController::SaveTlSettings()
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(applicationDirectory_) / L"Config", error);
        if (error)
        {
            return false;
        }
        const std::wstring path = PathInConfigDirectory(applicationDirectory_, kConfigIniName);
        if (!WriteIniBoolean(path, L"TL", L"enabled_tl_confrontation", tlEnabled_) ||
            !WriteIniBoolean(path, L"TL", L"handler_gettickcount_check", tlGetTickCount_) ||
            !WriteIniBoolean(path, L"TL", L"BlockResumeThread", tlBlockResumeThread_))
        {
            log_.Error(L"Config.ini 无法保存一个或多个 TL 设置", GetLastError());
            return false;
        }
        log_.Info(L"TL 设置已保存");
        return true;
    }

    void WinUiController::SetTlEnabled(const bool value)
    {
        tlEnabled_ = value;
        SaveTlSettings();
        ApplyTlOptions();
    }

    void WinUiController::SetTlGetTickCount(const bool value)
    {
        tlGetTickCount_ = value;
        SaveTlSettings();
        ApplyTlOptions();
    }

    void WinUiController::SetTlBlockResumeThread(const bool value)
    {
        tlBlockResumeThread_ = value;
        SaveTlSettings();
        ApplyTlOptions();
    }

    bool WinUiController::SelectForegroundTarget(const HWND hostWindow)
    {
        hostWindow_ = hostWindow;
        const HWND foreground = GetForegroundWindow();
        DWORD processId{};
        if (!foreground || GetWindowThreadProcessId(foreground, &processId) == 0 || processId == GetCurrentProcessId())
        {
            log_.Error(L"选择目标窗口失败：前台窗口无效或属于本程序", ERROR_INVALID_WINDOW_HANDLE);
            return false;
        }
        targetProcessId_ = processId;
        log_.Info(L"[TL.exe] 已选择前台目标 PID：" + std::to_wstring(targetProcessId_));
        ApplyTlOptions();
        return true;
    }

    void WinUiController::ApplyTlOptions()
    {
        if (!tlEnabled_ || targetProcessId_ == 0)
        {
            return;
        }
        if (tlBlockResumeThread_ && nativeApiLoaded_ && !nativeApi_.BlockGameResumeThread(targetProcessId_))
        {
            log_.Error(L"[TL.exe] 阻止恢复线程请求失败", GetLastError());
        }
        else if (tlBlockResumeThread_)
        {
            log_.Info(L"[TL.exe] 阻止恢复线程请求成功");
        }
        if (tlGetTickCount_)
        {
            const uintptr_t base = FindRemoteModuleBase(targetProcessId_, L"TL.exe", log_);
            if (base == 0)
            {
                log_.Error(L"[TL.exe] 未找到 TL.exe 模块基址", ERROR_MOD_NOT_FOUND);
            }
            else if (base <= std::numeric_limits<uintptr_t>::max() - kTlLastTickOffset)
            {
                log_.Info(L"[TL.exe] 已定位 GetTickCount 校验偏移：0x" + std::to_wstring(base + kTlLastTickOffset));
            }
        }
    }

    void WinUiController::InitializeAsync()
    {
        if (initialized_.load())
        {
            return;
        }
        if (initializing_.exchange(true))
        {
            return;
        }

        // 失败后允许用户重试。上一次工作线程已经把 initializing_ 置为
        // false，但 std::thread 仍保持 joinable；在创建新线程前必须回收它，
        // 否则给 joinable 的线程对象重新赋值会直接触发 std::terminate。
        if (initializeWorker_.joinable())
        {
            initializeWorker_.join();
        }
        {
            std::scoped_lock lock(errorMutex_);
            lastInitializationError_.clear();
        }
        // 网络暂不可用等可恢复失败不需要重启程序；用户修复网络或权限后再次
        // 点击即可重新执行严格校验/下载，再进入驱动初始化。
        if (symbolPreparationFailed_.load())
        {
            StartSymbolPreparationAsync();
        }
        initializeWorker_ = std::thread(&WinUiController::InitializeWorker, this);
    }

    void WinUiController::StartSymbolPreparationAsync()
    {
        if (symbolPreparing_.exchange(true))
        {
            return;
        }
        if (symbolPreparationWorker_.joinable())
        {
            symbolPreparationWorker_.join();
        }
        symbolStopRequested_.store(false);
        symbolCacheReady_.store(false);
        symbolPreparationFailed_.store(false);
        {
            std::scoped_lock lock(symbolStatusMutex_);
            symbolPreparationStatus_ = L"正在校验系统模块与本地缓存";
            symbolPreparationFailureDetails_.clear();
        }
        symbolPreparationWorker_ = std::thread(&WinUiController::PrepareSymbolsWorker, this);
    }

    void WinUiController::PrepareSymbolsWorker()
    {
        const SymbolPreparationResult result = PrepareSystemSymbolCache(applicationDirectory_, log_, symbolStopRequested_);
        {
            std::scoped_lock lock(symbolStatusMutex_);
            if (result.success)
            {
                symbolPreparationStatus_ = result.summary;
                symbolPreparationFailureDetails_.clear();
            }
            else
            {
                symbolPreparationStatus_ = L"符号缓存准备失败；请查看底部日志";
                symbolPreparationFailureDetails_ = result.failureDetails;
            }
        }
        symbolCacheReady_.store(result.success);
        symbolPreparationFailed_.store(!result.success && !symbolStopRequested_.load());
        symbolPreparing_.store(false);
    }

    bool WinUiController::WaitForSymbolPreparation()
    {
        if (symbolPreparationWorker_.joinable())
        {
            symbolPreparationWorker_.join();
        }
        if (symbolCacheReady_.load())
        {
            return true;
        }
        std::wstring details;
        {
            std::scoped_lock lock(symbolStatusMutex_);
            details = symbolPreparationFailureDetails_;
        }
        if (details.empty())
        {
            details = L"符号表准备未完成；未启动驱动。\r\n原因：准备线程未返回可用缓存。\r\n"
                L"解决方案：检查底部日志后重新启动程序，或确认 Symbols\\System 目录的写入权限。";
        }
        {
            std::scoped_lock lock(errorMutex_);
            lastInitializationError_ = details;
        }
        return false;
    }

    void WinUiController::InitializeWorker()
    {
        log_.Info(L"用户请求初始化 VT 后端");
        // 此处仍未加载或启动驱动。记录服务遗留状态、签名相关环境和
        // 发布文件哈希，避免失败后无法分辨“本次问题”与“上次遗留”。
        WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_, L"用户请求进入 VT 前");
        if (symbolPreparing_.load())
        {
            log_.Info(L"正在等待启动期符号缓存准备完成；此等待不会加载驱动");
        }
        if (!WaitForSymbolPreparation())
        {
            log_.Error(L"VT 后端初始化已取消：当前系统的 PDB 符号缓存未通过验证或下载失败；驱动没有被加载", ERROR_INVALID_DATA);
            WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_, L"符号预准备失败后");
            initializing_.store(false);
            return;
        }
        log_.Info(L"启动期符号缓存已通过验证；开始原有驱动初始化流程");
        log_.Info(L"VT 初始化阶段 1/4：正在初始化驱动设备");
        log_.Info(L"VT 初始化阶段 2/4：正在向已加载驱动发送符号表并进行握手");
        const bool ok = nativeApiLoaded_ && nativeApi_.Initialize(0x9dd14d00f5dd71bdULL);
        if (!ok)
        {
            DWORD error = GetLastError();
            if (error == ERROR_GEN_FAILURE || error == ERROR_SERVICE_SPECIFIC_ERROR)
            {
                const DWORD serviceError = QueryDriverServiceFailure(L"VT_Driver");
                if (serviceError != ERROR_SUCCESS)
                {
                    error = serviceError;
                }
            }
            const DWORD diagnosticError = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
            const ErrorExplanation explanation = ExplainWin32Error(diagnosticError);
            const std::wstring systemMessage = GetSystemErrorMessage(diagnosticError);
            std::wstringstream details;
            details << L"错误码：" << diagnosticError << L" (0x" << std::hex << std::uppercase
                << diagnosticError << std::dec << L")\r\n"
                << L"系统原文：" << systemMessage << L"\r\n"
                << L"原因：" << explanation.reason << L"\r\n"
                << L"解决方案：" << explanation.solution;
            if (diagnosticError == ERROR_GEN_FAILURE)
                details << L"\r\n补充诊断：错误码 31 是通用失败码，请结合 Log\\UnrealDbgDll.log 和 VT_Driver 服务退出码继续定位。";
            else if (diagnosticError == ERROR_INVALID_IMAGE_HASH)
                details << L"\r\n补充诊断：577 表示测试驱动签名链不受信任，不是缺少虚拟设备；请检查 Certificates 和系统签名策略。";
            {
                std::scoped_lock lock(errorMutex_);
                lastInitializationError_ = details.str();
            }
            log_.Error(L"VT 后端初始化失败；请根据错误码、原因和解决方案修复后重试", error);
            WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_, L"VT 初始化失败后");
            initializing_.store(false);
            return;
        }
        initialized_.store(true);
        log_.Info(L"VT 初始化阶段 3/4：符号表握手成功");
        log_.Info(L"VT 初始化阶段 4/4：驱动设备和符号表初始化完成");
        log_.Info(L"VT 后端初始化成功");
        WriteDevelopmentDiagnosticsSnapshot(applicationDirectory_, log_, L"VT 初始化成功后");
        initializing_.store(false);
    }

    std::wstring WinUiController::LastInitializationError() const
    {
        std::scoped_lock lock(errorMutex_);
        return lastInitializationError_;
    }

    std::wstring WinUiController::SymbolPreparationStatus() const
    {
        std::scoped_lock lock(symbolStatusMutex_);
        return symbolPreparationStatus_;
    }

    void WinUiController::StopWorkers() noexcept
    {
        symbolStopRequested_.store(true);
        if (initializeWorker_.joinable())
        {
            initializeWorker_.join();
        }
        if (symbolPreparationWorker_.joinable())
        {
            symbolPreparationWorker_.join();
        }
    }
}
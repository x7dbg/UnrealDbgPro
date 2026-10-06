#pragma once

#include "Core.h"
#include "SymbolCache.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace unrealdbg_native
{
    void SetWinUiLogSink(LogSink* sink) noexcept;
    LogSink* GetWinUiLogSink() noexcept;

    // WinUI 前端使用的后端控制器。它只负责原有 DLL/驱动协议、配置和
    // 日志，不持有任何 XAML 控件，因此前端换肤不会改变底层行为。
    class WinUiController final
    {
    public:
        explicit WinUiController(std::wstring applicationDirectory);
        ~WinUiController();

        WinUiController(const WinUiController&) = delete;
        WinUiController& operator=(const WinUiController&) = delete;

        bool Start();
        void PollBackendLogs();
        void DrainLogs(std::vector<LogRecord>& records);

        void InitializeAsync();
        bool IsInitializing() const noexcept { return initializing_.load(); }
        bool IsInitialized() const noexcept { return initialized_.load(); }
        std::wstring LastInitializationError() const;
        bool IsSymbolPreparing() const noexcept { return symbolPreparing_.load(); }
        bool IsSymbolCacheReady() const noexcept { return symbolCacheReady_.load(); }
        bool IsSymbolPreparationFailed() const noexcept { return symbolPreparationFailed_.load(); }
        std::wstring SymbolPreparationStatus() const;

        const std::wstring& ApplicationDirectory() const noexcept { return applicationDirectory_; }
        const std::wstring& SystemName() const noexcept { return systemName_; }
        const std::wstring& SystemVersion() const noexcept { return systemVersion_; }
        const std::wstring& CpuName() const noexcept { return cpuName_; }
        const std::wstring& MemoryText() const noexcept { return memoryText_; }
        const std::wstring& ArchText() const noexcept { return archText_; }
        const std::vector<DebuggerEntry>& Debuggers() const noexcept { return debuggerEntries_; }

        bool AddDebugger(const std::wstring& path);
        bool DeleteDebugger(size_t index);
        bool StartDebugger(size_t index);

        bool TlEnabled() const noexcept { return tlEnabled_; }
        bool TlGetTickCount() const noexcept { return tlGetTickCount_; }
        bool TlBlockResumeThread() const noexcept { return tlBlockResumeThread_; }
        void SetTlEnabled(bool value);
        void SetTlGetTickCount(bool value);
        void SetTlBlockResumeThread(bool value);
        bool SelectForegroundTarget(HWND hostWindow);

        // 前端"刷新"入口：重新从磁盘读取系统信息、调试器列表与 TL 设置。
        void ReloadFromDisk();
        // 前端窗口关闭时有序停止后台工作线程。
        void Shutdown() noexcept;

    private:
        void LoadSystemInformation();
        void LoadDebuggerList();
        bool SaveDebuggerList();
        void LoadTlSettings();
        bool SaveTlSettings();
        void ApplyTlOptions();
        void StartSymbolPreparationAsync();
        void PrepareSymbolsWorker();
        bool WaitForSymbolPreparation();
        void InitializeWorker();
        void StopWorkers() noexcept;

        std::wstring applicationDirectory_;
        LogSink log_;
        NativeApi nativeApi_;
        std::filesystem::path backendLogPath_;
        std::uintmax_t backendLogOffset_{};
        std::string backendPartialLine_;
        std::vector<DebuggerEntry> debuggerEntries_;
        // 系统信息由本地注册表/Win32 API 采集，构造时即为真实值（无“读取中”态）。
        std::wstring systemName_{ L"未知系统" };
        std::wstring systemVersion_{ L"未知内核版本" };
        std::wstring cpuName_{ L"未知处理器" };
        std::wstring memoryText_{ L"未知内存" };
        std::wstring archText_{ L"未知架构" };
        std::thread symbolPreparationWorker_;
        std::thread initializeWorker_;
        mutable std::mutex errorMutex_;
        std::wstring lastInitializationError_;
        mutable std::mutex symbolStatusMutex_;
        std::wstring symbolPreparationStatus_{ L"等待启动时校验" };
        std::wstring symbolPreparationFailureDetails_;
        std::atomic_bool symbolStopRequested_{ false };
        std::atomic_bool symbolPreparing_{ false };
        std::atomic_bool symbolCacheReady_{ false };
        std::atomic_bool symbolPreparationFailed_{ false };
        std::atomic_bool initializing_{ false };
        std::atomic_bool initialized_{ false };
        bool nativeApiLoaded_{};
        bool tlEnabled_{};
        bool tlGetTickCount_{};
        bool tlBlockResumeThread_{};
        DWORD targetProcessId_{};
        HWND hostWindow_{};
    };
}

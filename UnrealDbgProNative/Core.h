#pragma once

#include <Windows.h>

// Windows.h（经 timeapi.h）把 GetCurrentTime 定义成函数式宏：
//     #define GetCurrentTime() timeGetTime()
// 这个宏会破坏 Windows App SDK 生成的 C++/WinRT 投影头：动画投影
// （Microsoft.UI.Xaml.Media.Animation.h）里 Storyboard 的 ABI 适配类含有同名成员
//     int32_t __stdcall GetCurrentTime(int64_t* result) noexcept final try
// 宏展开后形参名 result 被整段剥离，编译随即报
// "C3861: result: 找不到标识符" / "C2065: result: 未声明的标识符"。
// 只要同一个翻译单元里同时出现 Windows.h 与该投影头就会踩到，且若在投影头已开始
// 解析之后再撤销宏，还会因声明与定义不一致报 C2039。
//
// 因此在 Windows.h 之后、任何 winrt 投影头之前无条件撤销该宏。本文件是工程内
// winrt 头的必经入口，集中处理可保证所有翻译单元看到一致的投影头文本。
// 撤销后若代码调用 Win32 的 GetCurrentTime()，会正常解析到 winuser.h 的同名函数。
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace unrealdbg_native
{
    enum class LogLevel
    {
        Debug,
        Info,
        Error,
    };

    struct LogRecord
    {
        LogLevel level{};
        std::wstring text;
    };

    // Human-readable diagnostics kept in Chinese so a numeric Win32/HRESULT
    // value is immediately actionable in both the file log and the UI.
    struct ErrorExplanation
    {
        std::wstring reason;
        std::wstring solution;
    };

    class LogSink final
    {
    public:
        explicit LogSink(std::wstring applicationDirectory);

        LogSink(const LogSink&) = delete;
        LogSink& operator=(const LogSink&) = delete;

        void AttachRichEdit(HWND richEdit) noexcept;
        void DetachRichEdit() noexcept;
        void DrainToRichEdit();
        // WinUI/XAML 前端不使用 RichEdit，直接取出线程安全的待显示记录。
        // 记录仍然同时写入 Log\log.ini，旧版 Win32 前端行为不变。
        void DrainPending(std::vector<LogRecord>& records);
        void Debug(std::wstring_view text);
        void Info(std::wstring_view text);
        void Error(std::wstring_view text);
        // 将后端 DLL 的日志转发到同一个前端日志管道，保留原始级别以
        // 便 UI 继续使用蓝/绿/红色区分调试、信息和错误。
        void External(LogLevel level, std::wstring_view text);
        void Error(std::wstring_view operation, DWORD error);
        void ErrorHresult(std::wstring_view operation, HRESULT status);
        // 每个进程都有独立会话日志；log.ini 仍保留为方便快速查看的滚动汇总。
        [[nodiscard]] const std::wstring& SessionId() const noexcept { return sessionId_; }
        [[nodiscard]] const std::wstring& SessionLogPath() const noexcept { return sessionLogPath_; }

    private:
        void Write(LogLevel level, std::wstring_view text);
        void AppendToFile(LogLevel level, std::wstring_view text) noexcept;

        std::wstring applicationDirectory_;
        std::wstring sessionId_;
        std::wstring sessionLogPath_;
        std::uint64_t sequence_{};
        std::uintmax_t sessionLogBytes_{};
        bool sessionLogTruncated_{};
        HWND richEdit_{};
        std::mutex mutex_;
        std::vector<LogRecord> pending_;
    };

    struct DebuggerEntry
    {
        std::wstring name;
        std::wstring path;
    };

    [[nodiscard]] std::wstring GetApplicationDirectory();
    // 真实运行目录：优先 exe 同级目录，其次当前工作目录与 exe 上级目录下的常见
    // 构建输出目录（x64\WinUI\Release 等），判定标准是同时存在 bin\UnrealDbgProDll.dll
    // 与 Config\Config.ini。用于消除"必须把工作目录切到产物目录才能启动"的路径依赖。
    [[nodiscard]] std::wstring GetRuntimeDirectory();
    // 当前进程是否以提升（管理员）权限运行。驱动能力依赖提升权限，
    // 界面需要把“权限”与“后端”分开表达，因此判定统一收敛到这里。
    [[nodiscard]] bool IsProcessElevated() noexcept;
    [[nodiscard]] std::wstring GetSystemErrorMessage(DWORD error);
    [[nodiscard]] ErrorExplanation ExplainWin32Error(DWORD error);
    [[nodiscard]] ErrorExplanation ExplainHresult(HRESULT status);
    [[nodiscard]] bool FileExists(const std::wstring& path);
    [[nodiscard]] bool DirectoryExists(const std::wstring& path);
    [[nodiscard]] bool ParseDebuggerEntry(std::wstring_view input, DebuggerEntry& entry);
    [[nodiscard]] std::wstring SerializeDebuggerEntry(const DebuggerEntry& entry);
    [[nodiscard]] bool ExtractJsonString(std::wstring_view json, std::wstring_view key, std::wstring& value);
    [[nodiscard]] std::wstring QueryWmiString(std::wstring_view className, std::wstring_view propertyName, LogSink& log);
    [[nodiscard]] std::wstring GetFileVersionString(const std::wstring& filePath, LogSink& log);
    [[nodiscard]] bool EnableDebugPrivilege(LogSink& log);
    [[nodiscard]] uintptr_t FindRemoteModuleBase(DWORD processId, std::wstring_view moduleName, LogSink& log);
    // 开发测试用的本地环境快照：只读采集系统、部署文件和服务状态，不加载、
    // 安装或停止任何驱动。每个重要阶段调用一次，方便跨会话复盘问题。
    void WriteDevelopmentDiagnosticsSnapshot(const std::wstring& applicationDirectory, LogSink& log,
        std::wstring_view checkpoint);

    class NativeApi final
    {
    public:
        explicit NativeApi(LogSink& log) noexcept;
        ~NativeApi();

        NativeApi(const NativeApi&) = delete;
        NativeApi& operator=(const NativeApi&) = delete;

        [[nodiscard]] bool Load(const std::wstring& applicationDirectory);
        void Unload() noexcept;
        [[nodiscard]] bool IsLoaded() const noexcept;
        [[nodiscard]] bool Initialize(std::uint64_t key);
        [[nodiscard]] bool Shutdown();
        [[nodiscard]] bool StartProcess(const std::wstring& executablePath, const std::wstring& applicationDirectory);
        [[nodiscard]] bool BlockGameResumeThread(DWORD processId);

    private:
        using InitializeFn = BOOL(*)(ULONG64);
        using StartProcessFn = BOOL(*)(wchar_t*, wchar_t*);
        using GetFileVersionFn = void(*)(wchar_t*, wchar_t*);
        using BlockResumeThreadFn = BOOL(*)(DWORD);
        using ShutdownFn = BOOL(*)();

        LogSink& log_;
        HMODULE module_{};
        InitializeFn initialize_{};
        StartProcessFn startProcess_{};
        GetFileVersionFn getFileVersion_{};
        BlockResumeThreadFn blockResumeThread_{};
        ShutdownFn shutdown_{};
        bool initializedByThisApi_{};
    };

    class EncryptionApi final
    {
    public:
        explicit EncryptionApi(LogSink& log) noexcept;
        ~EncryptionApi();

        EncryptionApi(const EncryptionApi&) = delete;
        EncryptionApi& operator=(const EncryptionApi&) = delete;

        [[nodiscard]] bool Load(const std::wstring& applicationDirectory);
        void Unload() noexcept;
        [[nodiscard]] bool IsLoaded() const noexcept;
        [[nodiscard]] bool DecryptFile(const std::wstring& filePath, const std::wstring& key, std::wstring& plaintext);

    private:
        using DecryptFileFn = int(*)(wchar_t*, wchar_t*, wchar_t*);

        LogSink& log_;
        HMODULE module_{};
        DecryptFileFn decryptFile_{};
    };

    // Pure core checks used by the executable's --self-test mode. They do not
    // load drivers, inject DLLs, alter files, or require administrator rights.
    [[nodiscard]] bool RunCoreSelfTests(std::wstring& failure);
}
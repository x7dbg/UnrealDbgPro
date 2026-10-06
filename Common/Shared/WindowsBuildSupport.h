#pragma once

// This table is deliberately shared by tools and the user-mode loader only.
// It does not assert that a build is production-certified: that status is
// earned only after the matching snapshot-VM test case has passed.
#include <cstdint>

namespace unrealdbg
{
namespace windows_support
{
    enum class SupportLevel : std::uint8_t
    {
        Validated,
        DevelopmentBaseline,
        Blocked
    };

    struct BuildEntry
    {
        std::uint32_t build;
        std::uint32_t minimumUbr;
        std::uint32_t maximumUbr;
        const wchar_t* marketingName;
        const wchar_t* bridgeDriver;
        SupportLevel level;
        const wchar_t* note;
    };

    // A build is selected only when both build number and UBR are in an
    // explicit row.  New Windows releases therefore fail closed instead of
    // silently taking the Win11 driver merely because their build is >= 22000.
    static const BuildEntry kBuildTable[] =
    {
        { 19045, 0, 99999, L"Windows 10 22H2", L"DbgkSysWin10.sys", SupportLevel::DevelopmentBaseline,
            L"开发基线；尚需对应快照虚拟机验证。" },
        { 22000, 0, 99999, L"Windows 11 21H2", L"DbgkSysWin11.sys", SupportLevel::DevelopmentBaseline,
            L"开发基线；尚需对应快照虚拟机验证。" },
        { 22621, 0, 99999, L"Windows 11 22H2", L"DbgkSysWin11.sys", SupportLevel::DevelopmentBaseline,
            L"开发基线；尚需对应快照虚拟机验证。" },
        { 22631, 0, 99999, L"Windows 11 23H2", L"DbgkSysWin11.sys", SupportLevel::DevelopmentBaseline,
            L"开发基线；尚需对应快照虚拟机验证。" },
        { 26100, 0, 99999, L"Windows 11 24H2", L"DbgkSysWin11.sys", SupportLevel::DevelopmentBaseline,
            L"开发基线；尚需对应快照虚拟机验证。" },
        { 26200, 0, 99999, L"Windows 11 25H2", L"DbgkSysWin11.sys", SupportLevel::DevelopmentBaseline,
            L"当前开发机基线；必须在隔离快照虚拟机完成加载、卸载及休眠/重启回归。" },
    };

    inline const BuildEntry* Find(const std::uint32_t build, const std::uint32_t ubr) noexcept
    {
        for (const BuildEntry& entry : kBuildTable)
        {
            if (entry.build == build && ubr >= entry.minimumUbr && ubr <= entry.maximumUbr)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    inline const wchar_t* LevelText(const SupportLevel level) noexcept
    {
        switch (level)
        {
        case SupportLevel::Validated: return L"已在测试矩阵验证";
        case SupportLevel::DevelopmentBaseline: return L"开发基线（待虚拟机验证）";
        default: return L"已阻止";
        }
    }
}
}

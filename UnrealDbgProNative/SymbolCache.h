#pragma once

#include "Core.h"

#include <atomic>
#include <string>

namespace unrealdbg_native
{
    // 这是启动阶段的“文件符号准备”，只读取 Windows 模块、校验或下载
    // 对应 PDB；绝不加载 VT_Driver，也绝不向驱动发送 IOCTL。
    struct SymbolPreparationResult
    {
        bool success{};
        unsigned int cacheHits{};
        unsigned int downloads{};
        std::wstring summary;
        std::wstring failureDetails;
    };

    // 缓存键由每个 PE 模块的 CodeView PDB 名称、GUID 与 Age 构成。Windows
    // 累积更新即使没有改变显示版本，只要模块的 PDB 标识改变也会自动失效。
    // 注意：Microsoft 公开 PDB 的内部 Age 可能因公开符号处理而不同；下载后
    // 以 GUID 严格校验内容归属，Age 仅用于定位正确的符号服务器缓存项。
    [[nodiscard]] SymbolPreparationResult PrepareSystemSymbolCache(
        const std::wstring& applicationDirectory, LogSink& log, const std::atomic_bool& stopRequested);

    // 不访问网络、磁盘上的系统模块或驱动，供 --self-test 验证关键格式化规则。
    [[nodiscard]] bool RunSymbolCacheSelfTests(std::wstring& failure);
}

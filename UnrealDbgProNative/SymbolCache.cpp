#include "SymbolCache.h"

#include <Wininet.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#pragma comment(lib, "Wininet.lib")

namespace
{
    constexpr wchar_t kSymbolServerHost[] = L"msdl.microsoft.com";
    constexpr wchar_t kSymbolServerPath[] = L"/download/symbols/";
    constexpr wchar_t kCacheManifestName[] = L"symbol-cache.ini";
    constexpr std::uint64_t kMaximumPdbBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr std::array<char, 32> kMsf7Signature = {
        'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', ' ', 'C', '/', 'C', '+', '+', ' ',
        'M', 'S', 'F', ' ', '7', '.', '0', '0', '\r', '\n', 0x1a, 'D', 'S', 0, 0, 0 };

    struct ModuleSymbolIdentity
    {
        std::wstring moduleName;
        std::wstring modulePath;
        std::wstring moduleVersion;
        std::wstring pdbFileName;
        GUID guid{};
        DWORD age{};
    };

    struct PdbStreamHeader
    {
        DWORD version;
        DWORD signature;
        DWORD age;
        GUID guid;
    };

    struct MsfSuperBlock
    {
        std::array<char, 32> magic;
        DWORD blockSize;
        DWORD freeBlockMapBlock;
        DWORD numberOfBlocks;
        DWORD directoryBytes;
        DWORD unknown;
        DWORD blockMapAddress;
    };

    [[nodiscard]] bool IsRangeInside(const size_t offset, const size_t length, const size_t total) noexcept
    {
        return offset <= total && length <= total - offset;
    }

    template <typename T>
    [[nodiscard]] bool ReadObject(const std::vector<std::byte>& bytes, const size_t offset, T& value) noexcept
    {
        if (!IsRangeInside(offset, sizeof(T), bytes.size())) return false;
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return true;
    }

    [[nodiscard]] std::wstring LastErrorText(const DWORD error)
    {
        return unrealdbg_native::GetSystemErrorMessage(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
    }

    [[nodiscard]] std::wstring GuidAndAgeKey(const GUID& guid, const DWORD age)
    {
        std::wostringstream text;
        text << std::uppercase << std::hex << std::setfill(L'0')
            << std::setw(8) << guid.Data1
            << std::setw(4) << guid.Data2
            << std::setw(4) << guid.Data3;
        for (const unsigned char item : guid.Data4) text << std::setw(2) << static_cast<unsigned int>(item);
        // Symbol Server 的 Age 是十六进制、无前导零的 GUID+Age 键的一部分。
        text << std::uppercase << std::hex << age;
        return text.str();
    }

    [[nodiscard]] bool IsSafePdbFileName(const std::wstring& name) noexcept
    {
        if (name.empty() || name.size() > MAX_PATH) return false;
        if (_wcsicmp(std::filesystem::path(name).extension().c_str(), L".pdb") != 0) return false;
        if (std::filesystem::path(name).filename() != std::filesystem::path(name)) return false;
        return std::all_of(name.begin(), name.end(), [](const wchar_t character)
        {
            return character >= 0x20 && character != L'\\' && character != L'/' && character != L':' &&
                character != L'*' && character != L'?' && character != L'\"' && character != L'<' &&
                character != L'>' && character != L'|';
        });
    }

    [[nodiscard]] bool ReadBinaryFile(const std::filesystem::path& path, std::vector<std::byte>& data, std::wstring& reason)
    {
        std::error_code error;
        const auto byteCount = std::filesystem::file_size(path, error);
        if (error || byteCount == 0 || byteCount > static_cast<std::uintmax_t>(std::numeric_limits<size_t>::max()))
        {
            reason = L"无法读取文件大小：" + path.wstring();
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            reason = L"无法打开文件：" + path.wstring();
            return false;
        }
        data.resize(static_cast<size_t>(byteCount));
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input || static_cast<size_t>(input.gcount()) != data.size())
        {
            reason = L"读取文件内容不完整：" + path.wstring();
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<size_t> RvaToFileOffset(const std::vector<std::byte>& image, const size_t ntOffset,
        const IMAGE_FILE_HEADER& fileHeader, const size_t optionalHeaderOffset, const DWORD rva, const size_t bytesNeeded)
    {
        const size_t sectionOffset = optionalHeaderOffset + fileHeader.SizeOfOptionalHeader;
        if (!IsRangeInside(sectionOffset, static_cast<size_t>(fileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER), image.size()))
            return std::nullopt;
        for (WORD index = 0; index < fileHeader.NumberOfSections; ++index)
        {
            IMAGE_SECTION_HEADER section{};
            const size_t current = sectionOffset + static_cast<size_t>(index) * sizeof(section);
            if (!ReadObject(image, current, section)) return std::nullopt;
            const DWORD virtualSize = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
            if (rva < section.VirtualAddress || rva - section.VirtualAddress > virtualSize) continue;
            const size_t relative = static_cast<size_t>(rva - section.VirtualAddress);
            const size_t rawOffset = static_cast<size_t>(section.PointerToRawData) + relative;
            if (relative <= section.SizeOfRawData && IsRangeInside(rawOffset, bytesNeeded, image.size())) return rawOffset;
            return std::nullopt;
        }
        // 调试目录有时位于 PE 头映射范围内。
        if (rva < ntOffset && IsRangeInside(rva, bytesNeeded, image.size())) return static_cast<size_t>(rva);
        return std::nullopt;
    }

    [[nodiscard]] bool ReadModuleIdentity(const std::filesystem::path& modulePath, ModuleSymbolIdentity& identity, std::wstring& reason)
    {
        std::vector<std::byte> image;
        if (!ReadBinaryFile(modulePath, image, reason)) return false;

        IMAGE_DOS_HEADER dos{};
        if (!ReadObject(image, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
        {
            reason = L"模块不是有效的 PE 文件（DOS 头无效）";
            return false;
        }
        const size_t ntOffset = static_cast<size_t>(dos.e_lfanew);
        DWORD signature{};
        IMAGE_FILE_HEADER fileHeader{};
        if (!ReadObject(image, ntOffset, signature) || signature != IMAGE_NT_SIGNATURE ||
            !ReadObject(image, ntOffset + sizeof(signature), fileHeader))
        {
            reason = L"模块不是有效的 PE 文件（NT 头无效）";
            return false;
        }
        const size_t optionalHeaderOffset = ntOffset + sizeof(signature) + sizeof(fileHeader);
        WORD optionalMagic{};
        if (!ReadObject(image, optionalHeaderOffset, optionalMagic))
        {
            reason = L"模块缺少可选头";
            return false;
        }
        IMAGE_DATA_DIRECTORY debugDirectory{};
        if (optionalMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            IMAGE_OPTIONAL_HEADER64 optional{};
            if (!ReadObject(image, optionalHeaderOffset, optional) || optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
            {
                reason = L"模块的 64 位可选头无效";
                return false;
            }
            debugDirectory = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        }
        else if (optionalMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            IMAGE_OPTIONAL_HEADER32 optional{};
            if (!ReadObject(image, optionalHeaderOffset, optional) || optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
            {
                reason = L"模块的 32 位可选头无效";
                return false;
            }
            debugDirectory = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        }
        else
        {
            reason = L"模块的可选头类型不受支持";
            return false;
        }
        if (debugDirectory.VirtualAddress == 0 || debugDirectory.Size < sizeof(IMAGE_DEBUG_DIRECTORY))
        {
            reason = L"模块未包含 CodeView 调试目录";
            return false;
        }
        const auto debugOffset = RvaToFileOffset(image, ntOffset, fileHeader, optionalHeaderOffset,
            debugDirectory.VirtualAddress, debugDirectory.Size);
        if (!debugOffset)
        {
            reason = L"模块调试目录越界";
            return false;
        }
        const size_t entryCount = debugDirectory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
        for (size_t index = 0; index < entryCount; ++index)
        {
            IMAGE_DEBUG_DIRECTORY entry{};
            if (!ReadObject(image, *debugOffset + index * sizeof(entry), entry)) break;
            if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.SizeOfData < sizeof(DWORD) + sizeof(GUID) + sizeof(DWORD)) continue;
            const size_t dataOffset = entry.PointerToRawData;
            if (!IsRangeInside(dataOffset, entry.SizeOfData, image.size())) continue;
            DWORD codeViewSignature{};
            if (!ReadObject(image, dataOffset, codeViewSignature) || codeViewSignature != 0x53445352) continue; // RSDS
            GUID guid{};
            DWORD age{};
            const size_t guidOffset = dataOffset + sizeof(DWORD);
            if (!ReadObject(image, guidOffset, guid) || !ReadObject(image, guidOffset + sizeof(GUID), age)) continue;
            const size_t nameOffset = guidOffset + sizeof(GUID) + sizeof(DWORD);
            const size_t nameBytes = entry.SizeOfData - (sizeof(DWORD) + sizeof(GUID) + sizeof(DWORD));
            const auto* name = reinterpret_cast<const char*>(image.data() + nameOffset);
            const auto* end = static_cast<const char*>(std::memchr(name, '\0', nameBytes));
            if (end == nullptr)
            {
                reason = L"CodeView PDB 名称没有以空字符结束";
                return false;
            }
            const int wideLength = MultiByteToWideChar(CP_ACP, 0, name, static_cast<int>(end - name), nullptr, 0);
            if (wideLength <= 0)
            {
                reason = L"无法转换 CodeView PDB 名称";
                return false;
            }
            std::wstring fullPdbName(static_cast<size_t>(wideLength), L'\0');
            if (MultiByteToWideChar(CP_ACP, 0, name, static_cast<int>(end - name), fullPdbName.data(), wideLength) != wideLength)
            {
                reason = L"无法转换 CodeView PDB 名称";
                return false;
            }
            const std::wstring pdbFileName = std::filesystem::path(fullPdbName).filename().wstring();
            if (!IsSafePdbFileName(pdbFileName))
            {
                reason = L"CodeView PDB 名称不安全或格式不正确";
                return false;
            }
            identity.moduleName = modulePath.filename().wstring();
            identity.modulePath = modulePath.wstring();
            // 版本信息由调用者使用实际日志对象读取；此处仅负责无副作用的 PE/CodeView 解析。
            identity.pdbFileName = pdbFileName;
            identity.guid = guid;
            identity.age = age;
            return true;
        }
        reason = L"模块中未找到 RSDS CodeView 调试记录";
        return false;
    }

    [[nodiscard]] bool ReadPdbIdentity(const std::filesystem::path& pdbPath, GUID& guid, DWORD& age, std::wstring& reason)
    {
        std::vector<std::byte> data;
        if (!ReadBinaryFile(pdbPath, data, reason)) return false;
        MsfSuperBlock super{};
        if (!ReadObject(data, 0, super) || super.magic != kMsf7Signature)
        {
            reason = L"PDB 不是有效的 MSF 7.00 文件";
            return false;
        }
        if (super.blockSize < 512 || super.blockSize > 1024 * 1024 || (super.blockSize & (super.blockSize - 1)) != 0 ||
            super.numberOfBlocks == 0 || super.directoryBytes < sizeof(DWORD))
        {
            reason = L"PDB 的 MSF 块参数无效";
            return false;
        }
        const std::uint64_t declaredBytes = static_cast<std::uint64_t>(super.blockSize) * super.numberOfBlocks;
        if (declaredBytes > data.size())
        {
            reason = L"PDB 文件不完整";
            return false;
        }
        const size_t directoryBlockCount = (static_cast<size_t>(super.directoryBytes) + super.blockSize - 1) / super.blockSize;
        const size_t blockMapBytes = directoryBlockCount * sizeof(DWORD);
        const size_t blockMapOffset = static_cast<size_t>(super.blockMapAddress) * super.blockSize;
        if (directoryBlockCount == 0 || !IsRangeInside(blockMapOffset, blockMapBytes, data.size()))
        {
            reason = L"PDB 目录块映射越界";
            return false;
        }
        std::vector<std::byte> directory;
        directory.reserve(super.directoryBytes);
        for (size_t index = 0; index < directoryBlockCount; ++index)
        {
            DWORD block{};
            if (!ReadObject(data, blockMapOffset + index * sizeof(DWORD), block) || block >= super.numberOfBlocks)
            {
                reason = L"PDB 目录块编号无效";
                return false;
            }
            const size_t sourceOffset = static_cast<size_t>(block) * super.blockSize;
            const size_t bytesToCopy = std::min<size_t>(super.blockSize, super.directoryBytes - directory.size());
            if (!IsRangeInside(sourceOffset, bytesToCopy, data.size()))
            {
                reason = L"PDB 目录块内容越界";
                return false;
            }
            directory.insert(directory.end(), data.begin() + sourceOffset, data.begin() + sourceOffset + bytesToCopy);
        }
        DWORD streamCount{};
        if (!ReadObject(directory, 0, streamCount) || streamCount < 2 || streamCount > 65536)
        {
            reason = L"PDB 流目录无效";
            return false;
        }
        const size_t streamSizesOffset = sizeof(DWORD);
        if (!IsRangeInside(streamSizesOffset, static_cast<size_t>(streamCount) * sizeof(DWORD), directory.size()))
        {
            reason = L"PDB 流大小表越界";
            return false;
        }
        std::vector<DWORD> streamSizes(streamCount);
        for (DWORD index = 0; index < streamCount; ++index)
        {
            if (!ReadObject(directory, streamSizesOffset + static_cast<size_t>(index) * sizeof(DWORD), streamSizes[index]))
            {
                reason = L"PDB 流大小表无法读取";
                return false;
            }
        }
        size_t streamBlocksOffset = streamSizesOffset + static_cast<size_t>(streamCount) * sizeof(DWORD);
        std::vector<DWORD> infoBlocks;
        for (DWORD index = 0; index < streamCount; ++index)
        {
            if (streamSizes[index] == 0xFFFFFFFF) continue;
            const size_t blocks = (static_cast<size_t>(streamSizes[index]) + super.blockSize - 1) / super.blockSize;
            if (!IsRangeInside(streamBlocksOffset, blocks * sizeof(DWORD), directory.size()))
            {
                reason = L"PDB 流块表越界";
                return false;
            }
            if (index == 1)
            {
                if (streamSizes[index] < sizeof(PdbStreamHeader) || blocks == 0)
                {
                    reason = L"PDB 信息流长度无效";
                    return false;
                }
                infoBlocks.resize(blocks);
                for (size_t blockIndex = 0; blockIndex < blocks; ++blockIndex)
                {
                    if (!ReadObject(directory, streamBlocksOffset + blockIndex * sizeof(DWORD), infoBlocks[blockIndex]) ||
                        infoBlocks[blockIndex] >= super.numberOfBlocks)
                    {
                        reason = L"PDB 信息流块编号无效";
                        return false;
                    }
                }
            }
            streamBlocksOffset += blocks * sizeof(DWORD);
        }
        if (infoBlocks.empty())
        {
            reason = L"PDB 缺少信息流";
            return false;
        }
        std::array<std::byte, sizeof(PdbStreamHeader)> headerBytes{};
        size_t copied{};
        for (const DWORD block : infoBlocks)
        {
            const size_t sourceOffset = static_cast<size_t>(block) * super.blockSize;
            const size_t amount = std::min<size_t>(super.blockSize, headerBytes.size() - copied);
            if (!IsRangeInside(sourceOffset, amount, data.size()))
            {
                reason = L"PDB 信息流内容越界";
                return false;
            }
            std::memcpy(headerBytes.data() + copied, data.data() + sourceOffset, amount);
            copied += amount;
            if (copied == headerBytes.size()) break;
        }
        PdbStreamHeader header{};
        std::memcpy(&header, headerBytes.data(), sizeof(header));
        guid = header.guid;
        age = header.age;
        return true;
    }

    [[nodiscard]] bool VerifyPdb(const std::filesystem::path& pdbPath, const ModuleSymbolIdentity& module, std::wstring& reason)
    {
        GUID guid{};
        DWORD age{};
        reason.clear();
        if (!ReadPdbIdentity(pdbPath, guid, age, reason)) return false;
        if (std::memcmp(&guid, &module.guid, sizeof(GUID)) != 0)
        {
            reason = L"PDB GUID 与当前系统模块不一致";
            return false;
        }
        // Windows 的 CodeView Age 是符号服务器路径/缓存键的一部分。微软公开
        // PDB 经 pdbcopy 等处理后，其 PDB 信息流中的 Age 可能不同，但 GUID
        // 保持不变；原版 Loader 也是只按 GUID 验证。将 Age 再作为内容校验会把
        // 正确的公开 PDB 误报为损坏或不匹配。
        if (age != module.age)
        {
            reason = L"公开 PDB 内部 Age 为 " + std::to_wstring(age) + L"，模块 CodeView Age 为 " +
                std::to_wstring(module.age) + L"；GUID 已一致，按 Microsoft 公开符号规则接受";
        }
        return true;
    }

    [[nodiscard]] std::wstring EscapeUrlPathComponent(const std::wstring& value)
    {
        std::wostringstream output;
        output << std::uppercase << std::hex;
        for (const wchar_t character : value)
        {
            const bool unreserved = (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z') ||
                (character >= L'0' && character <= L'9') || character == L'.' || character == L'_' || character == L'-';
            if (unreserved) output << character;
            else if (character <= 0x7f) output << L'%' << std::setw(2) << std::setfill(L'0') << static_cast<unsigned int>(character);
            else return {};
        }
        return output.str();
    }

    // InternetOpenUrlW / InternetReadFile 都是同步调用。关闭其句柄会使正在
    // 阻塞的网络调用尽快返回，从而让启动期后台线程具备确定的总超时上限。
    class InternetDownloadHandles final
    {
    public:
        void SetSession(const HINTERNET handle) noexcept { Set(handle, true); }
        void SetRequest(const HINTERNET handle) noexcept { Set(handle, false); }

        void CloseAll() noexcept
        {
            HINTERNET request{};
            HINTERNET session{};
            {
                std::scoped_lock lock(mutex_);
                if (closed_) return;
                closed_ = true;
                request = request_;
                session = session_;
                request_ = nullptr;
                session_ = nullptr;
            }
            if (request) InternetCloseHandle(request);
            if (session) InternetCloseHandle(session);
        }

    private:
        void Set(const HINTERNET handle, const bool isSession) noexcept
        {
            bool closeNow{};
            {
                std::scoped_lock lock(mutex_);
                if (closed_) closeNow = true;
                else if (isSession) session_ = handle;
                else request_ = handle;
            }
            if (closeNow && handle) InternetCloseHandle(handle);
        }

        std::mutex mutex_;
        HINTERNET session_{};
        HINTERNET request_{};
        bool closed_{};
    };

    enum class DownloadTimeoutReason
    {
        None,
        NoProgress,
        Overall,
    };

    [[nodiscard]] const wchar_t* DownloadTimeoutText(const DownloadTimeoutReason reason) noexcept
    {
        switch (reason)
        {
        case DownloadTimeoutReason::NoProgress:
            return L"符号下载连续 20 秒没有任何数据进度";
        case DownloadTimeoutReason::Overall:
            return L"符号下载即使持续有进度也超过了 3 分钟总时限";
        default:
            return L"符号下载超时";
        }
    }

    [[nodiscard]] bool DownloadWithSystemCurl(const std::wstring& url, const std::filesystem::path& partialPath,
        const std::atomic_bool& stopRequested, DWORD& error, std::wstring& reason)
    {
        wchar_t systemDirectory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        const std::filesystem::path curlPath = length == 0 || length >= std::size(systemDirectory) ?
            std::filesystem::path(L"curl.exe") : std::filesystem::path(systemDirectory) / L"curl.exe";
        if (!std::filesystem::exists(curlPath))
        {
            error = ERROR_FILE_NOT_FOUND;
            reason = L"当前 Windows 未提供系统 curl.exe，无法执行 HTTPS 符号下载";
            return false;
        }
        // --continue-at - 使同一 PDB 身份目录中的 .partial 文件成为真正的
        // 断点续传缓存；绝不重新下载已收到的字节。禁止降级到 HTTP，也不读取
        // 用户目录中的 curl 配置，避免本机配置改变下载安全性或行为。
        std::wstring commandLine = L"\"" + curlPath.wstring() + L"\" --disable --fail --location --silent --show-error "
            L"--proto \"=https\" --proto-redir \"=https\" --connect-timeout 15 --continue-at - --output \"" +
            partialPath.wstring() + L"\" \"" + url + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(curlPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
            partialPath.parent_path().c_str(), &startup, &process))
        {
            error = GetLastError();
            reason = L"无法启动系统 curl.exe 执行 HTTPS 符号下载";
            return false;
        }
        CloseHandle(process.hThread);
        const auto started = std::chrono::steady_clock::now();
        auto lastProgress = started;
        LARGE_INTEGER lastSize{};
        HANDLE existingPartial = CreateFileW(partialPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (existingPartial != INVALID_HANDLE_VALUE)
        {
            GetFileSizeEx(existingPartial, &lastSize);
            CloseHandle(existingPartial);
        }
        DownloadTimeoutReason timeout = DownloadTimeoutReason::None;
        for (;;)
        {
            if (WaitForSingleObject(process.hProcess, 250) == WAIT_OBJECT_0) break;
            LARGE_INTEGER currentSize{};
            HANDLE file = CreateFileW(partialPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                if (GetFileSizeEx(file, &currentSize) && currentSize.QuadPart > lastSize.QuadPart)
                {
                    lastSize = currentSize;
                    lastProgress = std::chrono::steady_clock::now();
                }
                CloseHandle(file);
            }
            const auto now = std::chrono::steady_clock::now();
            if (stopRequested.load()) { error = ERROR_CANCELLED; reason = L"符号准备已取消"; TerminateProcess(process.hProcess, error); break; }
            if (now - started >= std::chrono::minutes(3)) { timeout = DownloadTimeoutReason::Overall; TerminateProcess(process.hProcess, ERROR_TIMEOUT); break; }
            if (now - lastProgress >= std::chrono::seconds(20)) { timeout = DownloadTimeoutReason::NoProgress; TerminateProcess(process.hProcess, ERROR_TIMEOUT); break; }
        }
        WaitForSingleObject(process.hProcess, 5000);
        DWORD exitCode{};
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);
        if (timeout != DownloadTimeoutReason::None) { error = ERROR_TIMEOUT; reason = DownloadTimeoutText(timeout); return false; }
        if (stopRequested.load()) return false;
        if (exitCode != 0) { error = ERROR_NETWORK_UNREACHABLE; reason = L"系统 curl.exe 下载 PDB 失败，退出码：" + std::to_wstring(exitCode); return false; }
        return true;
    }

    [[nodiscard]] bool DownloadPdb(const ModuleSymbolIdentity& module, const std::filesystem::path& partialPath,
        const std::atomic_bool& stopRequested, DWORD& error, std::wstring& reason)
    {
        const std::wstring escapedName = EscapeUrlPathComponent(module.pdbFileName);
        if (escapedName.empty())
        {
            error = ERROR_INVALID_NAME;
            reason = L"PDB 文件名无法安全编码为下载地址";
            return false;
        }
        const std::wstring url = L"https://" + std::wstring(kSymbolServerHost) + kSymbolServerPath + escapedName + L"/" +
            GuidAndAgeKey(module.guid, module.age) + L"/" + escapedName;
        // curl.exe 是 Windows 11 自带的受系统维护 HTTPS 客户端。通过临时文件
        // 实际增长判断进度，因此能严格执行“20 秒无进度 / 3 分钟总时限”。
        return DownloadWithSystemCurl(url, partialPath, stopRequested, error, reason);
#if 0 // 保留到下一次整理时删除：旧 WinINet 同步实现无法可靠中断阻塞请求。
        if (stopRequested.load())
        {
            error = ERROR_CANCELLED;
            reason = L"符号准备已取消";
            return false;
        }
        // 使用用户既有的网络/代理设置，并禁止 HTTPS 降级到 HTTP。WinINet 直接
        // 读取响应流，避免 URLMon 在部分 Win11 网络环境的重定向阶段无进度等待。
        HINTERNET session = InternetOpenW(L"UnrealDbgNative/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
            nullptr, nullptr, 0);
        if (!session)
        {
            error = GetLastError();
            reason = L"无法创建符号下载网络会话";
            return false;
        }
        InternetDownloadHandles handles;
        handles.SetSession(session);
        std::atomic_bool downloadFinished{ false };
        std::atomic<DownloadTimeoutReason> timeoutReason{ DownloadTimeoutReason::None };
        std::mutex timeoutMutex;
        std::condition_variable timeoutCondition;
        std::mutex progressMutex;
        const auto downloadStarted = std::chrono::steady_clock::now();
        auto lastProgress = downloadStarted;
        std::thread timeoutWatchdog([&]
        {
            for (;;)
            {
                std::unique_lock lock(timeoutMutex);
                if (timeoutCondition.wait_for(lock, std::chrono::milliseconds(250), [&]
                    { return downloadFinished.load(); })) return;
                lock.unlock();
                const auto now = std::chrono::steady_clock::now();
                DownloadTimeoutReason reason = DownloadTimeoutReason::None;
                if (now - downloadStarted >= std::chrono::minutes(3)) reason = DownloadTimeoutReason::Overall;
                else
                {
                    std::scoped_lock progressLock(progressMutex);
                    if (now - lastProgress >= std::chrono::seconds(20)) reason = DownloadTimeoutReason::NoProgress;
                }
                if (reason != DownloadTimeoutReason::None)
                {
                    timeoutReason.store(reason);
                    handles.CloseAll();
                    return;
                }
            }
        });
        const auto finishDownload = [&]()
        {
            downloadFinished.store(true);
            timeoutCondition.notify_one();
            if (timeoutWatchdog.joinable()) timeoutWatchdog.join();
            handles.CloseAll();
        };
        DWORD networkOperationTimeout = 15000;
        InternetSetOptionW(session, INTERNET_OPTION_CONNECT_TIMEOUT, &networkOperationTimeout, sizeof(networkOperationTimeout));
        InternetSetOptionW(session, INTERNET_OPTION_SEND_TIMEOUT, &networkOperationTimeout, sizeof(networkOperationTimeout));
        InternetSetOptionW(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &networkOperationTimeout, sizeof(networkOperationTimeout));
        constexpr DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE |
            INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_NO_UI;
        HINTERNET request = InternetOpenUrlW(session, url.c_str(), nullptr, 0, flags, 0);
        handles.SetRequest(request);
        if (!request)
        {
            const DWORD networkError = GetLastError();
            finishDownload();
            const DownloadTimeoutReason timeout = timeoutReason.load();
            error = timeout != DownloadTimeoutReason::None ? ERROR_TIMEOUT : networkError;
            reason = timeout != DownloadTimeoutReason::None ? DownloadTimeoutText(timeout) : L"无法连接 Microsoft 符号服务器或跟随其 HTTPS 下载重定向";
            return false;
        }
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = GetLastError();
            finishDownload();
            reason = L"无法创建 PDB 临时缓存文件";
            return false;
        }
        std::array<std::byte, 64 * 1024> buffer{};
        std::uint64_t total{};
        bool success = true;
        while (!stopRequested.load())
        {
            DWORD received{};
            if (!InternetReadFile(request, buffer.data(), static_cast<DWORD>(buffer.size()), &received))
            {
                error = GetLastError();
                reason = L"读取 Microsoft 符号服务器下载数据失败";
                success = false;
                break;
            }
            if (received == 0) break;
            {
                std::scoped_lock progressLock(progressMutex);
                lastProgress = std::chrono::steady_clock::now();
            }
            total += received;
            if (total > kMaximumPdbBytes)
            {
                error = ERROR_FILE_TOO_LARGE;
                reason = L"PDB 下载大小超过 8 GB 安全上限";
                success = false;
                break;
            }
            output.write(reinterpret_cast<const char*>(buffer.data()), received);
            if (!output)
            {
                error = ERROR_WRITE_FAULT;
                reason = L"写入 PDB 临时缓存文件失败";
                success = false;
                break;
            }
        }
        const DownloadTimeoutReason timeout = timeoutReason.load();
        if (timeout != DownloadTimeoutReason::None)
        {
            error = ERROR_TIMEOUT;
            reason = DownloadTimeoutText(timeout);
            success = false;
        }
        if (stopRequested.load())
        {
            error = ERROR_CANCELLED;
            reason = L"符号准备已取消";
            success = false;
        }
        output.close();
        finishDownload();
        if (!success) return false;
        if (total == 0)
        {
            error = ERROR_INVALID_DATA;
            reason = L"符号服务器返回了空 PDB 文件";
            return false;
        }
        return true;
#endif
    }

    [[nodiscard]] std::wstring ReadManifestIdentity(const std::filesystem::path& manifestPath, const std::wstring& moduleName)
    {
        wchar_t buffer[256]{};
        GetPrivateProfileStringW(moduleName.c_str(), L"PdbIdentity", L"", buffer, static_cast<DWORD>(std::size(buffer)), manifestPath.c_str());
        return buffer;
    }

    [[nodiscard]] std::wstring ReadManifestModuleVersion(const std::filesystem::path& manifestPath, const std::wstring& moduleName)
    {
        wchar_t buffer[128]{};
        GetPrivateProfileStringW(moduleName.c_str(), L"ModuleVersion", L"", buffer,
            static_cast<DWORD>(std::size(buffer)), manifestPath.c_str());
        return buffer;
    }

    void WriteManifest(const std::filesystem::path& manifestPath, const ModuleSymbolIdentity& module, const std::filesystem::path& pdbPath)
    {
        const std::wstring key = GuidAndAgeKey(module.guid, module.age);
        WritePrivateProfileStringW(module.moduleName.c_str(), L"ModulePath", module.modulePath.c_str(), manifestPath.c_str());
        WritePrivateProfileStringW(module.moduleName.c_str(), L"ModuleVersion", module.moduleVersion.c_str(), manifestPath.c_str());
        WritePrivateProfileStringW(module.moduleName.c_str(), L"PdbName", module.pdbFileName.c_str(), manifestPath.c_str());
        WritePrivateProfileStringW(module.moduleName.c_str(), L"PdbIdentity", key.c_str(), manifestPath.c_str());
        WritePrivateProfileStringW(module.moduleName.c_str(), L"CacheFile", pdbPath.c_str(), manifestPath.c_str());
        WritePrivateProfileStringW(module.moduleName.c_str(), L"Verification", L"CodeView GUID 已验证；Age 用于缓存键", manifestPath.c_str());
    }

    void RemoveAbandonedTemporaryFiles(const std::filesystem::path& pdbPath, unrealdbg_native::LogSink& log)
    {
        const std::wstring prefix = pdbPath.filename().wstring() + L".downloading.";
        std::error_code error;
        for (const auto& item : std::filesystem::directory_iterator(pdbPath.parent_path(), error))
        {
            if (error) return;
            if (!item.is_regular_file(error) || error) { error.clear(); continue; }
            const std::wstring name = item.path().filename().wstring();
            if (name.rfind(prefix, 0) != 0) continue;
            const std::wstring pidText = name.substr(prefix.size());
            wchar_t* end{};
            const unsigned long parsed = wcstoul(pidText.c_str(), &end, 10);
            if (end == pidText.c_str() || *end != L'\0' || parsed == 0 || parsed > MAXDWORD) continue;
            HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parsed));
            if (process)
            {
                CloseHandle(process);
                continue;
            }
            if (GetLastError() != ERROR_INVALID_PARAMETER) continue; // 无权限时不能推断进程已经退出。
            std::filesystem::remove(item.path(), error);
            if (!error) log.Info(L"已清理上次异常退出留下的未完成符号下载文件：" + name);
            error.clear();
        }
    }

    [[nodiscard]] std::wstring SymbolFailureDetails(const std::wstring& reason, const DWORD error)
    {
        std::wostringstream output;
        output << L"符号表准备失败；为避免把不匹配的旧符号用于当前内核，VT 驱动尚未加载。\r\n"
            << L"原因：" << reason << L"\r\n"
            << L"错误码：" << error << L" (0x" << std::hex << std::uppercase << error << std::dec << L")\r\n"
            << L"系统原文：" << LastErrorText(error) << L"\r\n"
            << L"解决方案：确认网络可访问 https://msdl.microsoft.com/download/symbols/；检查磁盘剩余空间和 Symbols\\System 目录写入权限；若 Windows 刚完成更新，请保持网络连接后重新启动。";
        return output.str();
    }
}

namespace unrealdbg_native
{
    SymbolPreparationResult PrepareSystemSymbolCache(const std::wstring& applicationDirectory, LogSink& log, const std::atomic_bool& stopRequested)
    {
        SymbolPreparationResult result;
        const std::array<std::wstring, 3> moduleNames = { L"ntoskrnl.exe", L"win32kbase.sys", L"win32kfull.sys" };
        wchar_t systemDirectory[MAX_PATH]{};
        const UINT directoryLength = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        if (directoryLength == 0 || directoryLength >= std::size(systemDirectory))
        {
            const DWORD error = GetLastError();
            result.failureDetails = SymbolFailureDetails(L"无法确定 Windows System32 目录", error);
            log.Error(L"符号表预准备失败：无法获取 Windows System32 目录", error);
            return result;
        }
        const std::filesystem::path cacheRoot = std::filesystem::path(applicationDirectory) / L"Symbols" / L"System";
        std::error_code filesystemError;
        std::filesystem::create_directories(cacheRoot, filesystemError);
        if (filesystemError)
        {
            const DWORD error = static_cast<DWORD>(filesystemError.value());
            result.failureDetails = SymbolFailureDetails(L"无法创建符号缓存目录：" + cacheRoot.wstring(), error);
            log.Error(L"符号表预准备失败：无法创建 Symbols\\System 缓存目录", error);
            return result;
        }
        const std::filesystem::path manifestPath = cacheRoot / kCacheManifestName;
        log.Info(L"启动符号表预准备：仅校验/下载 PDB 缓存，不加载 VT 驱动");
        for (size_t index = 0; index < moduleNames.size(); ++index)
        {
            if (stopRequested.load())
            {
                result.failureDetails = SymbolFailureDetails(L"符号准备已取消", ERROR_CANCELLED);
                return result;
            }
            const std::filesystem::path modulePath = std::filesystem::path(systemDirectory) / moduleNames[index];
            log.Info(L"符号表预准备阶段 " + std::to_wstring(index + 1) + L"/" + std::to_wstring(moduleNames.size()) +
                L"：正在读取 " + moduleNames[index] + L" 的版本和 PDB 标识");
            ModuleSymbolIdentity module;
            std::wstring reason;
            if (!ReadModuleIdentity(modulePath, module, reason))
            {
                const DWORD error = GetLastError() == ERROR_SUCCESS ? ERROR_BAD_EXE_FORMAT : GetLastError();
                result.failureDetails = SymbolFailureDetails(moduleNames[index] + L"：" + reason, error);
                log.Error(L"符号表预准备失败：" + moduleNames[index] + L"；" + reason, error);
                return result;
            }
            module.moduleVersion = GetFileVersionString(module.modulePath, log);
            if (module.moduleVersion.empty()) module.moduleVersion = L"版本信息不可用";
            const std::wstring currentIdentity = GuidAndAgeKey(module.guid, module.age);
            const std::filesystem::path pdbPath = cacheRoot / module.pdbFileName / currentIdentity / module.pdbFileName;
            const std::wstring previousIdentity = ReadManifestIdentity(manifestPath, module.moduleName);
            const std::wstring previousModuleVersion = ReadManifestModuleVersion(manifestPath, module.moduleName);
            const bool identityChanged = !previousIdentity.empty() &&
                _wcsicmp(previousIdentity.c_str(), currentIdentity.c_str()) != 0;
            const bool moduleVersionChanged = !previousModuleVersion.empty() &&
                _wcsicmp(previousModuleVersion.c_str(), module.moduleVersion.c_str()) != 0;
            if (identityChanged || moduleVersionChanged)
            {
                std::wstring detail;
                if (identityChanged)
                {
                    detail = L"PDB 标识 " + previousIdentity + L" → " + currentIdentity;
                }
                if (moduleVersionChanged)
                {
                    if (!detail.empty()) detail += L"；";
                    detail += L"系统模块版本 " + previousModuleVersion + L" → " + module.moduleVersion;
                }
                log.Info(L"缓存失效：" + module.moduleName + L" 的" + detail + L"，将下载并验证对应的新缓存");
            }
            std::wstring verificationReason;
            if (!identityChanged && !moduleVersionChanged &&
                std::filesystem::is_regular_file(pdbPath, filesystemError) && !filesystemError &&
                VerifyPdb(pdbPath, module, verificationReason))
            {
                ++result.cacheHits;
                WriteManifest(manifestPath, module, pdbPath);
                if (!verificationReason.empty()) log.Debug(L"符号缓存验证说明：" + module.moduleName + L"；" + verificationReason);
                log.Info(L"符号缓存命中：" + module.moduleName + L"（CodeView 缓存键匹配且 PDB GUID 已验证，复用本地缓存）");
                continue;
            }
            if (std::filesystem::exists(pdbPath, filesystemError) && !filesystemError)
            {
                if (verificationReason.empty())
                {
                    verificationReason = moduleVersionChanged
                        ? L"系统模块版本已变化，必须重新验证"
                        : L"缓存清单与当前模块标识不一致";
                }
                log.Info(L"缓存失效：" + module.moduleName + L" 的现有 PDB 不再满足当前模块校验；将重新下载。原因：" + verificationReason);
            }
            else if (previousIdentity.empty())
            {
                log.Info(L"未发现 " + module.moduleName + L" 的本地符号缓存；将从 Microsoft 符号服务器下载");
            }
            std::filesystem::create_directories(pdbPath.parent_path(), filesystemError);
            if (filesystemError)
            {
                const DWORD error = static_cast<DWORD>(filesystemError.value());
                result.failureDetails = SymbolFailureDetails(L"无法创建 PDB 缓存子目录：" + pdbPath.parent_path().wstring(), error);
                log.Error(L"符号表预准备失败：无法创建 " + module.moduleName + L" 的缓存目录", error);
                return result;
            }
            const std::filesystem::path partialPath = pdbPath.wstring() + L".partial";
            RemoveAbandonedTemporaryFiles(pdbPath, log);
            if (std::filesystem::is_regular_file(partialPath, filesystemError) && !filesystemError)
            {
                const auto partialBytes = std::filesystem::file_size(partialPath, filesystemError);
                if (!filesystemError && VerifyPdb(partialPath, module, verificationReason))
                {
                    if (!MoveFileExW(partialPath.c_str(), pdbPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    {
                        const DWORD error = GetLastError();
                        result.failureDetails = SymbolFailureDetails(module.moduleName + L"：已完成的断点续传文件无法原子写入缓存", error);
                        log.Error(L"符号表预准备失败：无法提交已校验的 " + module.moduleName + L" 断点续传文件", error);
                        return result;
                    }
                    ++result.downloads;
                    WriteManifest(manifestPath, module, pdbPath);
                    if (!verificationReason.empty()) log.Debug(L"符号缓存验证说明：" + module.moduleName + L"；" + verificationReason);
                    log.Info(L"已校验并提交上次下载完成的 " + module.moduleName + L" PDB 缓存");
                    continue;
                }
                if (!filesystemError && partialBytes > 0)
                    log.Info(L"检测到 " + module.moduleName + L" 的未完成符号下载：已保留 " +
                        std::to_wstring(partialBytes / 1024) + L" KB；本次将从断点继续，不会重新下载已完成部分");
            }
            filesystemError.clear();
            log.Info(L"正在断点续传 " + module.moduleName + L" 的 PDB；下载完成后将校验 GUID、记录 Age 差异，再原子写入缓存");
            DWORD downloadError = ERROR_SUCCESS;
            if (!DownloadPdb(module, partialPath, stopRequested, downloadError, reason))
            {
                std::error_code partialError;
                const auto partialBytes = std::filesystem::file_size(partialPath, partialError);
                if (!partialError && partialBytes > 0)
                    reason += L"；已安全保留 " + std::to_wstring(partialBytes / 1024) +
                        L" KB 断点文件，下次启动或点击“重试并进入 VT”将从该位置继续下载";
                result.failureDetails = SymbolFailureDetails(module.moduleName + L"：" + reason, downloadError);
                log.Error(L"符号表预准备失败：" + module.moduleName + L"；" + reason, downloadError);
                return result;
            }
            if (!VerifyPdb(partialPath, module, verificationReason))
            {
                std::filesystem::remove(partialPath, filesystemError);
                result.failureDetails = SymbolFailureDetails(module.moduleName + L"：下载的 PDB 校验失败；" + verificationReason, ERROR_INVALID_DATA);
                log.Error(L"符号表预准备失败：下载的 " + module.moduleName + L" PDB 与当前模块不匹配；" + verificationReason, ERROR_INVALID_DATA);
                return result;
            }
            if (!verificationReason.empty()) log.Debug(L"符号下载验证说明：" + module.moduleName + L"；" + verificationReason);
            if (!MoveFileExW(partialPath.c_str(), pdbPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                const DWORD error = GetLastError();
                result.failureDetails = SymbolFailureDetails(module.moduleName + L"：无法原子写入已验证的 PDB 缓存", error);
                log.Error(L"符号表预准备失败：无法原子写入 " + module.moduleName + L" 的 PDB 缓存", error);
                return result;
            }
            ++result.downloads;
            WriteManifest(manifestPath, module, pdbPath);
            log.Info(L"符号下载并校验完成：" + module.moduleName + L"（已原子写入本地缓存）");
        }
        result.success = true;
        result.summary = L"符号缓存可用（3/3；缓存命中 " + std::to_wstring(result.cacheHits) + L"，本次下载 " + std::to_wstring(result.downloads) + L"）";
        log.Info(L"符号表预准备完成：" + result.summary + L"；进入 VT 模式时仍会执行原有驱动符号表握手");
        return result;
    }

    bool RunSymbolCacheSelfTests(std::wstring& failure)
    {
        GUID guid{};
        guid.Data1 = 0x01234567;
        guid.Data2 = 0x89AB;
        guid.Data3 = 0xCDEF;
        guid.Data4[0] = 0x01; guid.Data4[1] = 0x23; guid.Data4[2] = 0x45; guid.Data4[3] = 0x67;
        guid.Data4[4] = 0x89; guid.Data4[5] = 0xAB; guid.Data4[6] = 0xCD; guid.Data4[7] = 0xEF;
        if (GuidAndAgeKey(guid, 0x1A) != L"0123456789ABCDEF0123456789ABCDEF1A")
        {
            failure = L"符号缓存 GUID/Age 键格式测试失败";
            return false;
        }
        if (!IsSafePdbFileName(L"ntkrnlmp.pdb") || IsSafePdbFileName(L"..\\ntkrnlmp.pdb") ||
            IsSafePdbFileName(L"ntkrnlmp.exe") || !EscapeUrlPathComponent(L"ntkrnlmp.pdb").size())
        {
            failure = L"符号缓存 PDB 文件名安全检查测试失败";
            return false;
        }
        failure.clear();
        return true;
    }
}

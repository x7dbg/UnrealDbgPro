#include <iostream>
#include <fstream>
#include <iomanip>
#include <Windows.h>
#include <wincrypt.h>
#include <sstream>
#include <vector>
#include "MD5.h"

namespace
{
    void ReportMd5Failure(const char* operation)
    {
        DWORD error = GetLastError();
        if (error == ERROR_SUCCESS)
        {
            error = ERROR_GEN_FAILURE;
        }

        std::cerr << "MD5 操作失败：" << operation
                  << "；错误码=" << error << " (0x" << std::hex << std::uppercase << error << std::dec
                  << ")；原因：Windows 加密服务拒绝请求、权限不足或输入数据无效；"
                     "解决方案：检查 Cryptographic Services 服务、输入文件/数据及运行权限后重试。"
                  << std::endl;
    }
}

//计算文件md5
std::string calculateMD5(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);

    if (!file) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        ReportMd5Failure("打开输入文件");
        return "";
    }

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        ReportMd5Failure("CryptAcquireContext 获取加密上下文");
        return "";
    }

    HCRYPTHASH hHash = 0;
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        ReportMd5Failure("CryptCreateHash 创建哈希对象");
        CryptReleaseContext(hProv, 0);
        return "";
    }

    char buffer[4096];
    while (file) {
        file.read(buffer, sizeof(buffer));
        DWORD bytesRead = static_cast<DWORD>(file.gcount());
        if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(buffer), bytesRead, 0)) {
            ReportMd5Failure("CryptHashData 写入哈希数据");
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return "";
        }
    }

    //获取密码哈希的哈希值长度
    DWORD hashSize = 0;
    DWORD hashSizeLen = sizeof(DWORD);
    if (!CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashSize), &hashSizeLen, 0)) {
        ReportMd5Failure("CryptGetHashParam 获取哈希长度");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    //获取密码哈希的哈希值
    std::vector<BYTE> hashBuffer(hashSize);
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hashBuffer.data(), &hashSize, 0)) {
        ReportMd5Failure("CryptGetHashParam 获取哈希值");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::stringstream md5Stream;
    //转十六进制 不足两个字符补0填充 例如：00 0E
    md5Stream << std::hex << std::setfill('0');
    for (BYTE byte : hashBuffer) {
        md5Stream << std::setw(2) << static_cast<int>(byte);
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return md5Stream.str();
}

//字节流哈希摘要
std::string calculateMD5(const std::vector<unsigned char>& data)
{
    HCRYPTPROV hProv = NULL;
    HCRYPTHASH hHash = NULL;

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        ReportMd5Failure("CryptAcquireContext 获取加密上下文");
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
    {
        ReportMd5Failure("CryptCreateHash 创建哈希对象");
        CryptReleaseContext(hProv, 0);
        return "";
    }

    if (!CryptHashData(hHash, data.data(), data.size(), 0))
    {
        ReportMd5Failure("CryptHashData 写入哈希数据");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    DWORD cbHashSize = 0;
    DWORD dwCount = sizeof(DWORD);
    if (!CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&cbHashSize), &dwCount, 0))
    {
        ReportMd5Failure("CryptGetHashParam 获取哈希长度");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::vector<unsigned char> hashData(cbHashSize);
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hashData.data(), &cbHashSize, 0))
    {
        ReportMd5Failure("CryptGetHashParam 获取哈希值");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::string md5Hash;
    for (const auto& byte : hashData)
    {
        char hex[3];
        sprintf_s(hex, "%02X", byte);
        md5Hash += hex;
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return md5Hash;
}

//对字符串进行哈希摘要
std::string calculateMD5(const TCHAR* inputParam)
{
    HCRYPTPROV hProv = NULL;
    HCRYPTHASH hHash = NULL;

    if (inputParam == NULL)
    {
        return "";
    }

    std::wstring input(inputParam);

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        ReportMd5Failure("CryptAcquireContext 获取加密上下文");
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        ReportMd5Failure("CryptCreateHash 创建哈希对象");
        CryptReleaseContext(hProv, 0);
        return "";
    }

    const BYTE* inputData = reinterpret_cast<const BYTE*>(input.c_str());
    DWORD inputSize = static_cast<DWORD>(input.length() * sizeof(wchar_t));
    if (!CryptHashData(hHash, inputData, inputSize, 0)) {
        ReportMd5Failure("CryptHashData 写入哈希数据");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    DWORD dwHashSize = 0;
    DWORD dwHashLen = sizeof(DWORD);
    if (!CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&dwHashSize), &dwHashLen, 0)) {
        ReportMd5Failure("CryptGetHashParam 获取哈希长度");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::vector<BYTE> hash(dwHashSize);
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash.data(), &dwHashSize, 0)) {
        ReportMd5Failure("CryptGetHashParam 获取哈希值");
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::stringstream md5Stream;
    md5Stream << std::uppercase << std::hex << std::setfill('0');
    for (BYTE byte : hash) {
        md5Stream << std::setw(2) << static_cast<int>(byte);
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return md5Stream.str();
}

#include <iostream>
#include <Windows.h>
#include <string>
#include <codecvt>
#include <random>
#include <tuple>
#include <TlHelp32.h>
#include <vector>
#include "StringHandler.h"


namespace LingoLab
{
	//string转wstring
	std::wstring stringToWideString(const std::string& narrowStr)
	{
		// 获取宽字符字符串的长度（包括空终止符）
		int wideStrLength = MultiByteToWideChar(CP_UTF8, 0, narrowStr.c_str(), -1, nullptr, 0);

		// 分配内存来存储宽字符字符串
		wchar_t* wideStr = new wchar_t[wideStrLength];

		// 将窄字符转换为宽字符
		MultiByteToWideChar(CP_UTF8, 0, narrowStr.c_str(), -1, wideStr, wideStrLength);

		// 创建 std::wstring 对象
		std::wstring result(wideStr);

		// 释放内存
		delete[] wideStr;

		return result;
	}

	//wstring转string
	std::string wideStringToString(const std::wstring& wideStr)
	{
		int bufferSize = WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string str(bufferSize - 1, 0);
		WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), -1, &str[0], bufferSize - 1, nullptr, nullptr);
		return str;
	}

	//wchar_t*转string
	std::string wcharToString(const wchar_t* str)
	{
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		return converter.to_bytes(str);
	}

	//wchar_t*转wstring
	std::wstring wcharToWideString(const wchar_t* wcharStr)
	{
		// 使用构造函数将 wchar_t* 转换为 std::wstring
		std::wstring wideStr(wcharStr);

		return wideStr;
	}

	//char*转wchar_t*
	std::wstring ConvertCharToWchar(const char* charStr)
	{
		const int charStrLength = strlen(charStr) + 1; // char 字符串的长度（包括 null 终止符）

		// 计算 wchar_t 字符串所需的缓冲区大小
		const int wcharStrSize = MultiByteToWideChar(CP_UTF8, 0, charStr, charStrLength, nullptr, 0);

		// 分配 wchar_t 缓冲区
		wchar_t* wcharStr = new wchar_t[wcharStrSize];

		// 执行转换
		MultiByteToWideChar(CP_UTF8, 0, charStr, charStrLength, wcharStr, wcharStrSize);

		// 将 wchar_t 字符串封装到 std::wstring 类型
		std::wstring result(wcharStr);

		// 释放内存
		delete[] wcharStr;

		return result;
	}

	//gbk转utf8
	std::string GbkToUTF8(const std::string& gbkString)
	{
		int bufferSize = MultiByteToWideChar(CP_ACP, 0, gbkString.c_str(), -1, nullptr, 0);
		std::wstring wideString(bufferSize - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, gbkString.c_str(), -1, &wideString[0], bufferSize - 1);

		bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string utf8String(bufferSize - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, &utf8String[0], bufferSize - 1, nullptr, nullptr);

		return utf8String;
	}

	//gbk转utf8
	//std::string GbkToUTF8(const std::string& gbkString)
	//{
	//	int bufferSize = MultiByteToWideChar(CP_ACP, 0, gbkString.c_str(), -1, nullptr, 0);
	//	std::wstring wideString(bufferSize, L'\0');
	//	MultiByteToWideChar(CP_ACP, 0, gbkString.c_str(), -1, &wideString[0], bufferSize);

	//	bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, nullptr, 0, nullptr, nullptr);
	//	std::string utf8String(bufferSize, '\0');
	//	WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, &utf8String[0], bufferSize, nullptr, nullptr);

	//	return utf8String;
	//}

	// 将 utf8 编码的字符串转换为 GBK 编码
	std::string utf8ToGbk(const std::string& utf8String)
	{
		int bufferSize = MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), -1, nullptr, 0);
		if (bufferSize == 0)
		{
			// 转换失败，可以根据实际情况进行错误处理
			return "";
		}

		std::wstring wideString(bufferSize, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), -1, &wideString[0], bufferSize);

		bufferSize = WideCharToMultiByte(CP_ACP, 0, wideString.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (bufferSize == 0)
		{
			// 转换失败，可以根据实际情况进行错误处理
			return "";
		}

		std::string gbkString(bufferSize, '\0');
		WideCharToMultiByte(CP_ACP, 0, wideString.c_str(), -1, &gbkString[0], bufferSize, nullptr, nullptr);

		return gbkString;
	}

	// 将 utf8 编码的字符串转换为 Unicode 编码
	std::wstring utf8ToUnicode(const std::string& utf8String)
	{
		int bufferSize = MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), -1, nullptr, 0);
		std::wstring unicodeString(bufferSize, 0);
		MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), -1, &unicodeString[0], bufferSize);
		return unicodeString;
	}

	//本地代码页转std::wstring
	std::wstring ConvertLocalCodePageToWideString(const std::string& str)
	{
		int wideStrLen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
		if (wideStrLen == 0)
		{
			// 转换失败，可以根据实际情况处理错误
			return L"";
		}

		std::wstring wideStr(wideStrLen, L'\0');
		if (MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wideStr[0], wideStrLen) == 0)
		{
			// 转换失败，可以根据实际情况处理错误
			return L"";
		}

		// 去掉末尾的空字符
		wideStr.resize(wideStrLen - 1);

		return wideStr;
	}

	//本地代码页转std::string
	std::string LocalCodePageToUtf8(const std::string& localString)
	{
		int wideCharLength = MultiByteToWideChar(CP_ACP, 0, localString.c_str(), -1, nullptr, 0);
		if (wideCharLength == 0) {
			// 转换失败
			return "";
		}

		std::wstring wideString(wideCharLength, L'\0');
		if (MultiByteToWideChar(CP_ACP, 0, localString.c_str(), -1, &wideString[0], wideCharLength) == 0) {
			// 转换失败
			return "";
		}

		int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (utf8Length == 0) {
			// 转换失败
			return "";
		}

		std::string utf8String(utf8Length, '\0');
		if (WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, &utf8String[0], utf8Length, nullptr, nullptr) == 0) {
			// 转换失败
			return "";
		}

		return utf8String;
	}

	//Unicode转Utf8
	std::string UnicodeToUtf8(const std::wstring& unicodeString)
	{
		int utf8Length = WideCharToMultiByte(CP_UTF8, 0, unicodeString.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (utf8Length == 0) {
			// 转换失败
			return "";
		}

		std::string utf8String(utf8Length, '\0');
		if (WideCharToMultiByte(CP_UTF8, 0, unicodeString.c_str(), -1, &utf8String[0], utf8Length, nullptr, nullptr) == 0) {
			// 转换失败
			return "";
		}

		return utf8String;
	}

	//生成16位随机字符串
	std::string generateRandomString()
	{
		const std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
		const int length = 16;

		std::random_device rd;
		std::mt19937 generator(rd());
		std::uniform_int_distribution<int> distribution(0, characters.length() - 1);

		std::string randomString;

		for (int i = 0; i < length; ++i) {
			randomString += characters[distribution(generator)];
		}

		return randomString;
	}

	//字符串截取
	std::string truncateString(const std::string& input, int length)
	{
		if (length >= input.length())
		{
			return input;
		}
		else
		{
			return input.substr(0, length);
		}
	}

	//截取字符串 和剩余字符串
	std::tuple<std::string, std::string> truncateString2(const std::string& input, int length)
	{
		if (length >= input.length())
		{
			return std::make_tuple(input, "");
		}
		else
		{
			return std::make_tuple(input.substr(0, length), input.substr(length));
		}
	}

	//将string转小写
	std::string ToLowerWindows(const std::string& str)
	{
		std::string lowerStr(str);
		CharLowerBuffA(&lowerStr[0], static_cast<DWORD>(lowerStr.size()));

		return lowerStr;
	}

	//将wstring转小写
	std::wstring ToLowerWindows(const std::wstring& str)
	{
		std::wstring lowerStr(str);
		CharLowerBuffW(&lowerStr[0], static_cast<DWORD>(lowerStr.size()));

		return lowerStr;
	}

	//枚举进程
	std::vector<ProcessInfo> EnumerateProcesses()
	{
		std::vector<ProcessInfo> processes;

		// 获取系统中所有进程的快照
		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE)
		{
			// 返回空容器
			return processes;
		}

		PROCESSENTRY32W processEntry = { sizeof(PROCESSENTRY32W) };

		// 枚举进程快照中的进程信息
		if (Process32First(hSnapshot, &processEntry))
		{
			do
			{
				ProcessInfo process;
				process.processId = processEntry.th32ProcessID;
				process.processName = processEntry.szExeFile;

				processes.push_back(process);
			} while (Process32Next(hSnapshot, &processEntry));
		}

		// 关闭进程快照句柄
		CloseHandle(hSnapshot);

		return processes;
	}

	//检查目标进程是否正在运行
	BOOL IsProcessRunning(const std::wstring& processName)
	{
		BOOL boRet = FALSE;
		PROCESSENTRY32W entry;
		entry.dwSize = sizeof(PROCESSENTRY32W);

		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot != INVALID_HANDLE_VALUE)
		{
			if (Process32FirstW(hSnapshot, &entry))
			{
				do
				{
					std::wstring currentProcessName = ToLowerWindows(entry.szExeFile);
					if (currentProcessName.find(ToLowerWindows(processName)) != std::wstring::npos)  //查找子串
					{
						boRet = TRUE;
						break;
					}
				} while (Process32NextW(hSnapshot, &entry));
			}
			CloseHandle(hSnapshot);
		}
		return boRet;
	}

	//终止进程
	bool TerminateWindowsProcess(DWORD processId)
	{
		HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
		if (hProcess == NULL)
		{
			// 处理打开进程失败的情况
			return false;
		}

		// 终止进程
		bool result = TerminateProcess(hProcess, 0);

		// 关闭进程句柄
		CloseHandle(hProcess);

		return result;
	}
}
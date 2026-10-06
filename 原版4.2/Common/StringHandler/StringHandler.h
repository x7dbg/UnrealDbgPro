#pragma once

#ifndef _STRING_HANDLER_H
#define _STRING_HANDLER_H


namespace LingoLab
{
	struct ProcessInfo
	{
		DWORD processId;
		std::wstring processName;
	};

	//string转wstring
	std::wstring stringToWideString(const std::string& narrowStr);
	//wstring转string
	std::string wideStringToString(const std::wstring& wideStr);
	//wchar_t*转string
	std::string wcharToString(const wchar_t* str);
	//wchar_t*转wstring
	std::wstring wcharToWideString(const wchar_t* wcharStr);
	//char*תwchar_t*
	std::wstring ConvertCharToWchar(const char* charStr);
	//gbk转utf8
	std::string GbkToUTF8(const std::string& gbkString);
	//将utf8编码的字符串转换为GBK编码
	std::string utf8ToGbk(const std::string& utf8String);
	//将utf8编码的字符串转换为Unicode编码
	std::wstring utf8ToUnicode(const std::string& utf8String);
	//本地代码页转std::wstring
	std::wstring ConvertLocalCodePageToWideString(const std::string& str);
	//本地代码页转std::string
	std::string LocalCodePageToUtf8(const std::string& localString);
	//Unicode转utf8
	std::string UnicodeToUtf8(const std::wstring& unicodeString);
	//生成16位随机字符串
	std::string generateRandomString();
	//字符串截取
	std::string truncateString(const std::string& input, int length);
	//截取字符串 和剩余字符串
	std::tuple<std::string, std::string> truncateString2(const std::string& input, int length);
	//将string转小写
	std::string ToLowerWindows(const std::string& str);
	//将wstring转小写
	std::wstring ToLowerWindows(const std::wstring& str);
	//枚举进程
	std::vector<ProcessInfo> EnumerateProcesses();
	//检查目标进程是否正在运行
	BOOL IsProcessRunning(const std::wstring& processName);
	//终止进程
	bool TerminateWindowsProcess(DWORD processId);
}


#endif // !_STRING_HANDLER_H

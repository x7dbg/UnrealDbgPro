#include "../dllmain.h"
#include "Log.h"

void _outDebug(const TCHAR* sText)
{
	if (sText == nullptr)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("Hook 调试消息为空", ERROR_INVALID_PARAMETER);
		return;
	}
#ifdef _UNICODE
	OutputDebugStringW(sText);
	OutputDebugStringW(L"\n");
	logger.LogW(L"[DEBUG] %ls", sText);
#else
	OutputDebugStringA(sText);
	OutputDebugStringA("\n");
	logger.Log("[DEBUG] %s", sText);
#endif
}

int outDebug(const TCHAR* _Format, ...)
{
	if (_Format == nullptr)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("Hook outDebug 收到空格式字符串", ERROR_INVALID_PARAMETER);
		return -1;
	}
	int iRet;
	va_list list;
	TCHAR SzBuf[1024] = { 0 };
	va_start(list, _Format);
	#ifdef _UNICODE
	iRet = _vsnwprintf_s(SzBuf, _countof(SzBuf), _TRUNCATE, _Format, list);
	#else
	iRet = _vsnprintf_s(SzBuf, _countof(SzBuf), _TRUNCATE, _Format, list);
	#endif
	_outDebug(SzBuf);
	va_end(list);
	if (iRet < 0)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		logger.LogError("Hook 调试消息格式化失败或消息被截断", ERROR_INSUFFICIENT_BUFFER);
	}
	return iRet;
}

void ReportSeriousError(LPCSTR lpText)
{
	SetLastError(ERROR_UNHANDLED_EXCEPTION);
	logger.LogError("Hook 严重错误", ERROR_UNHANDLED_EXCEPTION);
	logger.Log("[DEBUG] Hook 错误原文：%s", lpText ? lpText : "<空>");
	if (lpText == nullptr)
	{
		MessageBoxW(NULL, L"Hook 返回了空错误信息。请查看日志并重新启动。", L"严重错误", MB_ICONERROR | MB_SYSTEMMODAL);
		return;
	}
	// Hook 旧接口仍接收窄字符串：优先按 UTF-8 转换，失败时按系统代码页
	// 转换，最后统一使用 MessageBoxW，避免中文弹窗出现乱码。
	const int utf8Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, lpText, -1, nullptr, 0);
	UINT codePage = CP_UTF8;
	int required = utf8Length;
	if (required == 0)
	{
		codePage = CP_ACP;
		required = MultiByteToWideChar(codePage, 0, lpText, -1, nullptr, 0);
	}
	std::wstring message;
	if (required > 0)
	{
		message.resize(static_cast<size_t>(required));
		MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
			lpText, -1, &message[0], required);
		if (!message.empty() && message.back() == L'\0')
		{
			message.pop_back();
		}
	}
	if (message.empty())
	{
		message = L"Hook 返回了无法解码的错误信息。请查看日志。";
	}
	MessageBoxW(NULL, message.c_str(), L"严重错误", MB_ICONERROR | MB_SYSTEMMODAL);
}

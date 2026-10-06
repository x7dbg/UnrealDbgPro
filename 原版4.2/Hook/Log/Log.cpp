#include "../dllmain.h"
#include "Log.h"

void _outDebug(const TCHAR* sText)
{
	MessageBox(NULL, sText, _T("Hook"), MB_ICONINFORMATION | MB_SYSTEMMODAL);
}

int outDebug(const TCHAR* _Format, ...)
{
	int iRet;
	va_list list;
	TCHAR SzBuf[1024] = { 0 };
	va_start(list, _Format);
	iRet = vswprintf(SzBuf, sizeof(SzBuf), _Format, list);
	_outDebug(SzBuf);
	va_end(list);
	return iRet;
}

void ReportSeriousError(LPCSTR lpText)
{
	MessageBoxA(NULL, lpText, "—œ÷ÿ¥ÌŒÛ:", MB_ICONERROR | MB_SYSTEMMODAL);
}
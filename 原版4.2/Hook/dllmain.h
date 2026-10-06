#pragma once

#ifndef _DLLMAIN_H
#define _DLLMAIN_H

#include "ntdll.h"
#include <DbgHelp.h>
#include <psapi.h>
#include <shlwapi.h>
#include <tchar.h>
#include <assert.h>
#include <string>
#include <vector>
#include <process.h>
#include <sstream>
#include <mutex>
#include <fstream>
#include "../Common/Common.h"
#include "../Common/FileSystem/FileSystem.h"
#include "../Common/Detours/Hook.h"
#include "../Common/Logger/Logger.h"
#include "../Common/Shared/IOCTLs.h"
#include "../Common/Shared//SharedStruct.h"
#include "../Common/Encrypt/Blowfish/Blowfish.h"
#include "../Common/include/List/vectorExt.h"

void SetupHook();
void UnHook();

//初始化设备
BOOL InitializeDevice();

PVOID GetRoutinePointer(TCHAR* modName, LPCSTR lpProcName);

void InitFunction();

void InitGlobalVariables();

HMODULE GetProcessModuleHandle(_In_ HANDLE hProcess, _In_ TCHAR* modName, _Out_ TCHAR* outMod);

//连接驱动
HANDLE CreateDeviceHandle();

extern Logger logger;


#endif // !_DLLMAIN_H


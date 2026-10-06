#pragma once

#ifndef _DRIVER_H
#define _DRIVER_H

#include <ntifs.h>
#include <intrin.h>
#include <Shared/SharedStruct.h>
#include "../../Common/Ring0/ia32-doc/out/ia32.hpp"
#include "../../Common/Ring0/SymbolicAccess/Utils/Log.h"
#include "../../Common/Ring0/SymbolicAccess/ModuleExtender/ModuleExtenderFactory.h"
#include "../../Common/Ring0/String/StringHandler.h"

#define IOCTL_POOL_MANAGER_ALLOCATE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

#define SYMBOLIC  L"\\DosDevices\\VT_Device"

#ifdef _DEBUG_LOG

#define outDebug(format, ...) \
	DbgPrint(format,__VA_ARGS__)

#else

#define outDebug(format, ...)

#endif // _DEBUG_LOG


NTSTATUS Dispatcher_Create_Close(_In_ PDEVICE_OBJECT device_object, _In_ PIRP irp);
NTSTATUS Dispatcher_IOCTL_Function(_In_ PDEVICE_OBJECT device_object, _In_ PIRP irp);

bool InitOffset(PWINDOWS_STRUCT vmcallinfo);
PCLIENT_ID GuestCurrentThreadCid();

bool SetBreakpoint(PVT_BREAK_POINT vmcallinfo, unsigned __int64 Type);
bool RemoveBreakpoint(PVT_BREAK_POINT vmcallinfo);

void MyKeBugCheck(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4
);

#endif // !_DRIVER_H

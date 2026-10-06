unit Kernel32Api;

interface

uses Winapi.Windows;

const
  THREAD_SUSPEND_RESUME = $2;
  LIST_MODULES_32BIT = $1;
  LIST_MODULES_64BIT = $2;
  LIST_MODULES_ALL = $3;
  LIST_MODULES_DEFAULT = $0;

type
  TQueryFullProcessImageNameA = function(hProcess: THandle; dwFlags: DWORD; lpExeName: LPSTR; var lpdwSize: DWORD): BOOL; stdcall;
  TOpenThread = function(dwDesiredAccess: DWORD; bInheritHandle: BOOL; dwThreadId: DWORD): THandle; stdcall;
  TEnumProcessModulesEx = function(hProcess: THandle; lphModule: PHMODULE; cb: DWORD; lpcbNeeded: PDWORD; dwFilterFlag: DWORD): BOOL; stdcall;

var
  WindowsKernel: THandle;
  QueryFullProcessImageNameA: TQueryFullProcessImageNameA;
  OpenThread: TOpenThread;
  EnumProcessModulesEx: TEnumProcessModulesEx;

implementation

initialization
begin
  WindowsKernel := LoadLibrary('Kernel32.dll');
  QueryFullProcessImageNameA := GetProcAddress(WindowsKernel,'QueryFullProcessImageNameA');
  OpenThread := GetProcAddress(WindowsKernel,'OpenThread');
  EnumProcessModulesEx := GetProcAddress(WindowsKernel,'K32EnumProcessModulesEx');
end;

end.

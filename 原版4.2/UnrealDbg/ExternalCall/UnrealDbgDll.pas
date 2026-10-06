unit UnrealDbgDll;

interface

uses Winapi.Windows;

const
  PROTECT_FILE       = 0;
  PROTECT_WIN        = 1;

type
  TUnreal_Initialize = function(key: ULONG64): BOOL;
  TUnreal_StartProcess = function(szExe: string; sPath: string): BOOL;
  TUnreal_GetFileVersion = procedure(FileName: string; VerInfo: Pointer);
  TUnreal_TL_BlockGameResumeThread = function(dwPid: DWORD): BOOL;


var
  LoadSymbol: THandle;
  Unreal_Initialize: TUnreal_Initialize;
  Unreal_StartProcess: TUnreal_StartProcess;
  Unreal_GetFileVersion: TUnreal_GetFileVersion;
  Unreal_TL_BlockGameResumeThread: TUnreal_TL_BlockGameResumeThread;

implementation

uses Grobal;

initialization
begin
{$IFNDEF RELEASE}
  LoadSymbol := LoadLibrary('E:\Projects\VS\repos\UnrealDbg\x64\Release\UnrealDbgDll.dll');
{$ELSE}
  LoadSymbol := LoadLibrary('UnrealDbgDll.dll');
{$ENDIF}
  if LoadSymbol = 0 then begin
    MessageBox(0,'没有找到 UnrealDbgDll.dll','错误信息:',MB_ICONERROR);
    ExitProcess(0);
  end;
  Unreal_Initialize := GetProcAddress(LoadSymbol,'Initialize');
  Unreal_StartProcess := GetProcAddress(LoadSymbol,'StartProcess');
  Unreal_GetFileVersion := GetProcAddress(LoadSymbol,'GetFileVersion');
  Unreal_TL_BlockGameResumeThread := GetProcAddress(LoadSymbol,'TL_BlockGameResumeThread');
end;

end.

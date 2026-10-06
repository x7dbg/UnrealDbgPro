unit GlobalVar;

interface

uses Winapi.ActiveX, System.Win.ComObj, Vcl.Forms, System.SysUtils, Winapi.Windows;

var
  g_boLoginSuccess: Boolean;
  g_boStartService: Boolean;

  g_boEnabled_tl_confrontation_TL: Boolean;
  g_boHandlerGetTickCountCheck_TL: Boolean;
  g_boBlockResumeThread_TL: Boolean;

implementation

initialization
begin
  g_boEnabled_tl_confrontation_TL := False;
  g_boHandlerGetTickCountCheck_TL := False;
  g_boBlockResumeThread_TL := False;
end;

end.

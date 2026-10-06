program UnrealDbg;

{$R *.dres}

uses
  Vcl.Forms,
  Main in 'Forms\Main.pas' {Form1},
  Log in 'LogSystem\Log.pas',
  UnrealDbgDll in 'ExternalCall\UnrealDbgDll.pas',
  ExtTQueue in 'Common\ExtTQueue.pas',
  GList in 'Common\GList.pas',
  Grobal in 'Common\Grobal.pas',
  VMProtectSDK in 'Common\VMProtectSDK.pas',
  LockThread in 'Threads\LockThread.pas',
  Kernel32Api in 'KernelApi\Kernel32Api.pas',
  KernelBaseApi in 'KernelApi\KernelBaseApi.pas',
  HandlerTLDetection in 'Handler\HandlerTLDetection.pas',
  EventHandlerThread in 'Threads\EventHandlerThread.pas',
  GlobalVar in 'Globals\GlobalVar.pas',
  KeyVerification in 'Network\KeyVerification.pas',
  D_encryptionDll in 'ExternalCall\D_encryptionDll.pas';

{$R *.res}

begin
  Application.Initialize;
  Application.MainFormOnTaskbar := True;
  Application.CreateForm(TForm1, Form1);
  Application.Run;
end.

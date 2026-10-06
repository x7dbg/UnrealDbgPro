{
  注意: RAD Studio 11 编译64位程序时，如果将项目名命名为中文则无法进行调试。
}

unit Main;

interface

uses
  Winapi.Windows, Winapi.Messages, System.SysUtils, System.Variants, System.Classes, Vcl.Graphics,
  Vcl.Controls, Vcl.Forms, Vcl.Dialogs, Vcl.StdCtrls, System.IniFiles;

type
  TForm2 = class(TForm)
    RadioButtonMirrorServer: TRadioButton;
    RadioButtonMicrosoftServer: TRadioButton;
    procedure FormCreate(Sender: TObject);
    procedure RadioButtonMirrorServerClick(Sender: TObject);
    procedure RadioButtonMicrosoftServerClick(Sender: TObject);
  private
    { Private declarations }
  public
    { Public declarations }
    procedure LoadSymbolState;
    procedure SetSymbolState(selectSvr: Integer);
    procedure RefRadioButtonState;
  end;

var
  Form2: TForm2;
  g_SelectServer: Integer; //选择的服务器

const
  _STR_CONFIG_INI = 'Config.ini';

implementation

{$R *.dfm}


procedure TForm2.LoadSymbolState;
var
  Config: TIniFile;
  sPath: string;
begin
  try
    sPath := ExtractFilePath(Application.ExeName);
    if sPath <> '' then begin
      sPath := sPath + _STR_CONFIG_INI;
      if FileExists(sPath) then begin
        Config := TIniFile.Create(sPath);
        g_SelectServer := Config.ReadInteger('符号','服务器', g_SelectServer);
        Config.Free;
      end;
    end;
    RefRadioButtonState;
  except on e:Exception do
  end;
end;

procedure TForm2.SetSymbolState(selectSvr: Integer);
var
  Config: TIniFile;
  sPath: string;
begin
  try
    g_SelectServer := selectSvr;

    sPath := ExtractFilePath(Application.ExeName);
    if sPath <> '' then begin
      sPath := sPath + _STR_CONFIG_INI;
      Config := TIniFile.Create(sPath);
      Config.WriteInteger('符号','服务器',g_SelectServer);
      Config.Free;
    end;

    RefRadioButtonState;
  except on e:Exception do
  end;
end;

procedure TForm2.RadioButtonMicrosoftServerClick(Sender: TObject);
begin
  SetSymbolState(200);  //微软服务器
end;

procedure TForm2.RadioButtonMirrorServerClick(Sender: TObject);
begin
  SetSymbolState(100);  //镜像服务器
end;

procedure TForm2.RefRadioButtonState;
begin
  if g_SelectServer = 100 then begin
    RadioButtonMirrorServer.Checked := True;
  end else if g_SelectServer = 200 then begin
    RadioButtonMicrosoftServer.Checked := True;
  end;
end;

procedure TForm2.FormCreate(Sender: TObject);
begin
  RadioButtonMirrorServer.Checked := False;
  RadioButtonMicrosoftServer.Checked := False;
  LoadSymbolState;
end;

initialization
begin
  g_SelectServer := 100;  //默认选择镜像服务器
end;

end.

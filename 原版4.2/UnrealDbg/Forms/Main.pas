{
  注意: RAD Studio 11 编译64位程序时，如果将项目名命名为中文则无法进行调试。
}

unit Main;

interface

uses
  Winapi.Windows, Winapi.Messages, System.SysUtils, System.Variants, System.Classes, Vcl.Graphics,
  Vcl.Controls, Vcl.Forms, Vcl.Dialogs, Vcl.ExtCtrls, Vcl.StdCtrls,
  Vcl.Imaging.jpeg, System.ImageList, Vcl.ImgList, Vcl.Buttons, Vcl.ComCtrls,
  ActiveX, ComObj, GList, System.IniFiles, Vcl.Menus, System.JSON;

type

  TProtectRecord = record
    filename, filePath: string;
  end;
  PTProtectRecord = ^TProtectRecord;

  TDebugger = TProtectRecord;
  PTDebugger = ^TDebugger;


  TForm1 = class(TForm)
    RichEdit1: TRichEdit;
    GroupBox1: TGroupBox;
    Label_SystemName: TLabel;
    Label_SystemVer: TLabel;
    Label_CPU: TLabel;
    GroupBox2: TGroupBox;
    CheckBox1: TCheckBox;
    CheckBox2: TCheckBox;
    CheckBox3: TCheckBox;
    CheckBox4: TCheckBox;
    CheckBox5: TCheckBox;
    CheckBox6: TCheckBox;
    EnterVTDebuggingMode: TButton;
    PageControl1: TPageControl;
    TabSheet1: TTabSheet;
    DbgListView: TListView;
    OpenDialog1: TOpenDialog;
    DebuggerPopupMenu: TPopupMenu;
    StartDebuggerMenu: TMenuItem;
    AddDebuggerMenu: TMenuItem;
    DelDebuggerMenu: TMenuItem;
    TabSheet2: TTabSheet;
    pgcConfrontation: TPageControl;
    TabSheet3: TTabSheet;
    TL_EnabledConfrontat: TCheckBox;
    Image1: TImage;
    Label1: TLabel;
    GroupBox3: TGroupBox;
    TL_HandlerGetTickCountCheck: TCheckBox;
    TL_BlockResumeThread: TCheckBox;
    Label2: TLabel;
    procedure FormCreate(Sender: TObject);
    procedure EnterVTDebuggingModeClick(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure AddDebuggerMenuClick(Sender: TObject);
    procedure DelDebuggerMenuClick(Sender: TObject);
    procedure DbgListViewMouseDown(Sender: TObject; Button: TMouseButton;
      Shift: TShiftState; X, Y: Integer);
    procedure StartDebuggerMenuClick(Sender: TObject);
    procedure Image1MouseDown(Sender: TObject; Button: TMouseButton;
      Shift: TShiftState; X, Y: Integer);
    procedure Image1MouseUp(Sender: TObject; Button: TMouseButton;
      Shift: TShiftState; X, Y: Integer);
    procedure Image1MouseMove(Sender: TObject; Shift: TShiftState; X,
      Y: Integer);
    procedure TL_EnabledConfrontatClick(Sender: TObject);
    procedure TL_HandlerGetTickCountCheckClick(Sender: TObject);
    procedure TL_BlockResumeThreadClick(Sender: TObject);
    procedure FormCloseQuery(Sender: TObject; var CanClose: Boolean);
  private
    { Private declarations }
    procedure AppOnIdle(Sender: TObject; var Done: Boolean);
  public
    { Public declarations }
    lastHandle: HWND;
    m_SelectedProcessId: DWORD;
    m_targetProcessId: DWORD;

    procedure GetWindowsNTVer;
    procedure RefCheckBoxState;
    procedure RefDebuggerListView;
    procedure LoadCheckBoxState;
    procedure SaveCheckBoxState;
    procedure LoadDebuggerList;
    procedure SaveDebuggerList;
    function StartDebugger(szExe: string): Boolean;
    procedure Enabled_TL_Confrontation;
    procedure LoadCopyright;
  private
    m_Caption: string;
    m_Log1: string;
  end;

  procedure PrintLog(lpData: Pointer); stdcall;

exports
  PrintLog;

var
  Form1: TForm1;
  g_DebuggerList: TGList;

const
  _STR_KEY = '9dd14d00f5dd71bd';  {虚幻调试器经16位md5计算得到}
  _STR_COPYRIGHT = 'copyright.db';
  _STR_TL_EXE = '[TL.exe]';
  _STR_DEBUGGER_INI = 'DebuggerList.ini';
  _STR_CONFIG_INI = 'Config.ini';
  _STR_STARTUP_INFO_INI = 'StartupInfo.ini';
  crScope = 1;  //Delphi的光标状态都是负数，所以定义正数就可以避免与系统的光标冲突

implementation

uses Log, UnrealDbgDll, Grobal, LockThread, HandlerTLDetection, EventHandlerThread,
  GlobalVar, KeyVerification, VMProtectSDK, D_encryptionDll;

{$R *.dfm}

//导出函数供外部模块调用
procedure PrintLog(lpData: Pointer); stdcall;
begin
  try
    sLog.outInfo(WideCharToString(lpData));
  except on e:Exception do
    sLog.outError('[PrintLog]===>' + e.Message);
  end;
end;

function GetWMIOperatingSystem(WMIProperty: string): string;
var
  Wmi, Objs, Obj: OleVariant;
  Enum: IEnumVariant;
  C: Cardinal;
begin
  Wmi := CreateOleObject('WbemScripting.SWbemLocator');
  Objs := Wmi.ConnectServer('.','root/cimv2').ExecQuery('Select ' + WMIProperty + ' from Win32_OperatingSystem');
  Enum := IEnumVariant(IUnknown(Objs._NewEnum));
  Enum.Reset;
  Enum.Next(1, Obj, C);
  Obj := Obj.Properties_.Item(WMIProperty, 0).Value;
  Result := Obj;
end;

function GetWMIProcessor(WMIProperty: string): string;
var
  Wmi, Objs, Obj: OleVariant;
  Enum: IEnumVariant;
  C: Cardinal;
begin
  Wmi := CreateOleObject('WbemScripting.SWbemLocator');
  Objs := Wmi.ConnectServer('.','root/cimv2').ExecQuery('Select ' + WMIProperty + ' from Win32_Processor');
  Enum := IEnumVariant(IUnknown(Objs._NewEnum));
  Enum.Reset;
  Enum.Next(1, Obj, C);
  Obj := Obj.Properties_.Item(WMIProperty, 0).Value;
  Result := Obj;
end;

procedure TForm1.GetWindowsNTVer;
var
  VerInfo: array[0..64] of Char;
  WinDir: array[0..255] of Char;
  ntoskrnl, OSText: string;
begin
  try
    OSText := GetWMIOperatingSystem('Caption') + GetWMIOperatingSystem('Version') + '  ' + GetWMIProcessor('AddressWidth') + '位操作系统';
    Label_SystemName.Caption := Label_SystemName.Caption + '  ' + OSText;

    FillChar(WinDir[0],Length(WinDir) * 2,#0);
    GetSystemDirectory(WinDir,Length(WinDir));
    ntoskrnl := WinDir + '\ntoskrnl.exe';
    FillChar(VerInfo[0],Length(VerInfo) * 2,#0);
    Unreal_GetFileVersion(ntoskrnl, @VerInfo);
    Label_SystemVer.Caption := Label_SystemVer.Caption + '  ntoskrnl.exe  ' + VerInfo;

    Label_CPU.Caption := Label_CPU.Caption + '  ' + GetWMIProcessor('Name');

  except on e:Exception do
    sLog.outError('[TForm1.GetWindowsNTVer]===>' + e.Message);
  end;
end;

procedure HighlightWindowBorder(Handle: HWND);
var
  DC: HDC;
  Rect: TRect;
  Pen: HGDIOBJ;
  Rgn: HRGN;
  Brush: HBRUSH;
  SysColor: DWORD;
  original_pen: HGDIOBJ;
  original_brush: HGDIOBJ;
  WndWidth: Integer;
  WndHeight: Integer;
  frameWidth: Integer;
  frameHeight: Integer;
  screen_Width: Integer;
  screen_height: Integer;
begin
  if Handle <> 0 then begin
    DC := GetWindowDC(Handle);
    if DC <> 0 then begin
      try
        WndWidth := GetSystemMetrics(SM_CXBORDER); // 窗口边框的宽度（以像素为单位）
        WndHeight := GetSystemMetrics(SM_CYBORDER); // 窗口边框的高度（以像素为单位）

        Rgn := CreateRectRgn(0, 0, 0, 0);
        Pen := CreatePen(PS_INSIDEFRAME, 3 * WndWidth, RGB(0, 0, 0));  //创建画笔
        original_pen := SelectObject(DC, Pen);
        original_brush := SelectObject(DC, GetStockObject(NULL_BRUSH)); // 选择一个空心画刷 NULL_BRUSH
        SetROP2(DC, R2_NOT);
        if GetWindowRgn(Handle,Rgn) <> 0 then begin
          SysColor := GetSysColor(COLOR_WINDOWFRAME);
          Brush := CreateHatchBrush(HS_DIAGCROSS, SysColor); //45 度交叉影线
          FrameRgn(DC, Rgn, Brush, 3 * WndWidth, 3 * WndHeight);
          DeleteObject(Brush);
        end else begin
          frameWidth := GetSystemMetrics(SM_CXFRAME);        // SM_CXFRAME 边框的宽度（以像素为单位）
          frameHeight := GetSystemMetrics(SM_CYFRAME);       // SM_CYFRAME 边框的高度（以像素为单位）
          screen_Width := GetSystemMetrics(SM_CXSCREEN);       // SM_CXSCREEN 主显示器的屏幕宽度（以像素为单位）
          screen_height := GetSystemMetrics(SM_CYSCREEN);      // SM_CYSCREEN 主显示器的屏幕高度（以像素为单位）
          GetWindowRect(Handle, Rect);               // 获取窗口尺寸

          //检查窗口是否最大化
          if IsZoomed(Handle) then begin
            Rectangle(DC, frameWidth, frameHeight, frameWidth + screen_Width, screen_height + frameHeight);
          end else begin
            Rectangle(DC, 0, 0, Rect.right - Rect.left, Rect.bottom - Rect.top);// 绘制矩形边框 使用当前笔勾勒矩形轮廓，使用当前画刷填充矩形。
          end;
        end;
        SelectObject(DC, original_brush);          // 还原画刷
        SelectObject(DC, original_pen);            // 还原画笔
        DeleteObject(Pen);
        DeleteObject(Rgn);

      finally
        ReleaseDC(Handle, DC);
      end;
    end;
  end;
end;

//清理边框
procedure CleanBorder(Handle: HWND);
begin
  HighlightWindowBorder(Handle);
end;

procedure TForm1.Image1MouseDown(Sender: TObject; Button: TMouseButton;
  Shift: TShiftState; X, Y: Integer);
var
  ResourceStream: TResourceStream;
begin
  if (ssLeft in Shift) then begin
    // 从资源加载第一个图片
    ResourceStream := TResourceStream.Create(HInstance, 'Empty', RT_RCDATA);
    try
      Image1.Picture.Graphic.LoadFromStream(ResourceStream);
      SetCursor(Screen.Cursors[crScope]);
    finally
      ResourceStream.Free;
    end;
  end;
end;

procedure TForm1.Image1MouseMove(Sender: TObject; Shift: TShiftState; X,
  Y: Integer);
var
  Handle: HWND;
  Point: TPoint;
  SelfProcessId: DWORD;
  targetProcessId: DWORD;
begin
  // 检查鼠标左键是否被按下
  if (ssLeft in Shift) then begin

    // 获取鼠标当前位置
    if GetCursorPos(Point) then begin
      // 根据坐标获取窗口句柄
      Handle := WindowFromPoint(Point);
      if (Handle <> 0) and (Handle <> lastHandle) then begin
        GetWindowThreadProcessId(Application.Handle,@SelfProcessId);
        GetWindowThreadProcessId(Handle,@targetProcessId);
        if targetProcessId <> SelfProcessId then begin
          CleanBorder(lastHandle);
          HighlightWindowBorder(Handle);
          lastHandle := Handle;
          m_SelectedProcessId := targetProcessId;
        end;
      end;
    end;
  end;
end;

procedure TForm1.Image1MouseUp(Sender: TObject; Button: TMouseButton;
  Shift: TShiftState; X, Y: Integer);
var
  ResourceStream: TResourceStream;
  sText: string;
  btn: Integer;
  MsgRcd: TMsgRcd;
begin
  try
    if Button = mbLeft then begin
      // 切换图片
      if Assigned(Image1.Picture.Graphic) then
      begin
        ResourceStream := TResourceStream.Create(HInstance, 'Original', RT_RCDATA);
        try
          Image1.Picture.Graphic.LoadFromStream(ResourceStream);
        finally
          ResourceStream.Free;
        end;
      end;

      CleanBorder(lastHandle);
      lastHandle := 0;

      if m_SelectedProcessId <> 0 then begin
        if g_boEnabled_tl_confrontation_TL then begin
          sText := 'pid: ' + m_SelectedProcessId.ToString + ' 是TL的进程吗？';
          btn := MessageBox(0,PChar(sText),'选择目标进程:', MB_YESNO or MB_SYSTEMMODAL);
          if btn = IDYES then begin
            m_targetProcessId := m_SelectedProcessId;
            sLog.outInfo(_STR_TL_EXE + ' pid: ' + m_targetProcessId.ToString);
            MsgRcd.nAction := EVENT_ENABLED_TL_CONFRONTATION;
            g_EventHandlerThread.SendMsg(MsgRcd);
          end;
        end;
      end;
      m_SelectedProcessId := 0;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.Image1MouseUp]===>' + e.Message);
  end;
end;

//启动TL对抗
procedure TForm1.Enabled_TL_Confrontation;
var
  base: DWORD_PTR;
begin
  if m_targetProcessId <> 0 then begin
    if g_boEnabled_tl_confrontation_TL then begin
      if g_boHandlerGetTickCountCheck_TL then begin
        sLog.outDebug(_STR_TL_EXE + ' 正在处理GetTickCount检测...');

        //判断之前是否已经创建
        if Assigned(g_LockThread) then begin
          g_LockThread.Stop;
          g_LockThread.WaitFor;
          g_LockThread.Free;
          g_LockThread := nil;
        end;
        base := GetProcessModuleBase(m_targetProcessId,'TL.exe');
        Handler_TLDetection(base); //处理TL的检测
      end;

      if g_boBlockResumeThread_TL then begin
        sLog.outDebug(_STR_TL_EXE + ' 阻止游戏恢复线程');
        Unreal_TL_BlockGameResumeThread(m_targetProcessId);
      end;
    end;
  end;
end;

procedure TForm1.EnterVTDebuggingModeClick(Sender: TObject);
begin
  g_boLoginSuccess := True;

  try
    if g_boLoginSuccess then begin
      g_boStartService := True;
      EnterVTDebuggingMode.Enabled := False;
      if not Unreal_Initialize($9dd14d00f5dd71bd) then begin
        sLog.outError('设备未能成功运行!');
      end;
    end else begin
      sLog.outError('未通过授权认证!');
    end;
  except on e:Exception do
    sLog.outError('[TForm1.EnterVTDebuggingModeClick]===>' + e.Message);
  end;

end;

procedure TForm1.FormCloseQuery(Sender: TObject; var CanClose: Boolean);
var
  btn: Integer;
begin
  try
    if g_boStartService then begin
      btn := Application.MessageBox('您确定要关闭程序窗口吗？只有重启电脑才能完全退出VT调试模式!', '警告:', MB_YESNO or MB_ICONWARNING or MB_SYSTEMMODAL);
      if btn = ID_YES then begin
        CanClose := True;
      end else begin
        CanClose := False;
      end;
    end else begin
      CanClose := True;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.FormCloseQuery]===>' + e.Message);
  end;
end;

procedure TForm1.LoadCopyright;
var
  sFileName, sPath: string;
  decryptedDataLen: Integer;
  PlainText: array of Char;
  PlainTextLen: Integer;
  jsonStr: string;
  json: TJSONObject;
begin
  try
    m_Caption := '游戏安全实验室';
    m_Log1 := 'www.bpsend.net';

    sPath := ExtractFilePath(Application.ExeName);
    if sPath <> '' then begin
      sFileName := sPath + _STR_COPYRIGHT;
      if FileExists(sFileName) then begin
        //返回的是字节数，包含结尾空字符
        decryptedDataLen := D_encryption_DecryptDataFromFile(sFileName,_STR_KEY,nil);
        if decryptedDataLen > 0 then begin
          PlainTextLen := (decryptedDataLen div 2) + 1;
          SetLength(PlainText,PlainTextLen); //长度是字符个数
          FillChar(PlainText[0],Length(PlainText) * SizeOf(Char),#0);
          decryptedDataLen := D_encryption_DecryptDataFromFile(sFileName,_STR_KEY,@PlainText[0]);
          jsonStr := string(PChar(@PlainText[0]));
          json := json.ParseJSONValue(jsonStr) as TJSONObject;     //解析json
          //m_Caption := json.Values['虚幻调试器标题'].AsType<string>;
          //m_Log1 := json.Values['QQ群日志'].AsType<string>;
          json.Free;
          SetLength(PlainText,0);
        end;
      end;
    end;
  except on e:Exception do begin
    MessageBox(0,'获取版权信息失败!','错误:',MB_ICONERROR);
    ExitProcess(0);
  end;
  end;
end;

procedure TForm1.FormCreate(Sender: TObject);
begin
  RichEdit1.Clear;
  sLog.setLog(RichEdit1);
  Application.OnIdle := AppOnIdle;


  LoadCopyright;
  Caption := m_Caption;

  sLog.outDebug('感谢！微雨问海棠，界面参考的HvmDbg！');
  sLog.outDebug('使用本软件，对您造成的直接或者间接的损失，由您自行承担！');
  GetWindowsNTVer;

  Screen.Cursors[crScope] := LoadCursor(HInstance,'Cursor_1');

  g_boStartService := False;
  g_boLoginSuccess := False;

  VMProtectBeginVirtualization('VMP');
  g_Authentication := TAuthentication.Create;
  if g_Authentication.cdkeyLogin then begin
    {进程提权}
    SetPrivilege;
    g_DebuggerList := TGList.Create;
    g_EventHandlerThread := TEventHandlerThread.Create(False);
    LoadDebuggerList;
    LoadCheckBoxState;
  end;
  VMProtectEnd;
end;

//释放资源之前，一定要先释放线程
procedure TForm1.FormDestroy(Sender: TObject);
var
  I: Integer;
begin
  if g_boLoginSuccess then begin
    if g_LockThread <> nil then begin
      g_LockThread.Stop;
      g_LockThread.WaitFor;
      g_LockThread.Free;
    end;

    if g_EventHandlerThread <> nil then begin
      g_EventHandlerThread.Stop;
      g_EventHandlerThread.WaitFor;
      g_EventHandlerThread.Free;
    end;

    for I := 0 to g_DebuggerList.Count - 1 do
      Dispose(PTDebugger(g_DebuggerList.Items[I]));
    g_DebuggerList.Free;
  end;

  g_Authentication.Free;
end;

procedure TForm1.AppOnIdle(Sender: TObject; var Done: Boolean);
begin
  try
    sLog.OutputMsg;
    Done := False;
  except on e:Exception do
  end;
end;

procedure TForm1.RefCheckBoxState;
begin
  try
    TL_EnabledConfrontat.Checked := g_boEnabled_tl_confrontation_TL;
    TL_HandlerGetTickCountCheck.Checked := g_boHandlerGetTickCountCheck_TL;
    TL_BlockResumeThread.Checked := g_boBlockResumeThread_TL;
  except on e:Exception do
    sLog.outError('[TForm1.RefCheckBoxState]===>' + e.Message);
  end;
end;

procedure TForm1.RefDebuggerListView;
var
  I: Integer;
  pTemp: PTDebugger;
  item: TListItem;
begin
  try
    DbgListView.Clear;
    g_DebuggerList.Lock;
    try
      for I := 0 to g_DebuggerList.Count - 1 do begin
        pTemp := g_DebuggerList.Items[I];
        if Assigned(pTemp) then begin
          item := DbgListView.Items.Add;
          item.Caption := IntToStr(DbgListView.Items.Count);  //序号
          item.SubItems.Add(pTemp.filename);      //文件名
          item.SubItems.Add(pTemp.filePath);      //目录
        end;
      end;
    finally
      g_DebuggerList.UnLock;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.RefDebuggerListView]===>' + e.Message);
  end;
end;

//检测是否已经存在相同配置
function DebuggerItemExists(filename, filePath: string): Boolean;
var
  I: Integer;
  pTemp: PTDebugger;
begin
  try
    Result := False;
    if (filename <> '') and (filePath <> '') then begin
      g_DebuggerList.Lock;
      try
        for I := 0 to g_DebuggerList.Count - 1 do begin
          pTemp := g_DebuggerList.Items[I];
          if pTemp <> nil then begin
            if (pTemp.filename = filename) and (pTemp.filePath = filePath) then begin
              Result := True;
              Break;
            end;
          end;
        end;
      finally
        g_DebuggerList.UnLock;
      end;
    end;
  except on e:Exception do
    sLog.outError('[DebuggerItemExists]===>' + e.Message);
  end;
end;


procedure TForm1.LoadDebuggerList;
var
  Config: TIniFile;
  sPath, sText: string;
  ItemCount: Integer;
  I: Integer;
  filename, filePath: string;
  pTemp: PTDebugger;
begin
  try
    sPath := ExtractFilePath(Application.ExeName);
    if sPath <> '' then begin
      sPath := sPath + _STR_DEBUGGER_INI;
      if FileExists(sPath) then begin
        Config := TIniFile.Create(sPath);
        ItemCount := Config.ReadString('配置','Count','0').ToInteger;
        for I := 0 to ItemCount - 1 do begin
          sText := Config.ReadString('配置','Debugger' + I.ToString,'');
          if sText <> '' then begin
            sText := GetValidStr3(sText,filename,['&']);
            sText := GetValidStr3(sText,filePath,['&']);

            if (filename <> '') and (filePath <> '') then begin
              New(pTemp);
              FillChar(pTemp^,SizeOf(TDebugger),#0);
              pTemp.filename := filename;
              pTemp.filePath := filePath;

              g_DebuggerList.Lock;
              try
                g_DebuggerList.Add(pTemp);
              finally
                g_DebuggerList.UnLock;
              end;
              RefDebuggerListView;
            end;
          end;
        end;
        Config.Free;
      end;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.LoadDebuggerList]===>' + e.Message);
  end;
end;

procedure TForm1.SaveDebuggerList;
var
  Config: TIniFile;
  sPath: string;
  I: Integer;
  pTemp: PTDebugger;
begin
  try
    sPath := ExtractFilePath(Application.ExeName);
    if sPath <> '' then begin
      sPath := sPath + _STR_DEBUGGER_INI;
      if FileExists(sPath) then begin
        DeleteFile(sPath);
      end;

      Config := TIniFile.Create(sPath);
      g_DebuggerList.Lock;
      try
        Config.WriteString('配置','Count',g_DebuggerList.Count.ToString);
        for I := 0 to g_DebuggerList.Count - 1 do begin
          pTemp := g_DebuggerList.Items[I];
          if Assigned(pTemp) then begin
            Config.WriteString('配置','Debugger' + I.ToString,pTemp.filename + '&' + pTemp.filePath);
          end;
        end;
      finally
        g_DebuggerList.UnLock;
      end;
      Config.Free;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.SaveDebuggerList]===>' + e.Message);
  end;
end;

procedure TForm1.LoadCheckBoxState;
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
        g_boEnabled_tl_confrontation_TL := Config.ReadBool('TL','enabled_tl_confrontation', g_boEnabled_tl_confrontation_TL);
        g_boHandlerGetTickCountCheck_TL := Config.ReadBool('TL','handler_gettickcount_check', g_boHandlerGetTickCountCheck_TL);
        g_boBlockResumeThread_TL := Config.ReadBool('TL','BlockResumeThread', g_boBlockResumeThread_TL);
        Config.Free;
      end;
    end;
    RefCheckBoxState;
  except on e:Exception do
    sLog.outError('[TForm1.LoadCheckBoxState]===>' + e.Message);
  end;
end;

procedure TForm1.SaveCheckBoxState;
var
  Config: TIniFile;
  sPath: string;
begin
  try
    sPath := ExtractFilePath(Application.ExeName);
    if sPath <> '' then begin
      sPath := sPath + _STR_CONFIG_INI;

      Config := TIniFile.Create(sPath);
      Config.WriteBool('TL','enabled_tl_confrontation', g_boEnabled_tl_confrontation_TL);
      Config.WriteBool('TL','handler_gettickcount_check', g_boHandlerGetTickCountCheck_TL);
      Config.WriteBool('TL','BlockResumeThread', g_boBlockResumeThread_TL);
      Config.Free;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.SaveCheckBoxState]===>' + e.Message);
  end;
end;

function TForm1.StartDebugger(szExe: string): Boolean;
begin
  Result := False;
  if Unreal_StartProcess(szExe,GetApplicationPath) then begin
    Result := True;
  end;
end;

//创建文件副本
function CreateCopyFile(fileName: string; filePath: string): string;
var
  iniFile: TIniFile;
  sPath,sfixedPath: string;
  PrefixName,suffixName,sName: string;
begin
  try
    Result := '';
    sPath := ExtractFilePath(filePath);
    sfixedPath := sPath;
    if sPath <> '' then begin
      sPath := sPath + _STR_STARTUP_INFO_INI;

      iniFile := TIniFile.Create(sPath);
      if FileExists(sPath) then begin
        //取出原文件名
        sName := iniFile.ReadString(fileName,'fileName','');
        sName := sfixedPath + sName;
        DeleteFile(sName);  //删除旧文件
      end;

      PrefixName := GenerateRandomString(8);
      suffixName := GenerateRandomString(3);
      sName := PrefixName + '.' + suffixName;
      iniFile.WriteString(fileName,'fileName',sName);

      sName := sfixedPath + sName;
      CopyFile(PChar(filePath),PChar(sName),False);
      Result := sName;
      iniFile.Free;
    end;
  except on e:Exception do
    sLog.outError('[CreateCopyFile]===>' + e.Message);
  end;
end;

procedure TForm1.StartDebuggerMenuClick(Sender: TObject);
var
  pTemp: PTDebugger;
  I: Integer;
  sNewFile, fileName, filePath: string;
  item: TListItem;
begin
  try
    if Assigned(DbgListView.Selected) then begin
      item := DbgListView.Items.Item[DbgListView.ItemIndex];
      if Assigned(item) then begin
        fileName := item.SubItems.Strings[0];
        filePath := item.SubItems.Strings[1];
        if (fileName <> '') and (filePath <> '') then begin
          StartDebugger(filePath);
//          sNewFile := CreateCopyFile(fileName,filePath);
//          if sNewFile <> '' then begin
//            StartDebugger(sNewFile);
//          end;
        end;
      end;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.StartDebuggerMenuClick]===>' + e.Message);
  end;
end;


procedure TForm1.TL_BlockResumeThreadClick(Sender: TObject);
begin
  g_boBlockResumeThread_TL := TL_BlockResumeThread.Checked;
  SaveCheckBoxState;
end;

procedure TForm1.TL_EnabledConfrontatClick(Sender: TObject);
begin
  g_boEnabled_tl_confrontation_TL := TL_EnabledConfrontat.Checked;
  SaveCheckBoxState;
end;

procedure TForm1.TL_HandlerGetTickCountCheckClick(Sender: TObject);
begin
  g_boHandlerGetTickCountCheck_TL := TL_HandlerGetTickCountCheck.Checked;
  SaveCheckBoxState;
end;

procedure TForm1.DbgListViewMouseDown(Sender: TObject; Button: TMouseButton;
  Shift: TShiftState; X, Y: Integer);
var
  curpos: TPoint;
begin
  try
    if (Button = mbRight) then begin
      if Assigned(TListView(Sender).Selected) then begin
        DelDebuggerMenu.Enabled := True;
      end else begin
        DelDebuggerMenu.Enabled := False;
      end;
      GetCursorPos(curpos);
      DebuggerPopupMenu.Popup(curpos.X + 10,curpos.Y);
    end;
  except on e:Exception do
    sLog.outError('[TForm1.DbgListViewMouseDown]===>' + e.Message);
  end;
end;


procedure TForm1.DelDebuggerMenuClick(Sender: TObject);
var
  pTemp: PTDebugger;
  I: Integer;
  sText, fileName, filePath: string;
  item: TListItem;
begin
  try
    if Assigned(DbgListView.Selected) then begin
      item := DbgListView.Items.Item[DbgListView.ItemIndex];
      if Assigned(item) then begin
        fileName := item.SubItems.Strings[0];
        filePath := item.SubItems.Strings[1];
        if (fileName <> '') and (filePath <> '') then begin
          if Application.MessageBox('是否确认移除选中的项？', '确认信息', MB_YESNO + MB_ICONQUESTION) = IDYES then begin
            g_DebuggerList.Lock;
            try
              for I := g_DebuggerList.Count - 1 downto 0 do begin
                pTemp := g_DebuggerList.Items[I];
                if pTemp <> nil then begin
                  if (pTemp.filename = fileName) and (pTemp.filePath = filePath) then begin
                    g_DebuggerList.Remove(pTemp);
                    Dispose(pTemp);
                    Break;
                  end;
                end else begin
                  g_DebuggerList.Delete(I);
                end;
              end;
            finally
              g_DebuggerList.UnLock;
            end;
            SaveDebuggerList;
            RefDebuggerListView;
          end;
        end;
      end;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.DelDebuggerMenuClick]===>' + e.Message);
  end;
end;

procedure TForm1.AddDebuggerMenuClick(Sender: TObject);
var
  filename, filePath: string;
  pTemp: PTDebugger;
begin
  try
    OpenDialog1.Title := '请选择调试器';
    OpenDialog1.FileName := '';
    OpenDialog1.Execute;
    filename := ExtractFileName(OpenDialog1.FileName);  //提取文件名
    filePath := OpenDialog1.FileName;  //提取文件路径

    if (filename <> '') and (filePath <> '') then begin
      if not DebuggerItemExists(filename,filePath) then begin
        New(pTemp);
        FillChar(pTemp^,SizeOf(TDebugger),#0);
        pTemp.filename := filename;
        pTemp.filePath := filePath;

        g_DebuggerList.Lock;
        try
          g_DebuggerList.Add(pTemp);
        finally
          g_DebuggerList.UnLock;
        end;
        SaveDebuggerList;
        RefDebuggerListView;
      end else begin
        Application.MessageBox('该输入项已经存在于列表中..', '警告:', MB_ICONWARNING);
      end;
    end;
  except on e:Exception do
    sLog.outError('[TForm1.AddDebuggerMenuClick]===>' + e.Message);
  end;
end;

end.

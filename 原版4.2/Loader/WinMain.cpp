#include "WinMain.h"
#include "Symbols.h"

#define TIMER_PROGRESSBAR 1
#define TIMER_TEXT 2

const int MAX_DOTS = 3;
int dotCount = 0;
std::wstring baseText1(L"正在初始化程序");
std::wstring modText;
std::wstring displayText;
int currentTextIndex = 0; // 当前绘制的文本索引

int nWidth = 0;
int progress = 0; // 进度条的当前进度
int tickcount = 0;


HWND g_hwnd;
std::wstring curdir;

void DrawBackground(HDC hdc)
{
	// 加载图片
	HBITMAP hBitmap = (HBITMAP)LoadImage(NULL, curdir.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

	// 获取图片原始尺寸
	BITMAP bitmap;
	GetObject(hBitmap, sizeof(BITMAP), &bitmap);
	int width = bitmap.bmWidth;
	int height = bitmap.bmHeight;
	int x = 0;
	int y = 0;

	// 绘制图片
	HDC memDC = CreateCompatibleDC(hdc);
	SelectObject(memDC, hBitmap);
	BitBlt(hdc, x, y, width, height, memDC, 0, 0, SRCCOPY);

	// 释放资源
	DeleteDC(memDC);
	DeleteObject(hBitmap);
}

void DrawProgressBar(HWND hwnd, HDC hdc, PAINTSTRUCT ps)
{
	// 绘制进度条
	RECT rect;
	GetClientRect(hwnd, &rect);
	rect.top = rect.bottom - 5; // 进度条的顶部位置
	//rect.bottom -= 10; // 进度条的底部位置
	rect.right = rect.left + progress; // 根据当前进度调整宽度

	// 填充背景
	//FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

	//FillRect(hdc, &rect, (HBRUSH)(COLOR_HIGHLIGHT + 1)); // 使用高亮颜色填充进度条


	// 创建绿色画刷
	HBRUSH hGreenBrush = CreateSolidBrush(RGB(0, 255, 0)); // 创建绿色画刷
	FillRect(hdc, &rect, hGreenBrush); // 使用绿色填充进度条

	DeleteObject(hGreenBrush); // 删除画刷
}

void DrawString(HWND hwnd, HDC hdc)
{
	// 设置文本颜色和背景颜色
	SetTextColor(hdc, RGB(255, 255, 255)); // 白色
	SetBkMode(hdc, TRANSPARENT);

	// 创建字体
	HFONT hFont = CreateFont(
		20,            // 字体高度
		0,             // 字体宽度
		0,             // 旋转角度
		0,             // 基线角度
		FW_NORMAL,     // 字体粗细
		FALSE,         // 斜体
		FALSE,         // 下划线
		FALSE,         // 删除线
		DEFAULT_CHARSET, // 字符集
		OUT_DEFAULT_PRECIS, // 外部精度
		CLIP_DEFAULT_PRECIS, // 剪裁精度
		DEFAULT_QUALITY, // 质量
		DEFAULT_QUALITY, // 字体质量
		L"宋体"      // 字体名称
	);

	// 选择字体到设备上下文
	SelectObject(hdc, hFont);

	// 绘制文字	
	RECT rect;
	GetClientRect(hwnd, &rect);

	// 获取文本的宽度和高度
	SIZE textSize;
	GetTextExtentPoint32(hdc, displayText.c_str(), displayText.length(), &textSize);

	// 计算绘制位置，使文本右对齐
	int x = rect.right - textSize.cx; // 右侧位置
	TextOut(hdc, x, rect.bottom - 50, displayText.c_str(), displayText.length());
	DeleteObject(hFont);
}

void DownloadSymbol()
{
	std::wstring Out;
	std::vector<std::wstring> modules = {
		L"ntoskrnl.exe",
		L"win32kbase.sys",
		L"win32kfull.sys"
	};

	for (const auto& mod : modules) {
		modText = mod;
		std::wstring FullPath = L"C:\\Windows\\System32\\" + mod;
		if (!DownloadSymbol_internal(FullPath, L"C:\\Symbols\\", &Out, true)) {
			exit(0);
		}
	}
}


// 定义窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
	{
		//SetTimer(hwnd, TIMER_PROGRESSBAR, 100, NULL); // 每100毫秒更新一次
		SetTimer(hwnd, TIMER_TEXT, 500, NULL); // 每500毫秒更新一次		
		InitThread();
		break;
	}
	case WM_DESTROY:
	{
		//KillTimer(hwnd, TIMER_PROGRESSBAR); // 关闭定时器
		KillTimer(hwnd, TIMER_TEXT); // 关闭定时器
		PostQuitMessage(0);
		break;
	}
	case USER_PROGRESS_BAR:
	{
		// 更新进度条
		progress = nWidth / 100 * wParam;
		if (progress > nWidth)
		{
			progress = 0; // 重置进度
		}
		displayText = L"下载" + modText + L"符号表(" + std::to_wstring(wParam) + L"%)";
		InvalidateRect(hwnd, NULL, TRUE); // 请求重绘
		break;
	}
	case WM_TIMER:
	{
		if (wParam == TIMER_TEXT)
		{
			if (progress == nWidth || progress == 0)
			{
				//环形计数器
				dotCount = (dotCount + 1) % (MAX_DOTS + 1);

				// 切换到下一个文本
				//if (dotCount == 0) {
				//	currentTextIndex = (currentTextIndex + 1) % 2; // 只有两个文本
				//}
				// 根据当前文本索引选择要显示的文本
				std::wstring dots(dotCount, L'.');
				displayText = baseText1 + dots;

				if (progress == nWidth)
				{
					tickcount++;
				}				

				//if (currentTextIndex == 0) {
				//	displayText = baseText1 + dots;
				//}
				//else if (currentTextIndex == 1) {
				//	displayText = baseText2 + dots;
				//}
			}
		}		
		else if (wParam == TIMER_PROGRESSBAR)
		{
			//// 更新进度条
			//progress += 10;
			//if (progress > nWidth)
			//{ 
			//	progress = 0; // 重置进度
			//}
		}
		InvalidateRect(hwnd, NULL, TRUE); // 请求重绘
		break;
	}
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		DrawBackground(hdc);  //渲染背景
		DrawString(hwnd, hdc);
		DrawProgressBar(hwnd, hdc, ps); //渲染进度条
		EndPaint(hwnd, &ps);
		break;
	}
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int DisplayBrand(
	_In_           HINSTANCE hInstance,
	_In_opt_       HINSTANCE hPrevInstance,
	_In_           LPSTR     lpCmdLine,
	_In_           int       nShowCmd
)
{
	// 注册窗口类
	const wchar_t CLASS_NAME[] = L"DisplayBrandClass";

	// 注册窗口类
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	//wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = CLASS_NAME;
	wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wcex)) {
		return 0;
	}

	curdir = FileSystem::GetModuleDirectory(NULL);
	if (curdir.empty())
	{
		Common::ReportSeriousError("%s[%d] 获取程序目录失败! (error: %d)", __func__, __LINE__, GetLastError());
		return 0;
	}
	curdir += L"res\\mm.pak";

	if (!Common::fileExists(curdir) || 
		(_stricmp(calculateMD5(Common::wideStringToString2(curdir)).c_str(),"5F499EB6E77B203FA96DEB2A121FBA13") != 0))
	{
		Common::ReportSeriousError("%s[%d] 资源文件已损坏! (error: %d)", __func__, __LINE__, GetLastError());
		return 0;
	}


	// 加载图片
	HBITMAP hBitmap = (HBITMAP)LoadImage(NULL, curdir.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

	DWORD err = GetLastError();

	// 获取图片原始尺寸
	BITMAP bitmap;
	GetObject(hBitmap, sizeof(BITMAP), &bitmap);
	int originalWidth = bitmap.bmWidth;
	int originalHeight = bitmap.bmHeight;
	nWidth = bitmap.bmWidth;
	DeleteObject(hBitmap);

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	// 缩放或裁剪图片以适应屏幕
	int width, height, x, y;
	if (originalWidth > screenWidth || originalHeight > screenHeight)
	{
		// 图片尺寸大于屏幕尺寸，需要进行缩放或裁剪
		// 计算缩放比例
		float scaleWidth = (float)screenWidth / originalWidth;
		float scaleHeight = (float)screenHeight / originalHeight;
		float scale = min(scaleWidth, scaleHeight);

		// 缩放图片尺寸
		width = (int)(originalWidth * scale);
		height = (int)(originalHeight * scale);

		// 计算屏幕中心位置
		x = (screenWidth - width) / 2;
		y = (screenHeight - height) / 2;
	}
	else
	{
		// 图片尺寸小于等于屏幕尺寸，直接居中显示
		width = originalWidth;
		height = originalHeight;
		x = (screenWidth - width) / 2;
		y = (screenHeight - height) / 2;
	}

	// 创建窗口
	HWND hwnd = CreateWindowEx(
		0,                              // 扩展窗口样式
		CLASS_NAME,                     // 窗口类名
		L"",                // 窗口标题
		WS_POPUP,                       // 窗口样式
		x, y,                           // 窗口位置
		width, height,                       // 窗口尺寸
		NULL,                           // 父窗口句柄
		NULL,                           // 菜单句柄
		hInstance,                      // 实例句柄
		NULL                            // 附加数据指针
	);

	if (hwnd == NULL)
	{
		return 0;
	}

	g_hwnd = hwnd;
	ShowWindow(hwnd, nShowCmd);
	UpdateWindow(hwnd);

	// 消息循环
	MSG msg = { 0 };

	while (1)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// test if this is a quit
			if (msg.message == WM_QUIT)
				break;

			// translate any accelerator keys
			TranslateMessage(&msg);

			// send the message to the window proc
			DispatchMessage(&msg);

		} // end if

		if (tickcount > 10)
		{
			DestroyWindow(hwnd);
			return 1;
		}

	} // end while
	return 0;
}

unsigned __stdcall DownloadSymbolThread(PVOID pArgList)
{
	Sleep(500);
	DownloadSymbol();
	return 0;
}

void InitThread()
{
	HANDLE hThread = (HANDLE)_beginthreadex(nullptr, 0, DownloadSymbolThread, nullptr, 0, nullptr);
	CloseHandle(hThread);
}


PROCESS_INFORMATION _StartProcess_(PSTARTUP_INFO pStartInfo)
{
	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	TCHAR szDllPath[256] = { 0 };
	BOOL is64Process;
	TCHAR* szExe = pStartInfo->szExe;
	TCHAR* sPath = pStartInfo->sPath;

	if (!CreateProcess(szExe,
		pStartInfo->sCommandLine,
		NULL,
		NULL,
		NULL,
		0,
		NULL,
		NULL,
		&si,
		&pi
	))
	{
		Common::ReportSeriousError("%s[%d] 启动虚幻调试器失败! (error: %d)", __func__, __LINE__, GetLastError());
	}
	else
	{
	}
	return pi;
}

PROCESS_INFORMATION StartProcess_internal(std::wstring processPath, std::wstring procName, std::wstring sCommandLine)
{
	PROCESS_INFORMATION pi = { 0 };

	if (!processPath.empty())
	{
		std::wstring exePath = processPath + procName;
		STARTUP_INFO info = { 0 };
		wcscpy(info.szExe, exePath.c_str());
		wcscpy(info.sPath, processPath.c_str());
		wcscpy(info.sCommandLine, sCommandLine.c_str());
		pi = _StartProcess_(&info);
	}
	return pi;
}

void StartProcess()
{
	std::wstring filename = FileSystem::GetModuleDirectory(NULL);
	if (!filename.empty())
	{
		StartProcess_internal(filename, L"UnrealDbg.aes", L"");
	}
}

int CALLBACK WinMain(
	_In_           HINSTANCE hInstance,
	_In_opt_       HINSTANCE hPrevInstance,
	_In_           LPSTR     lpCmdLine,
	_In_           int       nShowCmd
)
{
	if (DisplayBrand(hInstance, hPrevInstance, lpCmdLine, nShowCmd))
	{
		StartProcess();
	}	
	return 0;
}
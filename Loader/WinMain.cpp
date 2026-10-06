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
		Common::ReportSeriousError("%s[%d] 获取程序目录失败! (错误码：%d)", __func__, __LINE__, GetLastError());
		return 0;
	}
	curdir += L"res\\mm.pak";

	if (!Common::fileExists(curdir) ||
		(_stricmp(calculateMD5(Common::wideStringToString2(curdir)).c_str(),"5F499EB6E77B203FA96DEB2A121FBA13") != 0))
	{
		Common::ReportSeriousError("%s[%d] 资源文件已损坏! (错误码：%d)", __func__, __LINE__, GetLastError());
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
			// 检查是否为退出消息
			if (msg.message == WM_QUIT)
			{
				break;
			}

			// 转换任何加速器键
			TranslateMessage(&msg);

			// 发送消息到窗口过程
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
		Common::ReportSeriousError("%s[%d] 启动虚幻调试器失败! (错误码：%d)", __func__, __LINE__, GetLastError());
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

// 新版统一入口。旧启动器会先验证 res\mm.pak 并启动已经淘汰的
// UnrealDbg.aes；发布目录精简后，继续走那条路径会把“历史启动图不存在”
// 错报成“资源文件已损坏”。这里直接转交给同一配置的原生 WinUI 前端。
bool LaunchWinUiFrontend()
{
	std::vector<wchar_t> modulePath(32768, L'\0');
	const DWORD copied = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
	if (copied == 0 || copied >= modulePath.size())
	{
		const DWORD error = copied == 0 ? GetLastError() : ERROR_FILENAME_EXCED_RANGE;
		const std::wstring message = L"无法定位启动器自身路径。\r\n\r\n错误码：" + std::to_wstring(error) +
			L"\r\n原因：Windows 未能返回完整的启动器文件路径。\r\n\r\n"
			L"解决方案：请将完整发布目录重新部署到较短路径后再试。";
		MessageBoxW(nullptr, message.c_str(), L"虚幻调试器启动失败", MB_OK | MB_ICONERROR);
		return false;
	}

	const std::wstring launcherExecutable(modulePath.data(), copied);
	const size_t executableSeparator = launcherExecutable.find_last_of(L"\\/");
	if (executableSeparator == std::wstring::npos)
	{
		MessageBoxW(nullptr, L"无法从启动器路径解析发布目录。请从完整的 x64 发布目录运行程序。",
			L"虚幻调试器启动失败", MB_OK | MB_ICONERROR);
		return false;
	}
	const std::wstring launcherDirectory = launcherExecutable.substr(0, executableSeparator);
	const size_t configurationSeparator = launcherDirectory.find_last_of(L"\\/");
	if (configurationSeparator == std::wstring::npos)
	{
		MessageBoxW(nullptr, L"无法从启动器路径解析构建配置。请从完整的 x64 发布目录运行程序。",
			L"虚幻调试器启动失败", MB_OK | MB_ICONERROR);
		return false;
	}
	const std::wstring configuration = launcherDirectory.substr(configurationSeparator + 1);
	const std::wstring x64Directory = launcherDirectory.substr(0, configurationSeparator);
	
	// 优先查找同级目录（扁平化部署），其次查找 WinUI 子目录（标准构建输出）
	std::wstring frontendPath = launcherDirectory + L"\\UnrealDbgProNativeWinUI.exe";
	DWORD attributes = GetFileAttributesW(frontendPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
	{
		// 同级目录找不到，尝试 WinUI 子目录
		frontendPath = x64Directory + L"\\WinUI\\" + configuration + L"\\UnrealDbgProNativeWinUI.exe";
		attributes = GetFileAttributesW(frontendPath.c_str());
	}
	
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
	{
		const std::wstring message =
			L"未找到原生 WinUI 前端，无法启动虚幻调试器。\r\n\r\n"
			L"尝试路径：\r\n" + launcherDirectory + L"\\UnrealDbgProNativeWinUI.exe\r\n"
			L"或\r\n" + frontendPath + L"\r\n\r\n"
			L"原因：当前发布目录不完整。\r\n\r\n"
			L"解决方案：请将 UnrealDbgProNativeWinUI.exe 复制到与虚幻调试器.exe 同级目录，"
			L"或保留完整 x64 目录结构。";
		MessageBoxW(nullptr, message.c_str(), L"虚幻调试器启动失败", MB_OK | MB_ICONERROR);
		return false;
	}

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	std::wstring commandLine = L"\"" + frontendPath + L"\"";
	// 工作目录设置为前端所在目录的父目录（x64\Release），使前端能正确找到 bin\UnrealDbgProDll.dll
	const std::wstring workingDirectory = launcherDirectory;
	if (!CreateProcessW(frontendPath.c_str(), &commandLine[0], nullptr, nullptr, FALSE, 0, nullptr,
		workingDirectory.c_str(), &startup, &process))
	{
		const DWORD error = GetLastError();
		const std::wstring message =
			L"原生 WinUI 前端启动失败。\r\n\r\n"
			L"文件：\r\n" + frontendPath + L"\r\n\r\n"
			L"错误码：" + std::to_wstring(error) + L"\r\n"
			L"原因：Windows 无法创建前端进程，可能是文件被安全软件拦截、运行时组件缺失或目录权限不足。\r\n\r\n"
			L"解决方案：确认 Microsoft.WindowsAppRuntime.Bootstrap.dll 位于 WinUI 目录，安装 Windows App Runtime 1.8 x64，"
			L"并以管理员身份运行；若仍失败，请查看 WinUI\\" + configuration + L"\\Log\\log.ini。";
		MessageBoxW(nullptr, message.c_str(), L"虚幻调试器启动失败", MB_OK | MB_ICONERROR);
		return false;
	}

	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}

int CALLBACK WinMain(
	_In_           HINSTANCE hInstance,
	_In_opt_       HINSTANCE hPrevInstance,
	_In_           LPSTR     lpCmdLine,
	_In_           int       nShowCmd
)
{
	// 不再调用 DisplayBrand：它属于旧 Delphi/AES 启动链，并依赖已经
	// 归档的 mm.pak 启动图片。默认入口现在始终使用原生 WinUI 3 前端。
	return LaunchWinUiFrontend() ? 0 : 1;
}
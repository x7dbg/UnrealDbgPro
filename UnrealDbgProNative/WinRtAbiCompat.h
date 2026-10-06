#pragma once

// Windows.h（经 timeapi.h）会定义一个函数式宏：
//
//     #define GetCurrentTime() timeGetTime()
//
// 而 Windows App SDK 生成的 C++/WinRT 投影头
// （x64\WinUI\Release\Generated Files\winrt\Microsoft.UI.Xaml.Media.Animation.h）
// 中 Storyboard 的 ABI 适配类里含有同名成员：
//
//     int32_t __stdcall GetCurrentTime(int64_t* result) noexcept final try
//
// 宏展开后形参名 result 被剥离，编译器随即报出
// "C3861: result: 找不到标识符" 与 "C2065: result: 未声明的标识符"。
// 只要同一个翻译单元里同时出现 Windows.h 与动画投影头就会踩到该坑，
// 因此这里在包含 winrt 投影头之前撤销这个宏。
//
// 使用约定：必须在包含 Windows.h（本工程经 Core.h 间接引入）之后、
// 包含任何 winrt 投影头之前包含本文件。当前由 LogPanel.cpp 引入。
//
// 撤销后若仍有代码调用 Win32 的 GetCurrentTime()，会正常解析到 winuser.h
// 声明的同名函数，语义不受影响。

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

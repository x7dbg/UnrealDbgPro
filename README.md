# UnrealDbgPro

> 基于 Intel VT-x 硬件虚拟化技术的原生 Windows 内核调试器 —— 深度重构与现代化演进

[![许可证](https://img.shields.io/badge/许可证-GPLv3-blue.svg)](LICENSE)
[![平台](https://img.shields.io/badge/平台-Windows%2010%20%2F%2011-0078D6.svg)](#系统要求)
[![语言](https://img.shields.io/badge/语言-C%2B%2B17-00599C.svg)](#编译指南)

---

## 项目渊源

本项目是在**精易论坛花老板 UnrealDbg 二开版**的基础上深度重构而来，而花老板的版本本身基于**大师 UnrealDbg 4.2 泄露版源码**进行二次开发。

| 版本 | 说明 |
|------|------|
| **大师 UnrealDbg 4.2 泄露版** | 原始 VT-x 调试器核心架构与驱动代码，奠定了项目的技术基础 |
| **精易论坛花老板二开版** | 社区二次开发版本，包含部分功能改进与界面调整（即本项目的前身） |
| **UnrealDbgPro（当前版本）** | 在花老板二开版基础上进行全面重构与现代化演进 |

**感谢所有前辈与社区开发者的开源精神，正是站在巨人的肩膀上，才有了今天的 UnrealDbgPro。**

---

## 项目简介

**UnrealDbgPro** 是一款完全开源的 Windows 内核级调试器，通过 **Intel VT-x 硬件虚拟化**在 Ring -1 层构建虚拟机监视器（Hypervisor），从硬件层面接管处理器的调试能力。

与传统调试器不同，UnrealDbgPro 将调试逻辑下沉到 VMX Root 模式：

- 调试断点由 **VM-Exit 事件**触发，而非软件 `INT3` 指令替换
- 内存访问由 **EPT（扩展页表）**控制，可实现隐藏断点与内存隔离
- 调试子系统调用由**桥接驱动**按目标系统版本精确适配，避免依赖脆弱的硬编码偏移

当前版本提供全新的**原生 WinUI 3 C++ 前端**（完全基于 XAML 原生控件，不嵌入任何 WebView），后端继续沿用成熟的 `UnrealDbgProDll` 接口与驱动协议。

---

## 核心特性

| 特性 | 说明 |
|------|------|
| **VT-x 硬件虚拟化** | 基于 Intel VMX / VMCS / EPT 构建 Hypervisor，在 Ring -1 层拦截调试事件 |
| **原生 WinUI 3 界面** | C++ 原生 XAML 控件，暗黑主题，单页状态概览 + 调试器列表 + TL 选项 + 底部实时分级日志 |
| **Windows 版本精确适配** | 按 Windows **Build + UBR** 精确匹配 Win10 / Win11 桥接驱动，未知版本在加载前拒绝 |
| **驱动事务状态机** | 驱动分阶段加载，任一阶段失败均回滚本次创建的服务并给出可操作的错误诊断 |
| **V2 IOCTL 安全协议** | 设备通过受控 ACL 访问，V2 指令要求显式读写句柄权限 |
| **符号缓存管理** | PDB / EXP / LIB 调试符号管理，版本变化自动失效重验 |
| **完整的诊断体系** | 分级日志、单进程会话日志、只读环境快照、一键式诊断包采集 |
| **多调试器集成** | 支持注册并启动多个外部调试器（如 x64dbg） |
| **TL 反调试缓解** | 可选 `GetTickCount` 挂钩与目标进程线程阻塞 |

---

## 系统架构

UnrealDbgPro 采用**五层架构**，用户态、内核态与硬件层职责清晰分离：

```
┌──────────────────────────────────────────────┐
│         UnrealDbgProNativeWinUI.exe            │  ← 前端 (用户态)
│  WinUI 3 / XAML / 暗黑主题                     │
│  WinUiMain.cpp → WinUiController.cpp           │
├──────────────────────────────────────────────┤
│         UnrealDbgProDll.dll                    │  ← 后端 (用户态)
│  初始化驱动 / 管理符号 / 启动目标进程 / TL选项  │
├──────────────────────────────────────────────┤
│         Hook64.dll                             │  ← 注入 DLL (目标进程内)
│  Hook 调试API / 通道通信 / 断点管理            │
├──────────────────────────────────────────────┤
│  DbgkSysWin10.sys / DbgkSysWin11.sys          │  ← 桥接驱动 (内核态)
│  拦截 Dbgk 系统调用 / 调试事件分发             │
├──────────────────────────────────────────────┤
│         VT_Driver.sys                          │  ← VT 核心驱动 (内核态)
│  VMX 初始化 / EPT 映射 / VM-Exit 处理 / VMCALL  │
└──────────────────────────────────────────────┘
```

| 层级 | 组件 | 职责 |
|------|------|------|
| 用户态 · 界面 | `UnrealDbgProNativeWinUI.exe` | XAML 界面、状态展示、用户交互、日志呈现 |
| 用户态 · 后端 | `UnrealDbgProDll.dll` | 驱动加载编排、符号下发、进程启动、TL 选项控制 |
| 用户态 · 注入 | `Hook64.dll` | 注入目标进程，挂钩调试相关 API，经通道回传事件 |
| 内核态 · 桥接 | `DbgkSysWin10/11.sys` | 按系统版本适配调试子系统调用与内核结构偏移 |
| 内核态 · 核心 | `VT_Driver.sys` | VMX 初始化、EPT 映射、VM-Exit 处理、VMCALL 服务 |
| 硬件层 | Intel VT-x | 提供 VMX 根/非根模式切换与扩展页表能力 |

---

## 系统要求

### Windows 版本支持

程序使用 `RtlGetVersion` 获取真实内核版本，并读取注册表 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\UBR` 精确匹配。只有下表列出的 Build + UBR 范围才允许进入驱动事务；未知版本会在加载任何驱动前返回 `ERROR_NOT_SUPPORTED`。

| Build | 系统版本 | 桥接驱动 | 当前状态 |
|------:|----------|----------|----------|
| 19045 | Windows 10 22H2 | `DbgkSysWin10.sys` | 开发基线 |
| 22000 | Windows 11 21H2 | `DbgkSysWin11.sys` | 开发基线 |
| 22621 | Windows 11 22H2 | `DbgkSysWin11.sys` | 开发基线 |
| 22631 | Windows 11 23H2 | `DbgkSysWin11.sys` | 开发基线 |
| 26100 | Windows 11 24H2 | `DbgkSysWin11.sys` | 开发基线 |
| 26200 | Windows 11 25H2 | `DbgkSysWin11.sys` | 当前开发基线 |
| 其他 | 任意未列版本 | — | **加载前拒绝**，返回 `ERROR_NOT_SUPPORTED` |

> **「开发基线」不等于正式兼容承诺。** 只有当兼容性矩阵中的 CPU、Hyper-V/VBS/HVCI、签名与重启用例全部通过后，条目才可标记为「已验证」。Windows 更新导致 Build 变化后，必须重新执行完整矩阵。

### 硬件与软件要求

**硬件**
- 支持 **Intel VT-x** 的处理器（需在 BIOS/UEFI 中启用虚拟化）
- 支持 **EPT（Extended Page Tables）**，即 Intel Nehalem 及以后的处理器

**软件**
- Windows 10 22H2 或 Windows 11（详见上表）
- **Windows App Runtime 1.8 x64**（运行 WinUI 3 前端所必需）
- 管理员权限（驱动加载与调试能力均依赖提升权限）

---

## 编译指南

### 环境准备

1. 安装 **Visual Studio 2019**（勾选「使用 C++ 的桌面开发」工作负载）
2. 安装匹配版本的 **Windows Driver Kit（WDK）**
3. 安装 **Windows App Runtime 1.8 x64**（运行与调试 WinUI 前端所需）
4. 安装 **CMake**（用于编译第三方库）

### 第三方库编译

Common 目录中包含多个开源第三方库，你可以自行编译获取 lib 文件，也可以直接使用项目中已提供的预编译版本。

#### 1. Detours（Microsoft Detours）

**来源**：https://github.com/microsoft/Detours

**编译步骤**：
```powershell
# 克隆仓库
git clone https://github.com/microsoft/Detours.git
cd Detours

# 使用 nmake 编译（需要 Visual Studio 开发者命令提示符）
nmake

# 编译产物位于 src/x64/ 目录下
# detours.lib 即为所需库文件
```

**使用方法**：将编译好的 `detours.lib` 和 `detours.h` 复制到 `Common/Detours/x64/` 目录。

#### 2. jsoncpp

**来源**：https://github.com/open-source-parsers/jsoncpp

**编译步骤**：
```powershell
# 克隆仓库
git clone https://github.com/open-source-parsers/jsoncpp.git
cd jsoncpp

# 使用 CMake 生成 Visual Studio 项目
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release

# 打开 jsoncpp.sln 编译 jsoncpp_static 项目
# 或使用命令行
cmake --build . --config Release --target jsoncpp_static
```

**使用方法**：将编译好的 `jsoncpp_static.lib` 和 `include/json/` 头文件复制到 `Common/include/json/` 目录。

#### 3. TinyXML

**来源**：http://www.grinninglizard.com/tinyxml/

**编译步骤**：
TinyXML 是纯源码库，无需单独编译 lib，直接将以下源文件加入你的项目即可：
- `tinyxml.cpp`
- `tinyxmlparser.cpp`
- `tinyxmlerror.cpp`
- `tinystr.cpp`

项目中已包含这些源文件，位于 `Common/TinyXML/` 目录。

#### 4. ia32-doc

**来源**：https://github.com/wbenny/ia32-doc

**说明**：ia32-doc 是 Intel 指令集与 VMX 结构的 YAML 定义文件，可通过 Python 脚本生成 C/C++ 头文件。

**生成步骤**：
```powershell
cd Common/Ring0/ia32-doc

# 安装依赖
pip install -r requirements.txt

# 生成 C 头文件
python main.py

# 生成产物位于 out/ 目录下
# ia32.h / ia32.hpp 即为所需头文件
```

项目中已包含预生成的头文件，位于 `Common/Ring0/ia32/out/` 目录。

#### 5. Phnt（Windows NT 内核接口定义）

**来源**：https://github.com/winsiderss/phnt

**说明**：Phnt 是 Windows NT 内核未文档化接口的头文件集合，纯头文件库，无需编译。

**使用方法**：项目中已包含所需头文件，位于 `Common/Ring0/SymbolicAccess/Phnt/` 目录。如需更新，可从上游仓库同步最新头文件。

#### 6. SymbolicAccess

**来源**：https://github.com/Air14/SymbolicAccess

**说明**：用于解析 PDB 文件、提取内核结构偏移。项目中已提供预编译的 `SymbolicAccessKM.lib` 和 `SymbolicAccessKM.pdb`。

**编译步骤**：
```powershell
# 克隆 SymbolicAccess 仓库
git clone https://github.com/Air14/SymbolicAccess.git
cd SymbolicAccess

# 使用 CMake 生成 Visual Studio 项目
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release

# 编译内核模式库
cmake --build . --config Release --target SymbolicAccessKM
```

**使用方法**：将编译好的 `SymbolicAccessKM.lib` 复制到 `Common/Ring0/` 目录。

#### 7. VMProtect SDK

**说明**：VMProtect 是商业代码保护工具，**非开源项目**。SDK 需从官方获取：https://vmpsoft.com/

项目中已包含 `VMProtectSDK64.dll` 及相关导入库，仅供开发测试使用。正式发布请遵循 VMProtect 许可协议。

### 恢复 WinUI 编译依赖

首次构建前需恢复 NuGet 包，否则 C++/WinRT 投影头无法生成：

```powershell
powershell -ExecutionPolicy Bypass -File UnrealDbgProNative\RestoreWinUiPackages.ps1
```

脚本会从 NuGet 拉取 Windows App SDK 与 CppWinRT 包。**前端不使用 WebView**。

### 构建项目

> **最简单的办法**：用 VSCode 打开，使用 Trae 插件，跟 Trae 插件说：构建这个项目，然后等着 AI 自己构建就行了

1. 用 Visual Studio 打开 `UnrealDbgPro.sln`
2. 选择 **x64** 平台与 **Release** 配置
3. 构建 `UnrealDbgProNativeWinUI.vcxproj`（以及所需的驱动项目）
4. 构建产物位于 `x64\WinUI\Release\`，运行所需的 DLL/SYS 集中于 `bin` 子目录

> 也可直接运行 `构建并验证GUI.bat` 完成构建与验证。

### 手动部署 DLL

以下三个 DLL 不在解决方案的自动构建流程中，需要手动复制到编译产出目录（`x64\Release\bin\` 或 `x64\WinUI\Release\bin\`）：

| 文件 | 来源 | 说明 |
|------|------|------|
| `AIHelper.dll` | 根目录 | AI 辅助模块 DLL，直接从项目根目录复制到 `bin` 目录 |
| `D-encryption.dll` | `D-encryption` 项目编译产物 | 文件加密模块，源自**大师 UnrealDbg 4.2 泄露版源码**中的 `D-encryption` Delphi 项目。需单独编译 `D-encryption\D-encryption.dproj`，将生成的 `D-encryption.dll` 复制到 `bin` 目录 |
| `VMProtectSDK64.dll` | `Common\VMProtect\` | VMProtect 代码保护 SDK，直接从 `Common\VMProtect\VMProtectSDK64.dll` 复制到 `bin` 目录 |

---

## 运行与使用

### 运行前置条件

1. 安装 **Windows App Runtime 1.8 x64**
2. 以**管理员权限**运行程序
3. 测试证书需导入「本地计算机\受信任的根证书颁发机构」和「受信任的发布者」
4. 隔离虚拟机中可启用 TESTSIGNING（**正式发布不使用此方式**）

### 启动

```bat
:: 方式一：直接运行
x64\WinUI\Release\UnrealDbgProNativeWinUI.exe

:: 方式二：使用批处理
启动GUI.bat
```

程序会自动解析 `bin`、`Config` 目录，并兼容旧版根目录布局，无需手动切换工作目录。

### 主要功能

- **状态概览**：展示系统版本、CPU、内存、架构与驱动状态
- **调试器列表**：注册、删除并启动多个外部调试器（如 x64dbg）
- **TL 选项**：控制 `GetTickCount` 挂钩与目标进程线程阻塞
- **实时日志**：底部面板按级别着色显示（调试 / 信息 / 错误）
- **目标选择**：可选择前台窗口对应的进程作为调试目标

### 自检模式

```bat
UnrealDbgProNativeWinUI.exe --self-test
```

执行纯核心自检：**不加载驱动、不注入 DLL、不改动文件、无需管理员权限**。

---

## 目录结构

```
UnrealDbgPro/
├── UnrealDbgPro.sln               # Visual Studio / WDK 解决方案
├── README.md                      # 项目说明（本文件）
├── LICENSE                        # GNU GPL v3.0 许可证
├── 启动GUI.bat                     # 一键启动前端
├── 构建并验证GUI.bat               # 一键构建并验证
│
├── VT_Driver/                     # ★ VT-x 核心驱动源码
│   ├── Driver.cpp                 #   驱动入口，IRP 分发，设备创建
│   ├── vmm.cpp / vmm.h            #   虚拟机监视器核心，VM-Entry/Exit 循环
│   ├── vmcs.cpp / vmcs.h          #   VMCS 字段配置（Guest/Host 状态）
│   ├── vmexit_handler.cpp         #   VM-Exit 原因分发处理器
│   ├── EPT.cpp / EPT.h            #   扩展页表，内存隐藏/断点
│   ├── vmcall_handler.cpp         #   VMCALL 超调用处理（用户态→Hypervisor 通信）
│   ├── poolmanager.cpp            #   内核池内存管理
│   ├── spinlock.cpp               #   自旋锁同步
│   ├── gdt.cpp                    #   GDT/LDT 处理
│   ├── hypervisor_gateway.cpp     #   Hypervisor 网关接口
│   ├── ASM/                       #   汇编入口（VMX 启动/退出）
│   └── Init/                      #   初始化序列
│
├── DbgkSysWin10/                  # ★ Windows 10 调试子系统桥接驱动
├── DbgkSysWin11/                  # ★ Windows 11 调试子系统桥接驱动
│   ├── Asm/                       #   汇编桩
│   ├── DbgkApi/                   #   Dbgk 系统调用拦截
│   ├── DebugBreak/                #   调试断点处理
│   ├── Encrypt/                   #   内核态 Blowfish 加密
│   ├── Hooks/                     #   内核函数 Hook
│   ├── Hvm/                       #   Hypervisor 接口
│   ├── Init/                      #   初始化
│   ├── List/                      #   链表管理
│   ├── Log/                       #   日志输出（UDBG-UTF8/1 协议）
│   ├── Memory/                    #   内存操作
│   ├── ntos/                      #   NT 内核结构体定义
│   ├── Process/                   #   进程信息
│   └── Protect/                   #   保护对象
│
├── Hook/                          # ★ 注入目标的挂钩 DLL（Hook64.dll）
│   ├── Channels/                  #   前后端通信通道
│   ├── DebugBreak/                #   断点逻辑
│   ├── DebugEvent/                #   调试事件处理
│   ├── HookCallSet/               #   挂钩函数集合
│   ├── Inject/                    #   注入辅助
│   ├── Log/                       #   日志
│   └── vmx/                       #   VMX 交互
│
├── Loader/                        # ★ 驱动加载器（Loader.exe）
├── UnrealDbgProDll/               # ★ 后端接口层（UnrealDbgProDll.dll）
│
├── UnrealDbgProNative/            # ★ 原生 WinUI 3 前端（核心新模块）
│   ├── WinUiMain.cpp              #   界面入口与 XAML 构建
│   ├── WinUiController.cpp/.h     #   后端编排控制器
│   ├── Core.cpp/.h                #   日志/错误解释/路径/权限/诊断
│   ├── LogPanel.cpp/.h            #   底部实时分级日志面板
│   ├── SymbolCache.cpp/.h         #   符号缓存管理
│   ├── UiComponents / UiTheme.h   #   UI 组件与暗黑主题
│   └── RestoreWinUiPackages.ps1   #   NuGet 依赖恢复脚本
│
├── Common/                        # 共享代码库
│   ├── Detours/                   #   Microsoft Detours API Hook
│   ├── TinyXML/                   #   TinyXML XML 解析器
│   ├── jsoncpp/                   #   jsoncpp JSON 解析库
│   ├── Hash/                      #   MD5 / CRC32 哈希算法
│   ├── Encrypt/Blowfish/          #   Blowfish 加密算法
│   ├── IPC/SharedMemory/          #   共享内存 IPC
│   ├── Ring0/                     #   内核态共享代码
│   │   ├── ia32-doc/              #   Intel 指令集 YAML 定义
│   │   ├── SymbolicAccess/        #   PDB 符号解析
│   │   │   ├── Phnt/              #   Windows NT 内核接口定义
│   │   │   └── Pdb/               #   PDB 解析器
│   │   ├── Hvm/                   #   Hypervisor 相关代码
│   │   ├── Inject/                #   APC 注入模块
│   │   └── SymbolicAccessKM.lib   #   符号解析预编译库
│   └── Shared/IOCTLs.h            #   IOCTL 协议定义
│
├── UnrealDbg/                     # 旧版 Delphi 前端源码（已归档）
├── D-encryption/                  # 文件加密工具（Delphi）
├── CardRegistration/              # 卡密注册工具（Delphi）
├── SymbolTool/                    # 符号管理工具（Delphi）
│
├── docs/                          # 项目文档
│   ├── 项目架构与开发指南.md
│   ├── Windows支持与发布策略.md
│   ├── 签名工具Hook原理分析.md
│   └── Git发布清单.md
├── tests/windows/                 # 测试脚本与兼容性矩阵
└── x64/                           # 编译输出目录（构建后生成）
    ├── Debug/  Release/           #   运行目录（bin / Config / Symbols / Log）
    └── WinUI/                     #   WinUI 前端构建输出
```

---

## 运行时目录布局

### x64/Release（主要发布目录）

```
x64/Release/
├── bin/                    # 运行时 DLL/SYS
│   ├── UnrealDbgProDll.dll #   后端 DLL
│   ├── Hook64.dll          #   注入 DLL
│   ├── DbgkSysWin10.sys   #   Win10 桥接驱动
│   ├── DbgkSysWin11.sys   #   Win11 桥接驱动
│   ├── VT_Driver.sys       #   VT 核心驱动
│   ├── VMProtectSDK64.dll  #   VMProtect SDK
│   └── D-encryption.dll    #   加密 DLL
├── Config/                 # 配置文件
│   └── copyright.db        #   版权数据库
├── Certificates/           # 驱动测试证书
├── Symbols/                # PDB/EXP/LIB 调试符号
└── Log/                    # 运行日志
```

### x64/WinUI/Release（WinUI 构建输出）

```
x64/WinUI/Release/
├── UnrealDbgProNativeWinUI.exe  # WinUI 主程序
├── Microsoft.WindowsAppRuntime.Bootstrap.dll
├── bin/                      # 运行时 DLL/SYS
├── Config/                   # 配置
├── Certificates/             # 证书
├── Symbols/                  # 符号
└── Log/                      # 日志
```

---

## IOCTL 通信协议

前端与驱动通过 `DeviceIoControl` 通信，指令定义于 `Common/Shared/IOCTLs.h`：

| IOCTL | 功能 | V1 指令码 | V2 指令码 |
|-------|------|:---------:|:---------:|
| `LOAD_SYMBOLS_TABLE` | 加载符号表 | `0x800` | `0x900` |
| `LOAD_DEBUGGER_STATE` | 加载调试器状态 | `0x801` | `0x901` |
| `LOAD_PROTECT_OBJ_DATA` | 加载保护对象数据 | `0x802` | `0x902` |
| `LOAD_DEBUGGER_DATA` | 加载调试器数据 | `0x803` | `0x903` |
| `CREATE_REMOTE_THREAD` | 创建远程线程 | `0x804` | `0x904` |
| `SET_HARDWARE_BREAKPOINT` | 设置硬件断点 | `0x805` | `0x905` |
| `GET_PROCESS_INFO` | 获取进程信息 | `0x806` | `0x906` |
| `TL_BLOCK_RESUME_THREAD` | 阻塞恢复线程 | `0x807` | `0x907` |
| `DEL_HARDWARE_BREAKPOINT` | 删除硬件断点 | `0x808` | `0x908` |
| `SET_SOFTWARE_BREAKPOINT` | 设置软件断点 | `0x809` | `0x909` |
| `DEL_SOFTWARE_BREAKPOINT` | 删除软件断点 | `0x80A` | `0x90A` |
| `READ_SOFTWARE_BREAKPOINT` | 读取软件断点 | `0x80B` | `0x90B` |

**V2 与 V1 的区别**：V2 指令要求调用方持有具备读写权限的设备句柄（`FILE_READ_DATA | FILE_WRITE_DATA`），提升了越权访问的门槛；V1 保留用于兼容尚未升级的旧客户端。

---

## 驱动事务状态机

驱动加载遵循严格的事务化状态机，保证失败可追溯、可回滚：

```
精确版本检查 → 服务身份/二进制路径检查 → VT_Driver 核心就绪
→ Win10/Win11 桥接驱动就绪 → 打开 \\.\UnrealDbgPro → IOCTL 符号握手 → Ready
```

任一阶段失败均记录：**阶段名、Win32 错误码、系统原文、失败原因、解决建议**。

- 桥接阶段失败时，**仅补偿本次创建的服务**，不影响已有实例
- 若 VT 核心已进入 VMX 状态，程序**不会未经验证地强制卸载**，而是明确提示需要重启恢复，避免系统不稳定

---

## 日志与诊断

### 日志协议

驱动调试输出采用 **`UDBG-UTF8/1`** 行协议，文本按 UTF-8 解释，每条记录包含组件、时间、级别与状态码。内核不再创建或写入 `C:\Logs\driver.xml`；用户态日志统一写入发布目录下的 `Log\UnrealDbgProDll.log`。

### 日志文件

| 文件 | 内容 | 轮转策略 |
|------|------|----------|
| `Log\log.ini` | 前端滚动汇总日志 | 达 16 MB 轮转为 `log.previous.ini`；轮转被占用时继续追加，绝不主动丢失记录 |
| `Log\Sessions\UnrealDbgPro-*.log` | 前端单进程完整会话 | 单文件上限 64 MB；达到上限会写明原因，汇总日志继续记录 |
| `Log\UnrealDbgProDll.log` | 后端滚动汇总日志 | — |
| `Log\Sessions\UnrealDbgProDll-*.log` | 后端独立会话日志 | 单文件上限 64 MB |

### 环境快照

前端会在关键阶段（启动完成、进入 VT 前、符号准备失败后、VT 初始化成功/失败后）记录**只读环境快照**：Windows Build/UBR、管理员状态、VT/Hypervisor/VBS/HVCI/安全启动状态、运行时文件的路径/大小/修改时间/SHA-256，以及 `VT_Driver`、`UnrealDevice` 的服务状态、二进制路径和退出码。

### 诊断包采集

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\windows\Collect-DriverDiagnostics.ps1
```

采集过程**只读**，不会加载驱动或更改任何系统安全设置。诊断包包含：应用日志（默认最近 12 个会话）、运行时文件 SHA-256 与签名状态、服务配置与状态、近七天服务控制管理器与代码完整性事件、系统/CPU/Device Guard 状态及采集清单。单个异常日志超过 64 MB 时仅复制末尾 25000 行，并在包内明确标示。

---

## 安全说明

### 权限模型

- 设备 ACL 仅允许 `SYSTEM` 与内置 `Administrators` 访问
- V2 IOCTL 需显式读写句柄权限（`UNREALDBG_IOCTL_SECURE_ACCESS`）

### 密钥保护

- 后端初始化密钥始终脱敏，**不会写入任何日志或诊断包**

### 签名策略

- **开发测试**：隔离快照虚拟机 + 测试证书（须导入「受信任的根证书颁发机构」与「受信任的发布者」）
- **正式发布**：采用 Microsoft 认可的内核驱动签名流程（硬件开发者中心 Attestation 或 WHQL），并对 SYS / CAT / INF 及发布包做哈希记录
- **不通过**关闭 HVCI、关闭安全启动或永久启用 TESTSIGNING 来规避驱动签名错误（如 577）；上述设置仅可在明确隔离的开发虚拟机中使用

### 测试隔离

所有真实驱动加载测试均在**可回滚的快照虚拟机**中执行；宿主机与 CI 默认只执行预检、自检与符号缓存检查。

---

## 常见问题

<details>
<summary><b>启动时报「权限不足」或驱动加载失败？</b></summary>

请以**管理员身份**运行程序。驱动加载与调试能力均依赖提升权限。
</details>

<details>
<summary><b>提示 <code>ERROR_NOT_SUPPORTED</code>？</b></summary>

当前系统 Build + UBR 不在支持表内。请对照系统要求确认系统版本，或等待对应版本适配。
</details>

<details>
<summary><b>前端提示缺少 Windows App Runtime？</b></summary>

请安装 **Windows App Runtime 1.8 x64** 后重试。
</details>

<details>
<summary><b>构建时报找不到 C++/WinRT 投影头？</b></summary>

先执行 `UnrealDbgProNative\RestoreWinUiPackages.ps1` 恢复 NuGet 依赖，再重新构建。
</details>

<details>
<summary><b>驱动签名错误 577 如何解决？</b></summary>

请使用合法签名流程（测试证书或正式签名）。**不要**通过关闭 HVCI / 安全启动或永久启用 TESTSIGNING 规避，这些做法会削弱系统安全性，仅限隔离开发环境使用。
</details>

---

## 参与贡献

欢迎任何形式的贡献——无论是新功能、Bug 修复、文档改进还是版本适配：

1. Fork 本仓库
2. 创建特性分支（`git checkout -b feature/your-feature`）
3. 提交改动（`git commit -m 'feat: 添加某功能'`）
4. 推送分支（`git push origin feature/your-feature`）
5. 发起 Pull Request

> 提交驱动相关改动时，请附上在隔离虚拟机中的验证结果与日志。

---

## 许可证

本项目基于 **GNU General Public License v3.0** 发布，详见 [LICENSE](LICENSE)。

本项目包含的第三方组件（如 Detours、ia32-doc、Phnt、TinyXML、jsoncpp 等）遵循其各自的许可证，详见对应目录内的许可文件。

---

## 致谢

- **感谢大师**：UnrealDbg 4.2 泄露版源码提供了 VT-x 调试器的核心架构与驱动代码基础
- **感谢花老板**：精易论坛二开版本带来了社区改进思路与部分功能优化
- 感谢 [Detours](https://github.com/microsoft/Detours) 提供的 API Hook 能力
- 感谢 [ia32-doc](https://github.com/wbenny/ia32-doc) 提供的 Intel 指令集与 VMX 结构定义
- 感谢 [Phnt](https://github.com/winsiderss/phnt) 提供的 NT 内核接口定义
- 感谢 [jsoncpp](https://github.com/open-source-parsers/jsoncpp) 提供的 JSON 解析能力
- 感谢 [SymbolicAccess](https://github.com/Air14/SymbolicAccess) 提供的 PDB 符号解析能力
- 感谢所有为本项目提交 Issue 与 Pull Request 的贡献者

---

<div align="center">

**UnrealDbgPro** — 站在巨人的肩膀上，让内核调试更深、更稳、更难被察觉。

</div>
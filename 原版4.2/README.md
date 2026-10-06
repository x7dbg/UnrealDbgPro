# UnrealDbg - 基于Intel VT-x的虚拟化调试器

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-blue.svg)](https://www.microsoft.com/windows)
[![Architecture](https://img.shields.io/badge/arch-x64-blue.svg)]()
[![Language](https://img.shields.io/badge/language-C%2FC%2B%2B%2C%20Delphi-orange.svg)]()

## 项目简介

UnrealDbg 是一个基于 **Intel VT-x (Virtualization Technology)** 技术实现的内核级虚拟化调试器。通过在 CPU 上创建一个轻量级虚拟机监控器 (Hypervisor)，本项目实现了对目标进程的非侵入式调试和监控功能。

与传统的调试器不同，UnrealDbg 不需要修改目标进程的内存或注入 DLL，而是通过硬件虚拟化技术实现对系统的透明监控，具有极高的隐蔽性和强大的反反调试能力。

## 核心特性

### 虚拟化技术
- **Intel VT-x 支持**: 利用 CPU 硬件虚拟化扩展技术创建轻量级 Hypervisor
- **EPT (Extended Page Table) Hook**: 基于扩展页表的内存钩子技术，实现无感知 Hook
- **VMCS (Virtual Machine Control Structure)**: 完整的虚拟机控制结构管理
- **VM Exit 处理**: 完整的虚拟机退出事件处理机制，支持所有类型的 VM Exit

### 调试功能
- **硬件断点**: 
  - INT3 (0xCC) 软件断点
  - INT1 (#DB) 硬件调试断点
  - 支持任意地址设置断点
- **内存断点**: 
  - 基于 EPT 的内存访问监控
  - 支持读/写/执行三种监控类型
  - 支持大页 (2MB) 和 4KB 页监控
- **函数 Hook**: 
  - 支持通过 VMCALL 指令实现的函数钩子
  - 支持代理函数调用原函数
  - 支持取消 Hook 恢复原始状态
- **单步执行**: 支持单步跟踪调试

### 反反调试功能
- **反调试检测**: 检测和对抗常见的反调试技术
- **TL (TimeLock) 对抗**: 
  - 处理 GetTickCount 时间检测
  - 阻止 ResumeThread 反调试
- **调试器隐藏**: 隐藏调试器存在痕迹
- **符号解析**: 支持 PDB 符号文件解析，便于分析

### 安全特性
- **内存保护**: 进程和窗口保护功能
- **卡密验证**: 内置卡密注册系统
- **通信加密**: Blowfish 加密算法保护通信

## 技术架构

### 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                        用户层 (Ring 3)                       │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                  UnrealDbg (Delphi UI)                │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────┐  │  │
│  │  │ 进程管理    │  │ 断点管理    │  │ 反调试对抗    │  │  │
│  │  └─────────────┘  └─────────────┘  └───────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
│                         │ IOCTL / VMCALL                     │
└─────────────────────────┼───────────────────────────────────┘
                          │
┌─────────────────────────┼───────────────────────────────────┐
│                         ▼ 内核层 (Ring 0)                    │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                   VT_Driver (Hypervisor)              │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────┐  │  │
│  │  │ VMM 管理器  │  │ VMCS 管理   │  │ EPT 管理器    │  │  │
│  │  │ (vmm.cpp)   │  │ (vmcs.cpp)  │  │ (EPT.cpp)     │  │  │
│  │  └─────────────┘  └─────────────┘  └───────────────┘  │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────┐  │  │
│  │  │ VM Exit     │  │ VMCALL      │  │ 中断处理      │  │  │
│  │  │ 处理器      │  │ 处理器      │  │ (interrupt)   │  │  │
│  │  └─────────────┘  └─────────────┘  └───────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
│                          │ VM Entry/Exit                     │
└──────────────────────────┼──────────────────────────────────┘
                           │
┌──────────────────────────┼──────────────────────────────────┐
│                          ▼ 硬件层                            │
│              ┌─────────────────────────────┐                 │
│              │      Intel VT-x (VMX)       │                 │
│              │  ┌─────────┐  ┌─────────┐  │                 │
│              │  │ VMXON   │  │ VMCS    │  │                 │
│              │  │ Region  │  │ Region  │  │                 │
│              │  └─────────┘  └─────────┘  │                 │
│              └─────────────────────────────┘                 │
└──────────────────────────────────────────────────────────────┘
```

### 核心组件详解

#### 1. VMM (Virtual Machine Monitor)

**文件**: `VT_Driver/vmm.cpp`, `VT_Driver/vmm.h`

VMM 是 Hypervisor 的核心，负责：
- 为每个逻辑处理器分配 VCPU 上下文
- 初始化 VMX 操作模式
- 管理 VM Entry 和 VM Exit
- 分配和管理 VMM 堆栈

```cpp
// VMM 初始化流程
bool vmm_init() {
    // 1. 检查 CPU 虚拟化支持
    if (!hv::virtualization_support()) return false;
    
    // 2. 为每个逻辑处理器分配 VCPU
    allocate_vmm_context();
    
    // 3. 在每个处理器上启动 VMX
    KeIpiGenericCall(start_vmm, NULL);
    
    return true;
}
```

#### 2. VMCS (Virtual Machine Control Structure)

**文件**: `VT_Driver/vmcs.cpp`, `VT_Driver/vmcs.h`

VMCS 是 Intel VT-x 的核心数据结构，控制虚拟机的运行：

- **Guest State Area**: 保存客户机状态 (寄存器、段选择子等)
- **Host State Area**: 保存宿主机状态
- **VM Execution Control**: 控制 VM 执行行为
- **VM Entry Control**: 控制 VM Entry 行为
- **VM Exit Control**: 控制 VM Exit 行为
- **VM Exit Information**: VM Exit 时提供的信息

```cpp
// VMCS 关键配置
void setup_vmcs(__vcpu* vcpu) {
    // Guest State
    vmwrite(GUEST_CR0, read_effective_guest_cr0());
    vmwrite(GUEST_CR3, __readcr3());
    vmwrite(GUEST_CR4, read_effective_guest_cr4());
    vmwrite(GUEST_RIP, vcpu->guest_rip);
    vmwrite(GUEST_RSP, vcpu->guest_rsp);
    
    // Host State
    vmwrite(HOST_CR0, __readcr0());
    vmwrite(HOST_CR3, __readcr3());
    vmwrite(HOST_CR4, __readcr4());
    vmwrite(HOST_RIP, (unsigned __int64)vmexit_handler);
    
    // Execution Controls
    vmwrite(CPU_BASED_VM_EXEC_CONTROL, adjust_controls(...));
    vmwrite(PIN_BASED_VM_EXEC_CONTROL, adjust_controls(...));
    
    // EPT Configuration
    vmwrite(SECONDARY_VM_EXEC_CONTROL, enable_ept);
    vmwrite(EPT_POINTER, ept_state->ept_pointer->all);
}
```

#### 3. EPT (Extended Page Table)

**文件**: `VT_Driver/EPT.cpp`, `VT_Driver/EPT.h`

EPT 是 Intel VT-x 的二级地址转换机制，用于：

- **内存虚拟化**: 将客户机物理地址转换为宿主机物理地址
- **内存监控**: 通过设置页表权限实现内存访问监控
- **Hook 实现**: 通过修改 EPT 页表实现无痕 Hook

**EPT 页表结构**:
```
┌─────────────┐
│   PML4E     │ ────→ ┌─────────────┐
│  (512 GB)   │       │   PDPTE     │ ────→ ┌─────────────┐
└─────────────┘       │   (1 GB)    │       │    PDE      │ ────→ ┌─────────────┐
                      └─────────────┘       │   (2 MB)    │       │    PTE      │
                                            └─────────────┘       │  (4 KB)     │
                                                                    └─────────────┘
```

**EPT Hook 原理**:
```cpp
// 设置 EPT Hook
bool hook_function(void* target_address, void* proxy_function, void** origin_function) {
    // 1. 找到目标地址对应的 EPT PTE
    __ept_pte* pte = get_pte_for_address(target_address);
    
    // 2. 分配一个新的页面作为 Hook 页面
    void* hook_page = allocate_hook_page(target_address, proxy_function);
    
    // 3. 修改 PTE 指向 Hook 页面
    pte->page_frame_number = PFN(hook_page);
    
    // 4. 刷新 TLB
    invept_single_context(ept_state->ept_pointer->all);
    
    return true;
}
```

#### 4. VM Exit Handler

**文件**: `VT_Driver/vmexit_handler.cpp`, `VT_Driver/vmexit_handler.h`

VM Exit Handler 是处理虚拟机退出事件的核心组件：

```cpp
// VM Exit 处理流程
bool vmexit_handler(guest_context* guest_registers, PFXSAVE64 fxsave) {
    // 1. 获取当前 VCPU
    __vcpu* vcpu = (__vcpu*)_readfsbase_u64();
    
    // 2. 读取 VM Exit 信息
    vcpu->vmexit_info.reason = vmread(VM_EXIT_REASON) & 0xffff;
    vcpu->vmexit_info.qualification = vmread(EXIT_QUALIFICATION);
    vcpu->vmexit_info.guest_rip = vmread(GUEST_RIP);
    
    // 3. 分发到对应的处理器
    switch (vcpu->vmexit_info.reason) {
        case EXIT_REASON_VMCALL:
            vmexit_vmcall_handler(vcpu);
            break;
        case EXIT_REASON_EPT_VIOLATION:
            vmexit_ept_violation_handler(vcpu);
            break;
        case EXIT_REASON_EXCEPTION_NMI:
            vmexit_exception_or_nmi_handler(vcpu);
            break;
        // ... 其他 VM Exit 原因
    }
    
    // 4. 恢复 Guest 执行
    return true;
}
```

**支持的 VM Exit 类型**:
- `EXIT_REASON_VMCALL`: VMCALL 指令
- `EXIT_REASON_EPT_VIOLATION`: EPT 违规访问
- `EXIT_REASON_EXCEPTION_NMI`: 异常和 NMI
- `EXIT_REASON_CPUID`: CPUID 指令
- `EXIT_REASON_MSR_READ/WRITE`: MSR 读写
- `EXIT_REASON_CR_ACCESS`: 控制寄存器访问
- `EXIT_REASON_IO_INSTRUCTION`: IO 指令
- `EXIT_REASON_RDTSC`: RDTSC 指令
- ... 等 60+ 种 VM Exit 类型

#### 5. VMCALL Handler

**文件**: `VT_Driver/vmcall_handler.cpp`, `VT_Driver/vmcall_handler.h`

VMCALL 是客户机与 Hypervisor 通信的接口：

```cpp
// VMCALL 命令处理
void vmexit_vmcall_handler(__vcpu* vcpu) {
    // 验证 VMCALL 标识
    if (vcpu->vmexit_info.guest_registers->rax != VMCALL_IDENTIFIER) {
        inject_interruption(EXCEPTION_VECTOR_UNDEFINED_OPCODE, ...);
        return;
    }
    
    // 获取命令和参数
    unsigned __int64 vmcall_reason = vcpu->vmexit_info.guest_registers->rcx;
    
    switch (vmcall_reason) {
        case VMCALL_EPT_HOOK_FUNCTION:
            // 设置 EPT Hook
            ept::vmcall_hook_function(...);
            break;
        case VMCALL_EPT_UNHOOK_FUNCTION:
            // 取消 EPT Hook
            ept::unhook_function(...);
            break;
        case VMCALL_WATCH_WRITES:
            // 设置内存写监控
            setup_memory_watch(...);
            break;
        // ... 其他命令
    }
}
```

### 数据流图

```
┌─────────────────────────────────────────────────────────────────────┐
│                           用户操作                                   │
│  [设置断点] [单步执行] [查看内存] [设置 Hook] [反调试对抗]            │
└───────────────────────┬─────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      UnrealDbg UI (Delphi)                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │
│  │ 进程列表 │  │ 断点管理 │  │ 内存查看 │  │ 反调试配置       │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘    │
└───────────────────────┬─────────────────────────────────────────────┘
                        │ DeviceIoControl
                        ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      VT_Driver (Kernel)                             │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                    VM Exit Handler                           │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐ │  │
│  │  │ VMCALL   │  │ EPT      │  │ Exception│  │ MSR/CPUID    │ │  │
│  │  │ Handler  │  │ Handler  │  │ Handler  │  │ Handler      │ │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────────┘ │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                    EPT Manager                               │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐ │  │
│  │  │ PML4     │  │ PDPTE    │  │ PDE      │  │ PTE          │ │  │
│  │  │ (512GB)  │  │ (1GB)    │  │ (2MB)    │  │ (4KB)        │ │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────────┘ │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                        │
                        ▼ VM Entry/Exit
┌─────────────────────────────────────────────────────────────────────┐
│                         Hardware (VT-x)                             │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  VMXON Region  │  VMCS Region  │  EPT Tables  │  MSR Bitmap  │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

## 项目结构

```
UnrealDbg/
├── VT_Driver/                    # 内核驱动 (Hypervisor)
│   ├── ASM/                      # 汇编代码
│   │   ├── AsmCallset.asm        # 汇编调用集合
│   │   ├── exception-routines.asm# 异常处理例程
│   │   ├── interrupt-handlers.asm# 中断处理程序
│   │   ├── lde64.asm             # 长度反汇编引擎
│   │   ├── vm-exit.asm           # VM Exit 入口汇编
│   │   ├── vm-launch.asm         # VM Launch 汇编
│   │   └── vm_context.asm        # VM 上下文管理
│   │
│   ├── Init/                     # 初始化代码
│   │   └── Symbolic/
│   │       ├── InitNtoskrnl.cpp  # ntoskrnl 符号初始化
│   │       └── InitNtoskrnl.h
│   │
│   ├── Driver.cpp                # 驱动入口 (DriverEntry)
│   ├── Driver.h                  # 驱动头文件
│   ├── vmm.cpp/h                 # 虚拟机管理器核心
│   ├── vmcs.cpp/h                # VMCS 管理
│   ├── EPT.cpp/h                 # 扩展页表管理
│   ├── vmexit_handler.cpp/h      # VM Exit 处理器
│   ├── vmcall_handler.cpp/h      # VMCALL 处理器
│   ├── hypervisor_routines.cpp/h # Hypervisor 辅助例程
│   ├── hypervisor_gateway.cpp/h  # Hypervisor 网关接口
│   ├── poolmanager.cpp/h         # 内存池管理器
│   ├── gdt.cpp/h                 # GDT 管理
│   ├── idt.cpp/h                 # IDT 管理
│   ├── spinlock.cpp/h            # 自旋锁实现
│   ├── invalid_ept.cpp/h         # EPT 违规处理
│   ├── invalid_pcid.cpp/h        # PCID 无效处理
│   ├── invalid_vpid.cpp/h        # VPID 无效处理
│   ├── Globals.cpp/h             # 全局变量定义
│   ├── vm_context.h              # VM 上下文结构
│   ├── vmcall_reason.h           # VMCALL 命令定义
│   ├── AllocateMem.h             # 内存分配
│   ├── segment.h                 # 段描述符定义
│   ├── exception.h               # 异常定义
│   ├── interrupt.h               # 中断定义
│   ├── cpuid.h                   # CPUID 定义
│   ├── crx.h                     # 控制寄存器定义
│   ├── drx.h                     # 调试寄存器定义
│   ├── msr.h                     # MSR 定义
│   ├── mtrr.h                    # MTRR 定义
│   ├── vmx.h                     # VMX 定义
│   └── ntapi.h                   # NT API 定义
│
├── UnrealDbg/                    # 用户界面 (Delphi)
│   ├── Forms/
│   │   └── Main.pas              # 主界面
│   ├── ExternalCall/
│   │   ├── UnrealDbgDll.pas      # 调试器 DLL 接口
│   │   └── D_encryptionDll.pas   # 加密 DLL 接口
│   ├── Handler/
│   │   └── HandlerTLDetection.pas# TL 检测处理器
│   ├── Threads/
│   │   ├── EventHandlerThread.pas# 事件处理线程
│   │   └── LockThread.pas        # 锁定线程
│   ├── KernelApi/
│   │   ├── Kernel32Api.pas       # Kernel32 API
│   │   └── KernelBaseApi.pas     # KernelBase API
│   ├── LogSystem/
│   │   └── Log.pas               # 日志系统
│   ├── Common/
│   │   ├── VMProtectSDK.pas      # VMProtect SDK
│   │   ├── Grobal.pas            # 全局定义
│   │   ├── GList.pas             # 泛型列表
│   │   └── ExtTQueue.pas         # 扩展队列
│   └── Globals/
│       └── GlobalVar.pas         # 全局变量
│
├── Common/                       # 共享代码
│   ├── Shared/
│   │   ├── SharedStruct.h        # 共享结构体 (Ring0/Ring3)
│   │   └── IOCTLs.h              # IOCTL 定义
│   ├── Detours/                  # Microsoft Detours Hook 库
│   │   ├── Hook.cpp/h
│   │   └── detours.h
│   ├── Encrypt/                  # 加密模块
│   │   └── Blowfish/
│   ├── FileSystem/               # 文件系统工具
│   ├── Hash/                     # 哈希算法
│   │   ├── MD5/
│   │   └── Crc32.cpp/h
│   ├── IPC/                      # 进程间通信
│   │   └── SharedMemory/
│   ├── Logger/                   # 日志系统
│   └── Ring0/                    # 内核层公共代码
│       ├── SymbolicAccess/       # 符号访问库
│       │   ├── Pdb/              # PDB 解析
│       │   ├── Phnt/             # Process Hacker NT 头
│       │   ├── Utils/            # 工具函数
│       │   └── ModuleExtender/   # 模块扩展
│       ├── Hvm/                  # Hypervisor 网关
│       ├── ia32-doc/             # Intel IA-32 文档生成
│       ├── List/                 # 链表实现
│       ├── Memory/               # 内存管理
│       ├── String/               # 字符串处理
│       ├── Spinlock/             # 自旋锁
│       ├── PE/                   # PE 结构
│       ├── Inject/               # 注入模块
│       └── Encrypt/              # 内核加密
│
├── CardRegistration/             # 卡密注册系统 (Delphi)
│   └── Forms/
│       └── Main.pas
│
├── D-encryption/                 # 加密工具 (Delphi)
│   ├── Forms/
│   │   └── Main.pas
│   └── ExternalCall/
│       └── D_encryptionDll.pas
│
├── SymbolTool/                   # 符号处理工具 (Delphi)
│   └── Forms/
│       └── Main.pas
│
├── UnrealDbg.sln                 # Visual Studio 解决方案
└── README.md                     # 项目说明
```

## 系统要求

### 硬件要求
| 组件 | 最低要求 | 推荐配置 |
|------|---------|---------|
| CPU | 支持 Intel VT-x 的 64 位处理器 | Intel Core i5/i7/i9 或 Xeon |
| 内存 | 4 GB | 8 GB 或更多 |
| 硬盘 | 100 MB 可用空间 | SSD |
| 系统 | 64 位 UEFI/BIOS | UEFI with Secure Boot disabled |

### 软件要求
- **操作系统**: Windows 7/8/10/11 (x64)
- **开发环境**:
  - Visual Studio 2019 或更高版本 (用于编译驱动)
  - Windows Driver Kit (WDK) 10 或更高版本
  - Delphi 10.3 Rio 或更高版本 (用于编译 UI)
- **驱动签名**: 需要禁用驱动签名强制或启用测试模式

### CPU 兼容性
支持 Intel VT-x 的 CPU 系列：
- Intel Core 2 Duo/Quad (部分型号)
- Intel Core i3/i5/i7/i9 (第一代及以后)
- Intel Xeon (5500 系列及以后)
- Intel Atom (部分型号)

**注意**: AMD 处理器不支持 (使用 AMD-V 不同技术)

## 编译说明

### 环境准备

#### 1. 安装 Visual Studio
```powershell
# 安装 Visual Studio 2019/2022
# 工作负载选择:
# - 使用 C++ 的桌面开发
# - 通用 Windows 平台开发
```

#### 2. 安装 Windows Driver Kit (WDK)
```powershell
# 下载地址: https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk
# 安装 WDK 和 WDK Visual Studio 扩展
```

#### 3. 安装 Delphi
```powershell
# 安装 RAD Studio 10.3 Rio 或更高版本
# 确保安装了 Windows 64-bit 编译器
```

### 编译驱动 (VT_Driver)

#### 方法一: Visual Studio GUI
```
1. 打开 UnrealDbg.sln
2. 选择配置: Release
3. 选择平台: x64
4. 右键点击 VT_Driver 项目 → 生成
5. 输出文件: VT_Driver/x64/Release/VT_Driver.sys
```

#### 方法二: 命令行
```powershell
# 打开 "Developer Command Prompt for VS 2019"
cd e:\Projects\Driver\虚幻调试器\UnrealDbg

# 编译
msbuild UnrealDbg.sln /p:Configuration=Release /p:Platform=x64 /t:VT_Driver

# 输出
# VT_Driver\x64\Release\VT_Driver.sys
```

### 编译用户界面 (UnrealDbg)

```powershell
# 使用 Delphi 命令行编译器
cd UnrealDbg

# 编译 64 位版本
dcc64 UnrealDbg.dpr -B -E.\Release

# 或使用 Delphi IDE
# 1. 打开 UnrealDbg/UnrealDbg.dpr
# 2. 选择目标平台: Windows 64-bit
# 3. 编译 (Ctrl+F9)
```

### 驱动签名 (测试模式)

#### 方法一: 测试签名模式 (推荐用于开发)
```powershell
# 以管理员权限运行
bcdedit /set testsigning on
bcdedit /set nointegritychecks on

# 重启系统
shutdown /r /t 0
```

#### 方法二: 禁用驱动签名强制 (一次性)
```powershell
# 设置启动选项
bcdedit /set {current} bootmenupolicy legacy

# 重启时按 F8，选择 "禁用驱动程序签名强制"
```

## 使用方法

### 快速开始

#### 1. 启动驱动
```powershell
# 以管理员权限运行 PowerShell

# 创建服务
sc create VT_Driver type= kernel binPath= "C:\Path\To\VT_Driver.sys"

# 启动服务
sc start VT_Driver

# 检查状态
sc query VT_Driver
```

#### 2. 启动调试器
```powershell
# 运行用户界面程序
UnrealDbg.exe
```

#### 3. 基本操作
```
1. 点击 "进入VT调试模式" 初始化 Hypervisor
2. 在进程列表中选择目标进程
3. 右键点击进程，选择 "启动调试器"
4. 使用功能:
   - 设置断点 (INT3/INT1/EPT)
   - 内存监控 (读/写/执行)
   - 函数 Hook
   - 反调试对抗
```

### 编程接口

#### VMCALL 接口

VMCALL 是用户层与 Hypervisor 通信的主要接口：

```cpp
// VMCALL 命令枚举
enum vm_call_reasons {
    VMCALL_TEST = 0,                    // 测试 VMCALL
    VMCALL_VMXOFF,                      // 关闭 VMX
    VMCALL_EPT_CC_HOOK,                 // EPT CC Hook (INT3)
    VMCALL_EPT_INT1_HOOK,               // EPT INT1 Hook (#DB)
    VMCALL_EPT_RIP_HOOK,                // EPT RIP Hook
    VMCALL_EPT_HOOK_FUNCTION,           // 函数 Hook
    VMCALL_EPT_UNHOOK_FUNCTION,         // 取消函数 Hook
    VMCALL_INVEPT_CONTEXT,              // 刷新 EPT TLB
    VMCALL_DUMP_POOL_MANAGER,           // 转储内存池状态
    VMCALL_DUMP_VMCS_STATE,             // 转储 VMCS 状态
    VMCALL_HIDE_HV_PRESENCE,            // 隐藏 Hypervisor 存在
    VMCALL_UNHIDE_HV_PRESENCE,          // 显示 Hypervisor 存在
    VMCALL_HIDE_SOFTWARE_BREAKPOINT,    // 隐藏软件断点
    VMCALL_READ_SOFTWARE_BREAKPOINT,    // 读取软件断点
    VMCALL_READ_EPT_FAKE_PAGE_MEMORY,   // 读取 EPT 假页内存
    VMCALL_WATCH_WRITES,                // 监控内存写
    VMCALL_WATCH_READS,                 // 监控内存读
    VMCALL_WATCH_EXECUTES,              // 监控内存执行
    VMCALL_WATCH_DELETE,                // 删除内存监控
    VMCALL_GET_BREAKPOINT,              // 获取断点信息
    VMCALL_INIT_OFFSET,                 // 初始化偏移
};
```

#### 使用 VMCALL

```cpp
// 执行 VMCALL
bool vmcall(unsigned __int64 reason, unsigned __int64 param1 = 0, 
            unsigned __int64 param2 = 0, unsigned __int64 param3 = 0) {
    // 使用内联汇编或 intrinsic
    __asm {
        mov rax, VMCALL_IDENTIFIER  // 0xBF5587567C4C830F
        mov rcx, reason
        mov rdx, param1
        mov r8, param2
        mov r9, param3
        vmcall
    }
    return true;
}

// 示例: 设置内存写监控
void setup_write_watch(void* address, size_t size) {
    VT_BREAK_POINT bp = {};
    bp.command = VMCALL_WATCH_WRITES;
    bp.VirtualAddress = (unsigned __int64)address;
    bp.Size = size;
    bp.cr3 = get_current_cr3();
    
    vmcall(VMCALL_WATCH_WRITES, (unsigned __int64)&bp);
}
```

#### IOCTL 接口

```cpp
// 打开设备句柄
HANDLE hDevice = CreateFile(
    L"\\\\.\\UnrealDbg",
    GENERIC_READ | GENERIC_WRITE,
    0,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
);

// 发送 IOCTL
DWORD bytesReturned;
DeviceIoControl(
    hDevice,
    IOCTL_POOL_MANAGER_ALLOCATE,  // 或其他 IOCTL
    inputBuffer,
    inputSize,
    outputBuffer,
    outputSize,
    &bytesReturned,
    NULL
);
```

### 高级功能

#### 1. 设置 EPT Hook

```cpp
// 定义原函数类型
typedef int (__fastcall *OriginalFunction)(int a, int b);
OriginalFunction g_OriginalFunction = nullptr;

// 代理函数
int __fastcall ProxyFunction(int a, int b) {
    // 在调用原函数前执行自定义逻辑
    printf("Hook called with: %d, %d\n", a, b);
    
    // 调用原函数
    return g_OriginalFunction(a, b);
}

// 设置 Hook
void setup_hook() {
    void* target = GetProcAddress(GetModuleHandleA("kernel32.dll"), "TargetFunction");
    
    VT_BREAK_POINT bp = {};
    bp.command = VMCALL_EPT_HOOK_FUNCTION;
    bp.VirtualAddress = (unsigned __int64)target;
    bp.LoopUserMode = (unsigned __int64)ProxyFunction;
    bp.cr3 = get_current_cr3();
    
    vmcall(VMCALL_EPT_HOOK_FUNCTION, (unsigned __int64)&bp, (unsigned __int64)&g_OriginalFunction);
}
```

#### 2. 内存监控

```cpp
// 监控内存区域的所有访问
void monitor_memory(void* address, size_t size) {
    // 设置写监控
    VT_BREAK_POINT writeWatch = {};
    writeWatch.command = VMCALL_WATCH_WRITES;
    writeWatch.VirtualAddress = (unsigned __int64)address;
    writeWatch.Size = size;
    writeWatch.cr3 = get_current_cr3();
    vmcall(VMCALL_WATCH_WRITES, (unsigned __int64)&writeWatch);
    
    // 设置读监控
    VT_BREAK_POINT readWatch = {};
    readWatch.command = VMCALL_WATCH_READS;
    readWatch.VirtualAddress = (unsigned __int64)address;
    readWatch.Size = size;
    readWatch.cr3 = get_current_cr3();
    vmcall(VMCALL_WATCH_READS, (unsigned __int64)&readWatch);
    
    // 设置执行监控
    VT_BREAK_POINT execWatch = {};
    execWatch.command = VMCALL_WATCH_EXECUTES;
    execWatch.VirtualAddress = (unsigned __int64)address;
    execWatch.Size = size;
    execWatch.cr3 = get_current_cr3();
    vmcall(VMCALL_WATCH_EXECUTES, (unsigned __int64)&execWatch);
}
```

#### 3. 反调试对抗配置

```cpp
// 启用 TL (TimeLock) 对抗
void enable_tl_confrontation() {
    // 处理 GetTickCount 时间检测
    // 阻止 ResumeThread 反调试
    // 其他反调试对抗措施
}
```

## 技术原理详解

### Intel VT-x 简介

Intel VT-x (Virtualization Technology for x86) 是 Intel 处理器提供的硬件虚拟化技术，主要包括：

#### VMX 操作模式
- **VMX Root Operation**: Hypervisor 运行的模式
- **VMX Non-Root Operation**: 客户机运行的模式

#### 关键组件
1. **VMXON Region**: VMX 操作的内存区域
2. **VMCS (Virtual Machine Control Structure)**: 虚拟机控制结构
3. **EPT (Extended Page Table)**: 扩展页表
4. **VPID (Virtual Processor ID)**: 虚拟处理器标识

### VM Entry / VM Exit 流程

```
┌─────────────────────────────────────────────────────────────┐
│                      VM Entry 流程                          │
├─────────────────────────────────────────────────────────────┤
│  1. 检查 VMCS 有效性                                        │
│  2. 加载 Guest State (寄存器、段选择子等)                   │
│  3. 加载 MSR (根据 VM Entry Control)                        │
│  4. 设置 VMX Preemption Timer (如果启用)                    │
│  5. 注入事件 (如果配置了 VM Entry Interruption)             │
│  6. 执行 Guest 代码                                         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌─────────────────┐
                    │   Guest 执行    │
                    └─────────────────┘
                              │
                              ▼ 触发 VM Exit 条件
┌─────────────────────────────────────────────────────────────┐
│                      VM Exit 流程                           │
├─────────────────────────────────────────────────────────────┤
│  1. 保存 Guest State 到 VMCS                                │
│  2. 保存 MSR (根据 VM Exit Control)                         │
│  3. 加载 Host State (寄存器、段选择子等)                    │
│  4. 记录 VM Exit 原因和相关信息                             │
│  5. 跳转到 Host RIP (vmexit_handler)                        │
└─────────────────────────────────────────────────────────────┘
```

### EPT (Extended Page Table) 详解

#### 地址转换流程

```
Guest Virtual Address (GVA)
           │
           ▼ CR3 (Guest)
Guest Physical Address (GPA)
           │
           ▼ EPT (Extended Page Table)
Host Physical Address (HPA)
```

#### EPT 页表结构

```
┌─────────────────────────────────────────────────────────────┐
│                    EPT 4-Level Paging                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  EPT PML4E (Page Map Level 4 Entry)                         │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Bits 47:39 │  Index into 512GB region               │   │
│  │  Bits 51:12 │  Physical address of EPT PDPTE table   │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                  │
│                          ▼                                  │
│  EPT PDPTE (Page Directory Pointer Table Entry)             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Bits 38:30 │  Index into 1GB region                   │   │
│  │  Bits 51:12 │  Physical address of EPT PDE table       │   │
│  │  Bit 7 (PS) │  Page Size (1 = 1GB page)                │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                  │
│                          ▼                                  │
│  EPT PDE (Page Directory Entry)                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Bits 29:21 │  Index into 2MB region                   │   │
│  │  Bits 51:12 │  Physical address of EPT PTE table       │   │
│  │  Bit 7 (PS) │  Page Size (1 = 2MB page)                │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                  │
│                          ▼                                  │
│  EPT PTE (Page Table Entry)                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Bits 20:12 │  Index into 4KB page                     │   │
│  │  Bits 51:12 │  Physical address of 4KB page            │   │
│  │  Bit 0 (R)  │  Read access                             │   │
│  │  Bit 1 (W)  │  Write access                            │   │
│  │  Bit 2 (X)  │  Execute access                          │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

#### EPT Hook 实现原理

```
正常执行流程:
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│   Guest     │ ───→ │  EPT 页表   │ ───→ │  原始页面   │
│  虚拟地址   │      │  (原始映射)  │      │  (物理页)   │
└─────────────┘      └─────────────┘      └─────────────┘

Hook 后执行流程:
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│   Guest     │ ───→ │  EPT 页表   │ ───→ │  Hook 页面  │
│  虚拟地址   │      │  (修改映射)  │      │  (物理页)   │
└─────────────┘      └─────────────┘      └─────────────┘
                                                  │
                                                  ▼
                                           ┌─────────────┐
                                           │  代理函数   │
                                           └─────────────┘
```

### 断点实现原理

#### INT3 (0xCC) 断点
```cpp
// 在目标地址写入 0xCC (INT3 指令)
// 当执行到该地址时触发 #BP 异常
// Hypervisor 捕获该异常并处理
```

#### INT1 (#DB) 断点
```cpp
// 使用 DR0-DR3 调试寄存器设置断点地址
// 使用 DR7 设置断点类型和长度
// 当访问匹配地址时触发 #DB 异常
```

#### EPT 断点
```cpp
// 通过修改 EPT 页表权限实现
// 设置页面为不可执行/不可读/不可写
// 访问时触发 EPT Violation VM Exit
```

## 常见问题 (FAQ)

### Q: 系统蓝屏 (BSOD) 怎么办？
**A**: 
1. 检查 CPU 是否支持 VT-x
2. 确保 BIOS 中启用了虚拟化技术
3. 检查驱动是否正确签名或启用了测试模式
4. 查看蓝屏代码和转储文件分析原因

### Q: 为什么 AMD CPU 不支持？
**A**: AMD 使用 AMD-V 技术，与 Intel VT-x 指令集不同。本项目专门针对 Intel VT-x 实现。

### Q: 如何隐藏 Hypervisor 存在？
**A**: 使用 `VMCALL_HIDE_HV_PRESENCE` 命令可以隐藏 CPUID 等特征。

### Q: 支持 Windows 11 吗？
**A**: 支持，但可能需要禁用 Secure Boot 和 Memory Integrity。

### Q: 如何调试驱动代码？
**A**: 
1. 使用 WinDbg + 虚拟机 (VMware/VirtualBox)
2. 配置双机调试
3. 设置断点在 `DriverEntry` 或 `vmexit_handler`

## 注意事项

⚠️ **重要警告**

1. **系统稳定性**: 本项目涉及内核级编程，操作不当可能导致系统崩溃 (BSOD)。请在虚拟机或测试环境中使用。

2. **数据安全**: 使用前请备份重要数据。

3. **驱动签名**: 需要禁用驱动签名强制或启用测试模式才能加载未签名驱动。

4. **法律合规**: 请遵守当地法律法规，仅用于合法的安全研究和学习目的。

5. **兼容性**: 
   - 不支持 AMD 处理器
   - 可能与某些安全软件冲突
   - 某些反作弊系统可能检测到 Hypervisor

## 调试技巧

### 使用 WinDbg 调试

```
# 设置符号路径
.sympath SRV*C:\Symbols*http://msdl.microsoft.com/download/symbols

# 加载驱动符号
.reload /f VT_Driver.sys

# 设置断点
bp VT_Driver!DriverEntry
bp VT_Driver!vmexit_handler
bp VT_Driver!vmexit_ept_violation_handler

# 查看 VMCS
dt VT_Driver!__vcpu

# 查看 EPT 状态
dt VT_Driver!__ept_state
```

### 日志输出

驱动使用 `KdPrint` 输出调试信息，可以使用 DbgView 查看：

```cpp
// 在驱动代码中
KdPrint(("[VT_Driver] VM Exit reason: %d\n", reason));
```

## 性能优化

### 减少 VM Exit 开销
1. 使用 MSR Bitmap 减少 MSR 访问导致的 VM Exit
2. 使用 IO Bitmap 控制 IO 指令的 VM Exit
3. 合理设置 EPT 权限，避免不必要的 EPT Violation

### 内存优化
1. 使用 2MB 大页减少 EPT 页表级数
2. 合理管理内存池，避免频繁分配/释放

## 二次开发与维护指南

### 目录
- [开发环境配置](#开发环境配置)
- [代码结构说明](#代码结构说明)
- [添加新的 VM Exit 处理器](#添加新的-vm-exit-处理器)
- [添加新的 VMCALL 命令](#添加新的-vmcall-命令)
- [添加新的 EPT Hook 类型](#添加新的-ept-hook-类型)
- [调试和测试](#调试和测试)
- [常见问题排查](#常见问题排查)
- [版本发布流程](#版本发布流程)

---

### 开发环境配置

#### 推荐开发环境
```
操作系统: Windows 10/11 Pro x64
IDE: Visual Studio 2022 Community
WDK: Windows Driver Kit 10 (最新版)
Delphi: RAD Studio 11 Alexandria
虚拟机: VMware Workstation Pro 17
```

#### 虚拟机配置（用于调试）
```
1. 创建 Windows 10/11 虚拟机
2. 分配至少 4GB 内存
3. 启用虚拟化引擎：
   - 处理器 → 虚拟化 Intel VT-x/EPT
4. 配置串口用于双机调试：
   - 添加串口 → 使用命名管道
   - 管道名称: \\.\pipe\debug_pipe
   - 此端是服务器/另一端是应用程序
```

#### 符号配置
创建符号服务器配置文件 `C:\Symbols\symsrv.ini`:
```ini
[SymbolServer]
SRV*C:\Symbols*http://msdl.microsoft.com/download/symbols
SRV*C:\Symbols*https://chromium-browser-symsrv.commondatastorage.googleapis.com
```

---

### 代码结构说明

#### 核心模块依赖关系
```
Driver.cpp (入口)
    ├── vmm.cpp (VMM 初始化)
    │       ├── vmcs.cpp (VMCS 配置)
    │       ├── EPT.cpp (EPT 初始化)
    │       └── ASM/vm-launch.asm (VM 启动)
    │
    ├── vmexit_handler.cpp (VM Exit 处理)
    │       ├── vmcall_handler.cpp (VMCALL)
    │       ├── invalid_ept.cpp (EPT 违规)
    │       ├── interrupt.cpp (异常处理)
    │       └── ASM/vm-exit.asm (VM Exit 入口)
    │
    ├── hypervisor_routines.cpp (辅助函数)
    │       └── hypervisor_gateway.cpp (网关接口)
    │
    └── poolmanager.cpp (内存管理)
```

#### 关键数据结构

##### 1. VCPU 上下文 (`vm_context.h`)
```cpp
typedef struct ___vcpu
{
    // VMCS 区域
    __vmcs* vmcs;
    
    // EPT 状态
    __ept_state* ept_state;
    
    // VMM 堆栈
    void* vmm_stack;
    
    // VM Exit 信息
    __vmexit_info vmexit_info;
    
    // 虚拟化状态
    struct {
        bool vmm_launched;
        bool vmx_off_executed;
    } vcpu_status;
    
    // 位图
    struct {
        void* msr_bitmap;
        void* io_bitmap_a;
        void* io_bitmap_b;
    } vcpu_bitmaps;
    
    // ... 其他字段
} __vcpu;
```

##### 2. EPT 状态 (`EPT.h`)
```cpp
typedef struct ___ept_state
{
    // EPT 指针
    __ept_pointer* ept_pointer;
    
    // 页表
    __ept_pml4e* ept_page_table;
    
    // Hook 页面列表
    LIST_ENTRY hooked_pages_list;
    
    // 自旋锁
    spinlock_t lock;
    
    // ... 其他字段
} __ept_state;
```

##### 3. VM Exit 信息 (`vmexit_handler.h`)
```cpp
typedef struct ___vmexit_info
{
    unsigned __int64 reason;           // 退出原因
    unsigned __int64 qualification;    // 退出资格
    unsigned __int64 guest_rip;        // Guest RIP
    unsigned __int64 guest_rsp;        // Guest RSP
    unsigned __int64 instruction_length;
    __rflags guest_rflags;
    guest_context* guest_registers;
    PFXSAVE64 fxsave;
} __vmexit_info;
```

---

### 添加新的 VM Exit 处理器

#### 步骤 1: 定义退出原因（如果尚未定义）
在 `vmexit_handler.h` 中添加：
```cpp
enum __vm_exit_reason
{
    // ... 已有定义
    EXIT_REASON_YOUR_NEW_REASON = 0xXX,  // 添加新的退出原因
};
```

#### 步骤 2: 声明处理函数
在 `vmexit_handler.h` 中添加：
```cpp
void vmexit_your_handler(__vcpu* vcpu);
```

#### 步骤 3: 实现处理函数
在 `vmexit_handler.cpp` 中添加：
```cpp
void vmexit_your_handler(__vcpu* vcpu)
{
    // 1. 获取必要信息
    unsigned __int64 qualification = vcpu->vmexit_info.qualification;
    
    // 2. 处理逻辑
    // ... 你的处理代码
    
    // 3. 调整 RIP（如果需要）
    adjust_rip(vcpu);
}
```

#### 步骤 4: 注册到分发器
在 `dispatch_vm_exit` 函数中添加：
```cpp
void dispatch_vm_exit(__vcpu* vcpu)
{
    switch (vcpu->vmexit_info.reason)
    {
        // ... 已有 case
        case EXIT_REASON_YOUR_NEW_REASON:
            vmexit_your_handler(vcpu);
            break;
        // ...
    }
}
```

#### 示例：添加 RDTSCP 指令处理
```cpp
// 1. 定义退出原因（已存在 EXIT_REASON_RDTSCP）

// 2. 声明处理函数 (vmexit_handler.h)
void vmexit_rdtscp_handler(__vcpu* vcpu);

// 3. 实现处理函数 (vmexit_handler.cpp)
void vmexit_rdtscp_handler(__vcpu* vcpu)
{
    // 读取 TSC
    unsigned __int64 tsc = __rdtsc();
    unsigned __int64 tsc_aux = 0;
    
    // 读取 TSC_AUX MSR
    tsc_aux = __readmsr(IA32_TSC_AUX);
    
    // 写入 Guest 寄存器
    vcpu->vmexit_info.guest_registers->rax = tsc & 0xFFFFFFFF;
    vcpu->vmexit_info.guest_registers->rdx = tsc >> 32;
    vcpu->vmexit_info.guest_registers->rcx = tsc_aux;
    
    // 调整 RIP
    adjust_rip(vcpu);
}

// 4. 注册到分发器
case EXIT_REASON_RDTSCP:
    vmexit_rdtscp_handler(vcpu);
    break;
```

---

### 添加新的 VMCALL 命令

#### 步骤 1: 定义命令 ID
在 `vmcall_reason.h` 中添加：
```cpp
enum vm_call_reasons
{
    // ... 已有命令
    VMCALL_YOUR_NEW_COMMAND,
};
```

#### 步骤 2: 定义参数结构（如果需要）
在 `SharedStruct.h` 中添加：
```cpp
typedef struct _YOUR_COMMAND_DATA {
    unsigned __int64 command;  // 必须是第一个字段
    // 添加你的参数字段
    unsigned __int64 param1;
    unsigned __int64 param2;
    // ...
} YOUR_COMMAND_DATA, * PYOUR_COMMAND_DATA;
```

#### 步骤 3: 实现处理逻辑
在 `vmcall_handler.cpp` 中添加：
```cpp
case VMCALL_YOUR_NEW_COMMAND:
{
    // 1. 读取参数
    PYOUR_COMMAND_DATA data = (PYOUR_COMMAND_DATA)vmcall_parameter1;
    
    // 2. 验证地址
    if (!hv::is_valid_guest_address(data))
    {
        status = false;
        break;
    }
    
    // 3. 读取数据到本地缓冲区
    YOUR_COMMAND_DATA local_data;
    if (!hv::read_guest_virtual_memory(data, &local_data, sizeof(local_data)))
    {
        status = false;
        break;
    }
    
    // 4. 执行命令逻辑
    status = your_command_handler(&local_data);
    
    // 5. 写回结果（如果需要）
    if (status)
    {
        hv::write_guest_virtual_memory(data, &local_data, sizeof(local_data));
    }
    break;
}
```

#### 步骤 4: 添加用户层接口
在 `hypervisor_gateway.h` 中添加：
```cpp
namespace hvgt
{
    // ... 已有接口
    bool your_new_command(YOUR_COMMAND_DATA* data);
}
```

在 `hypervisor_gateway.cpp` 中实现：
```cpp
bool your_new_command(YOUR_COMMAND_DATA* data)
{
    data->command = VMCALL_YOUR_NEW_COMMAND;
    return vmcall(data, sizeof(YOUR_COMMAND_DATA));
}
```

#### 示例：添加内存读取命令
```cpp
// 1. 定义命令 (vmcall_reason.h)
VMCALL_READ_GUEST_MEMORY,

// 2. 定义结构 (SharedStruct.h)
typedef struct _READ_MEMORY_DATA {
    unsigned __int64 command;
    unsigned __int64 guest_address;
    unsigned __int64 buffer;
    unsigned __int64 size;
    unsigned __int64 bytes_read;
} READ_MEMORY_DATA, * PREAD_MEMORY_DATA;

// 3. 实现处理 (vmcall_handler.cpp)
case VMCALL_READ_GUEST_MEMORY:
{
    PREAD_MEMORY_DATA data = (PREAD_MEMORY_DATA)vmcall_parameter1;
    READ_MEMORY_DATA local_data;
    
    if (!hv::read_guest_virtual_memory(data, &local_data, sizeof(local_data)))
    {
        status = false;
        break;
    }
    
    // 执行读取
    local_data.bytes_read = hv::read_guest_virtual_memory(
        (void*)local_data.guest_address,
        (void*)local_data.buffer,
        local_data.size
    );
    
    status = (local_data.bytes_read > 0);
    break;
}

// 4. 用户层接口 (hypervisor_gateway.cpp)
bool read_guest_memory(void* guest_addr, void* buffer, size_t size, size_t* bytes_read)
{
    READ_MEMORY_DATA data = {};
    data.command = VMCALL_READ_GUEST_MEMORY;
    data.guest_address = (unsigned __int64)guest_addr;
    data.buffer = (unsigned __int64)buffer;
    data.size = size;
    
    bool result = vmcall(&data, sizeof(data));
    if (result && bytes_read)
    {
        *bytes_read = data.bytes_read;
    }
    return result;
}
```

---

### 添加新的 EPT Hook 类型

#### 步骤 1: 定义 Hook 类型
在 `EPT.h` 中添加：
```cpp
enum __ept_hook_type
{
    EPT_HOOK_TYPE_NONE,
    EPT_HOOK_TYPE_FUNCTION,
    EPT_HOOK_TYPE_PAGE,
    EPT_HOOK_TYPE_YOUR_NEW_TYPE,  // 添加新类型
};
```

#### 步骤 2: 实现 Hook 逻辑
在 `EPT.cpp` 中添加：
```cpp
bool your_new_hook_type(
    __ept_state& ept_state,
    void* target_address,
    void* hook_data,
    unsigned __int64 target_cr3
)
{
    // 1. 获取或创建 EPT 条目
    __ept_pte* pte = get_pte_for_address(ept_state, target_address, target_cr3);
    if (!pte) return false;
    
    // 2. 保存原始页面信息
    __hooked_page* hooked_page = allocate_hooked_page();
    hooked_page->original_pte = *pte;
    hooked_page->virtual_address = target_address;
    hooked_page->hook_type = EPT_HOOK_TYPE_YOUR_NEW_TYPE;
    
    // 3. 设置 Hook 页面
    void* hook_page = create_hook_page(target_address, hook_data);
    pte->page_frame_number = PFN(hook_page);
    
    // 4. 修改权限（如果需要）
    pte->read_access = 1;
    pte->write_access = 0;  // 禁用写入
    pte->execute_access = 1;
    
    // 5. 添加到列表
    InsertTailList(&ept_state.hooked_pages_list, &hooked_page->entry);
    
    // 6. 刷新 TLB
    hvgt::invept(false);
    
    return true;
}
```

#### 步骤 3: 处理 EPT 违规
在 `invalid_ept.cpp` 中添加：
```cpp
bool handle_ept_violation_your_type(__vcpu* vcpu, __ept_state& ept_state)
{
    unsigned __int64 guest_physical = vcpu->vmexit_info.guest_physical_address;
    __hooked_page* hooked_page = find_hooked_page(ept_state, guest_physical);
    
    if (!hooked_page || hooked_page->hook_type != EPT_HOOK_TYPE_YOUR_NEW_TYPE)
        return false;
    
    // 处理违规
    // ... 你的处理逻辑
    
    return true;
}
```

#### 步骤 4: 注册到违规处理器
在 `vmexit_ept_violation_handler` 中添加：
```cpp
// 尝试各种 Hook 类型的处理
if (handle_ept_violation_function_hook(vcpu, ept_state))
    return;
    
if (handle_ept_violation_your_type(vcpu, ept_state))
    return;

// 其他处理...
```

---

### 调试和测试

#### 单元测试框架
创建 `Tests/` 目录：
```
Tests/
├── TestVMCall.cpp          # VMCALL 测试
├── TestEPT.cpp             # EPT 测试
├── TestVMExit.cpp          # VM Exit 测试
└── TestFramework.h         # 测试框架
```

#### 测试示例
```cpp
// TestFramework.h
#define TEST_ASSERT(condition) \
    if (!(condition)) { \
        KdPrint(("[TEST] FAILED: %s at %s:%d\n", #condition, __FILE__, __LINE__)); \
        return false; \
    }

#define TEST_ASSERT_EQ(expected, actual) \
    if ((expected) != (actual)) { \
        KdPrint(("[TEST] FAILED: Expected %llx, got %llx at %s:%d\n", \
                 (unsigned __int64)(expected), (unsigned __int64)(actual), __FILE__, __LINE__)); \
        return false; \
    }

// TestVMCall.cpp
bool test_vmcall_basic()
{
    KdPrint(("[TEST] Testing basic VMCALL...\n"));
    
    // 测试 VMCALL_TEST
    bool result = hvgt::test_vmcall();
    TEST_ASSERT(result);
    
    KdPrint(("[TEST] Basic VMCALL test PASSED\n"));
    return true;
}

bool test_vmcall_hook()
{
    KdPrint(("[TEST] Testing VMCALL hook...\n"));
    
    // 测试函数
    auto test_func = []() { return 42; };
    
    // 设置 Hook
    void* original = nullptr;
    bool result = hvgt::hook_function((void*)test_func, (void*)hook_func, &original);
    TEST_ASSERT(result);
    
    // 取消 Hook
    result = hvgt::ept_unhook((void*)test_func);
    TEST_ASSERT(result);
    
    KdPrint(("[TEST] VMCALL hook test PASSED\n"));
    return true;
}

void run_all_tests()
{
    KdPrint(("[TEST] Starting test suite...\n"));
    
    int passed = 0;
    int failed = 0;
    
    if (test_vmcall_basic()) passed++; else failed++;
    if (test_vmcall_hook()) passed++; else failed++;
    // ... 更多测试
    
    KdPrint(("[TEST] Results: %d passed, %d failed\n", passed, failed));
}
```

#### 调试技巧

##### 1. 使用 VMCS Dump
```cpp
// 在 VM Exit 处理中调用
hv::dump_vmcs();
```

##### 2. 内存断点调试
```cpp
// 在特定地址设置内存断点
__debugbreak();  // 在代码中插入断点
```

##### 3. 日志级别控制
```cpp
// 在 Globals.h 中定义日志级别
#define LOG_LEVEL_NONE     0
#define LOG_LEVEL_ERROR    1
#define LOG_LEVEL_WARNING  2
#define LOG_LEVEL_INFO     3
#define LOG_LEVEL_DEBUG    4

#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG

#define LOG(level, format, ...) \
    if (level <= CURRENT_LOG_LEVEL) { \
        KdPrint(("[%s] " format, #level, ##__VA_ARGS__)); \
    }
```

---

### 常见问题排查

#### 问题 1: VM Entry 失败
**症状**: 系统蓝屏，代码 `0xC0000005` 或 `0x80000033`

**排查步骤**:
```cpp
// 1. 检查 VMCS 配置
hv::dump_vmcs();

// 2. 验证控制字段
unsigned __int64 pin_based = hv::vmread(PIN_BASED_VM_EXEC_CONTROL);
unsigned __int64 cpu_based = hv::vmread(CPU_BASED_VM_EXEC_CONTROL);
KdPrint(("Pin-based: %llx, CPU-based: %llx\n", pin_based, cpu_based));

// 3. 检查 Guest State
unsigned __int64 guest_cr0 = hv::vmread(GUEST_CR0);
unsigned __int64 guest_cr4 = hv::vmread(GUEST_CR4);
KdPrint(("Guest CR0: %llx, CR4: %llx\n", guest_cr0, guest_cr4));
```

**常见原因**:
- CR0/CR4 控制字段未正确设置
- VMCS 字段未对齐
- EPT 指针无效

#### 问题 2: EPT 违规处理循环
**症状**: 系统卡死或频繁 VM Exit

**解决方案**:
```cpp
// 在 EPT 违规处理器中添加循环检测
static unsigned __int64 last_gpa = 0;
static int violation_count = 0;

if (guest_physical == last_gpa)
{
    if (++violation_count > 10)
    {
        KdPrint(("[ERROR] EPT violation loop detected at %llx\n", guest_physical));
        // 恢复原始页面权限
        restore_original_pte(vcpu, guest_physical);
        return;
    }
}
else
{
    last_gpa = guest_physical;
    violation_count = 0;
}
```

#### 问题 3: 内存泄漏
**症状**: 系统内存逐渐减少

**排查方法**:
```cpp
// 定期转储内存池信息
pool_manager::dump_pools_info();

// 检查 Hook 页面列表
void dump_hooked_pages(__ept_state& ept_state)
{
    PLIST_ENTRY entry = ept_state.hooked_pages_list.Flink;
    int count = 0;
    
    while (entry != &ept_state.hooked_pages_list)
    {
        __hooked_page* page = CONTAINING_RECORD(entry, __hooked_page, entry);
        KdPrint(("Hooked page %d: VA=%llx, PA=%llx\n", 
                 count++, page->virtual_address, page->original_pte.page_frame_number << 12));
        entry = entry->Flink;
    }
}
```

#### 问题 4: 多处理器竞争
**症状**: 随机蓝屏，IRQL 错误

**解决方案**:
```cpp
// 使用自旋锁保护共享数据
spinlock_t my_lock;

void safe_operation()
{
    spinlock_acquire(&my_lock);
    // 访问共享数据
    spinlock_release(&my_lock);
}

// 或者使用 Interlocked 函数
InterlockedIncrement(&shared_counter);
```

---

### 版本发布流程

#### 版本号规范
使用语义化版本：`主版本.次版本.修订号`
- 主版本：重大架构变更
- 次版本：功能添加
- 修订号：Bug 修复

#### 发布检查清单
```
□ 所有单元测试通过
□ 代码审查完成
□ 文档已更新
□ 版本号已更新
□ 更新日志已编写
□ Release Notes 已准备
□ 安装包已测试
```

#### Git 分支策略
```
main (稳定版本)
  ├── develop (开发分支)
  │     ├── feature/new-vmexit-handler
  │     ├── feature/new-vmcall-command
  │     └── bugfix/ept-violation-loop
  ├── release/v1.2.0
  └── hotfix/critical-bug
```

#### 发布脚本示例
```powershell
# release.ps1
param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

# 1. 更新版本号
(Get-Content "VT_Driver\Globals.h") -replace 'VERSION ".*"', "VERSION \"$Version\"" | Set-Content "VT_Driver\Globals.h"

# 2. 编译驱动
msbuild UnrealDbg.sln /p:Configuration=Release /p:Platform=x64 /t:VT_Driver

# 3. 编译 UI
cd UnrealDbg
dcc64 UnrealDbg.dpr -B -E..\Release\$Version

# 4. 复制文件
New-Item -ItemType Directory -Force -Path "Release\$Version"
Copy-Item "VT_Driver\x64\Release\VT_Driver.sys" "Release\$Version\"
Copy-Item "UnrealDbg\Release\$Version\UnrealDbg.exe" "Release\$Version\"
Copy-Item "README.md" "Release\$Version\"

# 5. 创建压缩包
Compress-Archive -Path "Release\$Version\*" -DestinationPath "Release\UnrealDbg-v$Version.zip"

Write-Host "Release v$Version created successfully!"
```

---

### 代码审查清单

#### 安全性检查
- [ ] 所有用户输入都经过验证
- [ ] 内存访问使用安全的读写函数
- [ ] 没有硬编码的敏感信息
- [ ] 锁的使用正确，避免死锁

#### 性能检查
- [ ] 避免在 VM Exit 处理中进行耗时操作
- [ ] 减少不必要的 TLB 刷新
- [ ] 合理使用大页减少页表级数

#### 可维护性检查
- [ ] 代码注释清晰
- [ ] 函数职责单一
- [ ] 命名规范一致
- [ ] 错误处理完善

---


- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - Intel 软件开发手册
- [ia32-doc](https://github.com/wbenny/ia32-doc) - Intel IA-32 文档生成工具
- [Phnt](https://github.com/processhacker/phnt) - Process Hacker NT 头文件
- [Microsoft Detours](https://github.com/microsoft/Detours) - Hook 库
- [HyperPlatform](https://github.com/tandasat/HyperPlatform) - 参考实现




**免责声明**: 代码不是我写的，我只是看了一段时间，自己修改了一个版本，现在把原始版本放出来
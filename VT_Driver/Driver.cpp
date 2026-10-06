#include "Driver.h"
#include "poolmanager.h"
#include "Globals.h"
#include "mtrr.h"
#include "EPT.h"
#include "hypervisor_routines.h"
#include "vmm.h"
#include "hypervisor_gateway.h"
#include "interrupt.h"
#include "Init/Symbolic/InitNtoskrnl.h"
#include "AsmCallset.h"
#include "vmexit_handler.h"
#include "vmcs.h"


EXTERN_C
VOID Unload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] 开始卸载\n"));
    const bool hasVcpuTable = g_vmm_context.vcpu != nullptr && g_vmm_context.processor_count != 0;
    bool anyVmxState = false;
    if (hasVcpuTable)
    {
        for (unsigned int i = 0; i < g_vmm_context.processor_count; ++i)
        {
            if (g_vmm_context.vcpu[i].vcpu_status.vmm_launched ||
                g_vmm_context.vcpu[i].vcpu_status.vmx_on)
            {
                anyVmxState = true;
                break;
            }
        }
    }
    if (anyVmxState)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] 正在移除 EPT 挂钩并停止 VMX\n"));
        if (g_vmm_context.vcpu[0].vcpu_status.vmm_launched)
        {
            hvgt::ept_unhook();
        }
        hvgt::vmoff(g_vmm_context.processor_count);
    }

    if (hasVcpuTable)
    {
        hv::disable_vmx_operation();
    }
    free_vmm_context();

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] 卸载完成\n"));
}

//对于vt驱动尽量少使用Windows内核的api函数
//请参考vmexit_handler函数
EXTERN_C
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    sLog("\n");
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] 驱动入口开始\n"));

    DriverObject->DriverUnload = Unload;
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] 已注册卸载例程\n"));
    

    NTSTATUS nStatus = STATUS_SUCCESS;

    if (InitNtoskrnlSymbolsTable())
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] ntoskrnl 符号初始化完成\n"));
        int cpuid[4] = { 0 };
        __cpuid(cpuid, 1);
        if ((cpuid[2] & (1 << 31)) != 0)
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[VT_Driver] 检测到已有 Hyper-V/VBS 虚拟机监控程序；当前裸 VMX 驱动无法安全接管 VMX root mode，拒绝继续初始化。解决方案：关闭 Hyper-V、VBS、HVCI 后重启，或使用支持嵌套虚拟化的实现。\n"));
            return STATUS_NOT_SUPPORTED;
        }
        //
        // Check if our cpu support virtualization
        //
        if (!hv::virtualization_support()) {
            outDebug("当前处理器不支持 VMX 虚拟化操作。\n");
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 虚拟化支持检测结果：不支持\n"));
            return STATUS_UNSUCCESSFUL;
        }

        //
        // Initialize and start virtual machine
        // If it fails turn off vmx and deallocate all structures
        // 初始化 并安装vt
        //

        hv::InitGlobalVariables();
        if (vmm_init() == false)
        {
            hv::disable_vmx_operation();
            free_vmm_context();
            outDebug("VMM 初始化失败");
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] vmm_init 初始化失败\n"));
            return STATUS_UNSUCCESSFUL;
        }
        outDebug("驱动加载成功!!!\n");
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] VMX/VMM 初始化成功\n"));
    }
    else
    {
        nStatus = STATUS_UNSUCCESSFUL;
        outDebug("驱动加载失败!!!\n");
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] ntoskrnl 符号表初始化失败\n"));
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, NT_SUCCESS(nStatus) ? DPFLTR_INFO_LEVEL : DPFLTR_ERROR_LEVEL,
        "[VT_Driver] 驱动入口结束，状态=0x%08X\n", nStatus));
    return nStatus;
}

bool InitOffset(PWINDOWS_STRUCT vmcallinfo)
{
    WINDOWS_STRUCT tmp_vmcallinfo = { 0 };

    if (sizeof(WINDOWS_STRUCT) != hv::read_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(WINDOWS_STRUCT)))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 初始化偏移失败：客户机内存读取不完整；原因：客户机地址或符号偏移无效；解决方案：重新加载匹配版本的符号表。\n"));
        return false;
    }

    hv::ghv.kpcr_pcrb_offset = 0x180;
    hv::ghv.kprcb_current_thread_offset = 0x8;
    ethread_offset::Cid = tmp_vmcallinfo.ethread_offset_Cid;
    return true;
}

PCLIENT_ID GuestCurrentThreadCid()
{
    size_t Thread = hv::current_guest_ethread();
    size_t ptr_Cid = Thread + ethread_offset::Cid;
    return (PCLIENT_ID)ptr_Cid;
}

bool SetBreakpoint(PVT_BREAK_POINT vmcallinfo, unsigned __int64 Type)
{
    int errorCode = 0;
    int status = 0;
    VT_BREAK_POINT tmp_vmcallinfo = { 0 };

    if (sizeof(VT_BREAK_POINT) != hv::read_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 设置断点失败：客户机内存读取不完整；原因：目标地址无效或客户机未就绪；解决方案：检查进程和符号表后重试。\n"));
        return false;
    }

    int outID = -1;
    if (ept::ept_watch_activate(tmp_vmcallinfo, Type, &outID, errorCode))
    {
        tmp_vmcallinfo.watchid = outID;

        if (sizeof(VT_BREAK_POINT) != hv::write_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 设置断点失败：客户机内存写入不完整；原因：目标页面不可写或客户机已退出；解决方案：确认目标仍在运行并检查内存权限。\n"));
            return false;
        }
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] 断点已设置：地址=%p 类型=%llu 监视编号=%d\n",
            tmp_vmcallinfo.VirtualAddress, Type, outID));
        return true;
    }
    else
    {
        tmp_vmcallinfo.errorCode = errorCode;

        if (sizeof(VT_BREAK_POINT) != hv::write_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 设置断点失败：错误结果写回不完整；原因：客户机通信缓冲区无效；解决方案：重新初始化 VT 后重试。\n"));
            return false;
        }
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 断点被拒绝：地址=%p 类型=%llu 错误码=%d\n",
            tmp_vmcallinfo.VirtualAddress, Type, errorCode));
    }
    return false;
}

bool RemoveBreakpoint(PVT_BREAK_POINT vmcallinfo)
{
    VT_BREAK_POINT tmp_vmcallinfo = { 0 };

    if (sizeof(VT_BREAK_POINT) != hv::read_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 移除断点失败：客户机内存读取不完整；原因：目标地址或客户机状态无效；解决方案：重新枚举目标并重试。\n"));
        return false;
    }

    if (ept::ept_watch_deactivate(tmp_vmcallinfo, tmp_vmcallinfo.watchid) == 0)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VT_Driver] 断点已移除：地址=%p 监视编号=%d\n",
            tmp_vmcallinfo.VirtualAddress, tmp_vmcallinfo.watchid));
        return true;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[VT_Driver] 移除断点失败：监视编号=%d\n", tmp_vmcallinfo.watchid));
    return false;
}

void MyKeBugCheck(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4
)
{
    //__vmx_off();                                  // 退出vmx模式
    //__writecr3(g_guest_cr3);                      // 还原cr3
    //__reload_gdtr(g_gdtr.base_address, g_gdtr.limit); // 还原gdt
    //__lidt(&g_idtr);                                 // 还原idt 
    KeBugCheckEx(BugCheckCode, BugCheckParameter1, BugCheckParameter2, BugCheckParameter3, BugCheckParameter4);// 触发蓝屏dump
}

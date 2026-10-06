#include "Driver.h"
#include "poolmanager.h"
#include "Globals.h"
#include "vmcall_handler.h"
#include "interrupt.h"
#include "vmcall_reason.h"
#include "invalid_ept.h"
#include "msr.h"
#include "mtrr.h"
#include "EPT.h"
#include "AsmCallset.h"
#include "hypervisor_routines.h"
#include "vmexit_handler.h"
#include "vmcs.h"

void restore_segment_registers()
{
	__writemsr(IA32_FS_BASE, hv::vmread(GUEST_FS_BASE));
	__writemsr(IA32_GS_BASE, hv::vmread(GUEST_GS_BASE));
	__reload_gdtr(hv::vmread(GUEST_GDTR_BASE), hv::vmread(GUEST_GDTR_LIMIT));
	__reload_idtr(hv::vmread(GUEST_IDTR_BASE), hv::vmread(GUEST_IDTR_LIMIT));
}

void call_vmxoff(__vcpu* vcpu)
{
	//vcpu->vmx_off_state.guest_rip = vcpu->vmexit_info.guest_rip + vcpu->vmexit_info.instruction_length;
	//vcpu->vmx_off_state.guest_rsp = vcpu->vmexit_info.guest_registers->rsp;
	vcpu->vmx_off_state.vmx_off_executed = true;
}

void vmexit_vmcall_handler(__vcpu* vcpu)
{
	bool status = true;
	unsigned __int64 vmcall_reason = 0;
	unsigned __int64 vmcall_parameter1 = 0;
	unsigned __int64 vmcall_parameter2 = 0;
	unsigned __int64 vmcall_parameter3 = 0;
	unsigned __int64 vmcall_parameter4 = 0;
	unsigned __int64 vmcall_parameter5 = 0;
	unsigned __int64 vmcall_parameter6 = 0;
	unsigned __int64 vmcall_parameter7 = 0;
	unsigned __int64 vmcall_parameter8 = 0;
	unsigned __int64 vmcall_parameter9 = 0;

	//
	// Check if this vmcall belongs to us
	// 检查此vmcall是否属于我们调用的
	//
	if ((vcpu->vmexit_info.guest_registers->rax != VMCALL_IDENTIFIER) &&
		(vcpu->vmexit_info.guest_registers->eax != VMCALL_IDENTIFIER2))
	{
		if (ept::handler_vmcall_rip(*vcpu->ept_state))
		{
			return;  //不去更改rax寄存器
		}

		hv::inject_interruption(EXCEPTION_VECTOR_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, false);
		return;
	}

	//只允许ring0程序调用
	//if (hv::get_guest_cpl() != 0)
	//{
	//	//向guest机注入#GP异常
	//	hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, 1);
	//	return;
	//}



	if (vcpu->vmexit_info.guest_registers->eax == VMCALL_IDENTIFIER2)
	{
		vmcall_reason = vcpu->vmexit_info.guest_registers->ecx;
		vmcall_parameter1 = vcpu->vmexit_info.guest_registers->edx;
	}
	else
	{
		vmcall_reason = vcpu->vmexit_info.guest_registers->rcx;
		vmcall_parameter1 = vcpu->vmexit_info.guest_registers->rdx;
		vmcall_parameter2 = vcpu->vmexit_info.guest_registers->r8;
		vmcall_parameter3 = vcpu->vmexit_info.guest_registers->r9;

		//
		// These only if __vmcall_ex was called
		//
		vmcall_parameter4 = vcpu->vmexit_info.guest_registers->r10;
		vmcall_parameter5 = vcpu->vmexit_info.guest_registers->r11;
		vmcall_parameter6 = vcpu->vmexit_info.guest_registers->r12;
		vmcall_parameter7 = vcpu->vmexit_info.guest_registers->r13;
		vmcall_parameter8 = vcpu->vmexit_info.guest_registers->r14;
		vmcall_parameter9 = vcpu->vmexit_info.guest_registers->r15;
	}



	switch (vmcall_reason)  //什么原因调用vmcall
	{
	case VMCALL_TEST:
	{
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_VMXOFF:
	{
		call_vmxoff(vcpu);  //退出vmx模式
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_EPT_CC_HOOK:
	{
		//unsigned __int64 old_cr3 = hv::swap_context(vmcall_parameter4);

		//status = ept::cc_hook_function(*vcpu->ept_state, (void*)vmcall_parameter1, (void*)vmcall_parameter2, (void**)vmcall_parameter3);

		//hv::restore_context(old_cr3);

		adjust_rip(vcpu);
		break;
	}
	case VMCALL_EPT_INT1_HOOK:
	{
		//unsigned __int64 old_cr3 = hv::swap_context(vmcall_parameter4);

		//status = ept::int1_hook_function(*vcpu->ept_state, (void*)vmcall_parameter1, (void*)vmcall_parameter2, (void**)vmcall_parameter3);

		//hv::restore_context(old_cr3);

		adjust_rip(vcpu);
		break;
	}
	case VMCALL_EPT_RIP_HOOK:
	{
		status = ept::vmcall_hook_function(*vcpu->ept_state, 
			(void*)vmcall_parameter1, 
			(void*)vmcall_parameter2, 
			(void**)vmcall_parameter3,
			vmcall_parameter4);

		adjust_rip(vcpu);
		break;
	}
	case VMCALL_EPT_HOOK_FUNCTION:  //执行ept hook
	{
		//unsigned __int64 old_cr3 = hv::swap_context(vmcall_parameter4);

		//status = ept::hook_function(*vcpu->ept_state, (void*)vmcall_parameter1, (void*)vmcall_parameter2, (void**)vmcall_parameter3);

		//hv::restore_context(old_cr3);

		adjust_rip(vcpu);
		break;
	}
	case VMCALL_EPT_UNHOOK_FUNCTION:  //关闭ept hook
	{
		//unsigned __int64 old_cr3 = hv::swap_context(vmcall_parameter3);

		// If set unhook all pages
		if (vmcall_parameter1 == true)
		{
			ept::unhook_all_functions(*vcpu->ept_state);
		}

		else
		{
			// Page physciall address
			status = ept::unhook_function(*vcpu->ept_state, vmcall_parameter2);
		}

		//hv::restore_context(old_cr3);

		adjust_rip(vcpu);
		break;
	}
	case VMCALL_INVEPT_CONTEXT:  //刷新ept缓存
	{
		// If set invept all contexts
		if (vmcall_parameter1 == true)
		{
			invept_all_contexts_func();
		}
		else
		{
			invept_single_context_func((*vcpu->ept_state).ept_pointer->all);
		}

		adjust_rip(vcpu);
		break;
	}
	case VMCALL_DUMP_POOL_MANAGER:
	{
		pool_manager::dump_pools_info();
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_DUMP_VMCS_STATE:
	{
		hv::dump_vmcs();
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_HIDE_HV_PRESENCE:
	{
		g_vmm_context.hv_presence = false;
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_UNHIDE_HV_PRESENCE:
	{
		g_vmm_context.hv_presence = true;
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_HIDE_SOFTWARE_BREAKPOINT:
	{
		status = ept::set_hide_software_breakpoint((PVT_BREAK_POINT)vmcall_parameter1);
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_READ_SOFTWARE_BREAKPOINT:
	{
		status = ept::get_hide_software_breakpoint(*vcpu->ept_state, (PVT_BREAK_POINT)vmcall_parameter1);
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_READ_EPT_FAKE_PAGE_MEMORY:
	{
		//unsigned __int64 old_cr3 = hv::swap_context(vmcall_parameter4);

		//status = ept::get_hide_software_breakpoint(*vcpu->ept_state, (void*)vmcall_parameter1, (void*)vmcall_parameter2, vmcall_parameter3);

		//hv::restore_context(old_cr3);

		adjust_rip(vcpu);
		break;
	}
	case VMCALL_WATCH_WRITES:
	{
		status = SetBreakpoint((PVT_BREAK_POINT)vmcall_parameter1, EPTW_WRITE);
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_WATCH_READS:
	{
		status = SetBreakpoint((PVT_BREAK_POINT)vmcall_parameter1, EPTW_READWRITE);
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_WATCH_EXECUTES:
	{
		status = SetBreakpoint((PVT_BREAK_POINT)vmcall_parameter1, EPTW_EXECUTE);
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_WATCH_DELETE:
	{
		status = RemoveBreakpoint((PVT_BREAK_POINT)vmcall_parameter1);
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_GET_BREAKPOINT:
	{
		status = hv::get_breakpoint_detected(vcpu, (PBREAKPOINT_DETECTED)vmcall_parameter1);
		adjust_rip(vcpu);
		break;
	}
	case VMCALL_INIT_OFFSET:
	{
		status = InitOffset((PWINDOWS_STRUCT)vmcall_parameter1);
		adjust_rip(vcpu);
		break;
	}
	}

	vcpu->vmexit_info.guest_registers->rax = status;
}
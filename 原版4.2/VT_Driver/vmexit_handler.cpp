#include "Driver.h"
#include "poolmanager.h"
#include "Globals.h"
#include "mtrr.h"
#include "EPT.h"
#include "vmexit_handler.h"
#include "hypervisor_routines.h"
#include "vmcs.h"
#include "interrupt.h"
#include "AsmCallset.h"
#include "crx.h"
#include "drx.h"
#include "cpuid.h"
#include "msr.h"
#include "invalid_vpid.h"
#include "invalid_pcid.h"
#include "segment.h"
#include "spinlock.h"


void vmexit_nmi_window_handler(__vcpu* vcpu);
void vmexit_vmcall_handler(__vcpu* vcpu);
void vmexit_unimplemented(__vcpu* vcpu);
void vmexit_monitor_trap_flag_handler(__vcpu* vcpu);
void vmexit_exception_or_nmi_handler(__vcpu* vcpu);
void vmexit_ept_violation_handler(__vcpu* vcpu);
void vmexit_ept_misconfiguration_handler(__vcpu* vcpu);
void vmexit_cr_handler(__vcpu* vcpu);
void vmexit_vm_instruction(__vcpu* vcpu);
void vmexit_triple_fault_handler(__vcpu* vcpu);
void vmexit_failed(__vcpu* vcpu);
void vmexit_invd_handler(__vcpu* vcpu);
void vmexit_invlpg_handler(__vcpu* vcpu);
void vmexit_rdtscp_handler(__vcpu* vcpu);
void vmexit_xsetbv_handler(__vcpu* vcpu);
void vmexit_rdtsc_handler(__vcpu* vcpu);
void vmexit_cpuid_handler(__vcpu* vcpu);
void vmexit_msr_read_handler(__vcpu* vcpu);
void vmexit_msr_write_handler(__vcpu* vcpu);
void vmexit_vmx_on_handler(__vcpu* vcpu);
void vmexit_getsec_handler(__vcpu* vcpu);
void vmexit_vmx_preemption_handler(__vcpu* vcpu);

//恢复guest状态
void RestoreGuest()
{
	//恢复guest的寄存器状态
	hv::vmwrite(CR0_READ_SHADOW, hv::read_effective_guest_cr0().flags);
	hv::vmwrite(CR4_READ_SHADOW, hv::read_effective_guest_cr4().flags);

	// 恢复dr7 cr3寄存器的值
	__writedr(7, hv::vmread(GUEST_DR7));
	__writecr3(hv::vmread(GUEST_CR3));

	// MSRs
	__writemsr(IA32_SYSENTER_CS, hv::vmread(GUEST_SYSENTER_CS));
	__writemsr(IA32_SYSENTER_ESP, hv::vmread(GUEST_SYSENTER_ESP));
	__writemsr(IA32_SYSENTER_EIP, hv::vmread(GUEST_SYSENTER_EIP));
	__writemsr(IA32_PAT, hv::vmread(GUEST_PAT));
	__writemsr(IA32_DEBUGCTL, hv::vmread(GUEST_DEBUG_CONTROL));

	//恢复gdtr和idtr寄存器的值
	__reload_gdtr(hv::vmread(GUEST_GDTR_BASE), hv::vmread(GUEST_GDTR_LIMIT));
	__reload_idtr(hv::vmread(GUEST_IDTR_BASE), hv::vmread(GUEST_IDTR_LIMIT));

	segment_selector guest_tr;
	guest_tr.flags = static_cast<uint16_t>(hv::vmread(GUEST_TR_SELECTOR));

	// TSS
	//注意顺序要先将gdt的值写回gdtr寄存器
	__pseudo_descriptor64 gdtr = { 0 };
	__sgdt(&gdtr);
	(reinterpret_cast<segment_descriptor_32*>(gdtr.base_address) + guest_tr.index)->type = SEGMENT_DESCRIPTOR_TYPE_TSS_AVAILABLE;
	write_tr(guest_tr.flags);

	// segment selectors
	write_ds(static_cast<uint16_t>(hv::vmread(GUEST_DS_SELECTOR)));
	write_es(static_cast<uint16_t>(hv::vmread(GUEST_ES_SELECTOR)));
	write_fs(static_cast<uint16_t>(hv::vmread(GUEST_FS_SELECTOR)));
	write_gs(static_cast<uint16_t>(hv::vmread(GUEST_GS_SELECTOR)));
	write_ldtr(static_cast<uint16_t>(hv::vmread(GUEST_LDTR_SELECTOR)));

	// FS and GS base address
	// 和__writemsr(IA32_GS_BASE, hv::vmread(GUEST_GS_BASE))比起来
	// 这种方式能立即将GUEST_GS_BASE的值写入到gs base寄存器里
	_writefsbase_u64(hv::vmread(GUEST_FS_BASE));
	_writegsbase_u64(hv::vmread(GUEST_GS_BASE));
}

void set_hide_vm_exit_overhead(__vcpu* vcpu, bool value)
{
	vcpu->hide_vm_exit_overhead = value;
}

//隐藏vmexit开销
void hide_vm_exit_overhead(__vcpu* vcpu) {
	//
	// Guest APERF/MPERF values are stored/restored on vm-entry and vm-exit,
	// however, there appears to be a small, yet constant, overhead that occurs
	// when the CPU is performing these stores and loads. This is the case for
	// every MSR, so naturally PERF_GLOBAL_CTRL is affected as well. If it wasn't
	// for this, hiding vm-exit overhead would be sooooo much easier and cleaner,
	// but whatever.
	//

	ia32_perf_global_ctrl_register perf_global_ctrl;
	perf_global_ctrl.flags = vcpu->msr_exit_store.perf_global_ctrl.msr_data;

	// make sure the CPU loads the previously stored guest state on vm-entry
	vcpu->msr_entry_load.aperf.msr_data = vcpu->msr_exit_store.aperf.msr_data;
	vcpu->msr_entry_load.mperf.msr_data = vcpu->msr_exit_store.mperf.msr_data;
	hv::vmwrite(VMCS_GUEST_PERF_GLOBAL_CTRL, perf_global_ctrl.flags);

	// account for the constant overhead associated with loading/storing MSRs
	vcpu->msr_entry_load.aperf.msr_data -= vcpu->vm_exit_mperf_overhead;
	vcpu->msr_entry_load.mperf.msr_data -= vcpu->vm_exit_mperf_overhead;

	// account for the constant overhead associated with loading/storing MSRs
	if (perf_global_ctrl.en_fixed_ctrn & (1ull << 2)) {
		auto const cpl = hv::current_guest_cpl();

		ia32_fixed_ctr_ctrl_register fixed_ctr_ctrl;
		fixed_ctr_ctrl.flags = __readmsr(IA32_FIXED_CTR_CTRL);

		// this also needs to be done for many other PMCs, but whatever
		if ((cpl == 0 && fixed_ctr_ctrl.en2_os) || (cpl == 3 && fixed_ctr_ctrl.en2_usr))
			__writemsr(IA32_FIXED_CTR2, __readmsr(IA32_FIXED_CTR2) - vcpu->vm_exit_ref_tsc_overhead);
	}

	// this usually occurs for vm-exits that are unlikely to be reliably timed,
	// such as when an exception occurs or if the preemption timer fired
	if (!vcpu->hide_vm_exit_overhead || vcpu->vm_exit_tsc_overhead > 10000) {
		// this is our chance to resync the TSC
		vcpu->tsc_offset = 0;

		// soft disable the VMX preemption timer
		vcpu->preemption_timer = ~0ull;

		return;
	}

	// set the preemption timer to cause an exit after 10000 guest TSC ticks have passed
	ia32_vmx_misc_register vmx_misc;
	vmx_misc.flags = __readmsr(IA32_VMX_MISC);
	vcpu->preemption_timer = max(2, 10000 >> vmx_misc.preemption_timer_tsc_relationship);

	// use TSC offsetting to hide from timing attacks that use the TSC
	vcpu->tsc_offset -= vcpu->vm_exit_tsc_overhead;
}

//注意在host里尽量避免使用Windows的api，因为Windows api有很多自己的限制
//例如会检查irql级别和栈base limit检查，在检查不通过时会报告BSOD
//ASSERT DbgBreakPoint等函数都无法在host里使用
EXTERN_C
bool vmexit_handler(guest_context* guest_registers, PFXSAVE64 fxsave)
{
	//__vcpu* vcpu = g_vmm_context.vcpu_table[KeGetCurrentProcessorNumber()];

	//从fs_base里取出vcpu的指针
	//因为在填充host_vmcs字段HOST_FS_BASE时保存的
	__vcpu* vcpu = reinterpret_cast<__vcpu*>(_readfsbase_u64());

	guest_registers->rsp = hv::vmread(GUEST_RSP);

	vcpu->vmexit_info.reason = hv::vmread(VM_EXIT_REASON) & 0xffff;
	vcpu->vmexit_info.qualification = hv::vmread(EXIT_QUALIFICATION);
	vcpu->vmexit_info.guest_rflags.all = hv::vmread(GUEST_RFLAGS);
	vcpu->vmexit_info.guest_rip = hv::vmread(GUEST_RIP);
	vcpu->vmexit_info.instruction_length = hv::vmread(VM_EXIT_INSTRUCTION_LENGTH);
	vcpu->vmexit_info.instruction_information = hv::vmread(VM_EXIT_INSTRUCTION_INFORMATION);
	vcpu->vmexit_info.guest_registers = guest_registers;
	vcpu->vmexit_info.fxsave = fxsave;

	vcpu->hide_vm_exit_overhead = false;
	//
	//采用函数棒的设计无法满足设计需求
	//换做switch，这样可以将未来未收录的reason放到default里进行处理
	dispatch_vm_exit(vcpu);
	
	if (vcpu->vmx_off_state.vmx_off_executed == true)
	{
		vcpu->vcpu_status.vmm_launched = false;

		RestoreGuest();

		//如果要停止虚拟化，vmexit_handler 将返回 true
		return true;
	}

	//hide_vm_exit_overhead(vcpu);

	//hv::vmwrite(TSC_OFFSET, vcpu->tsc_offset);
	//hv::vmwrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, vcpu->preemption_timer);

	return false;
}

void dispatch_vm_exit(__vcpu* vcpu)
{
	switch (vcpu->vmexit_info.reason)
	{
	case VMX_EXIT_REASON_EXCEPTION_OR_NMI:             vmexit_exception_or_nmi_handler(vcpu);     break;
	case VMX_EXIT_REASON_EXECUTE_GETSEC:               vmexit_getsec_handler(vcpu);              break;
	case VMX_EXIT_REASON_EXECUTE_INVD:                 vmexit_invd_handler(vcpu);                break;
	case VMX_EXIT_REASON_EXECUTE_INVLPG:               vmexit_invlpg_handler(vcpu);              break;
	case VMX_EXIT_REASON_NMI_WINDOW:                   vmexit_nmi_window_handler(vcpu);           break;
	case VMX_EXIT_REASON_EXECUTE_CPUID:                vmexit_cpuid_handler(vcpu);               break;
	case VMX_EXIT_REASON_MOV_CR:                       vmexit_cr_handler(vcpu);               break;
	case VMX_EXIT_REASON_EXECUTE_RDMSR:                vmexit_msr_read_handler(vcpu);               break;
	case VMX_EXIT_REASON_EXECUTE_WRMSR:                vmexit_msr_write_handler(vcpu);               break;
	case VMX_EXIT_REASON_EXECUTE_XSETBV:               vmexit_xsetbv_handler(vcpu);              break;
	case VMX_EXIT_REASON_EXECUTE_VMXON:                vmexit_vmx_on_handler(vcpu);               break;
	case VMX_EXIT_REASON_EXECUTE_VMCALL:               vmexit_vmcall_handler(vcpu);              break;
	case VMX_EXIT_REASON_VMX_PREEMPTION_TIMER_EXPIRED: vmexit_vmx_preemption_handler(vcpu);       break;
	case VMX_EXIT_REASON_EPT_VIOLATION:                vmexit_ept_violation_handler(vcpu);        break;
	case VMX_EXIT_REASON_EXECUTE_RDTSC:                vmexit_rdtsc_handler(vcpu);               break;
	case VMX_EXIT_REASON_EXECUTE_RDTSCP:               vmexit_rdtscp_handler(vcpu);              break;
	case VMX_EXIT_REASON_MONITOR_TRAP_FLAG:            vmexit_monitor_trap_flag_handler(vcpu);    break;
	case VMX_EXIT_REASON_EPT_MISCONFIGURATION:         vmexit_ept_misconfiguration_handler(vcpu); break;
	case VMX_EXIT_REASON_EXECUTE_VMCLEAR:
	case VMX_EXIT_REASON_EXECUTE_VMLAUNCH:
	case VMX_EXIT_REASON_EXECUTE_VMPTRLD:
	case VMX_EXIT_REASON_EXECUTE_VMPTRST:
	case VMX_EXIT_REASON_EXECUTE_VMREAD:
	case VMX_EXIT_REASON_EXECUTE_VMRESUME:
	case VMX_EXIT_REASON_EXECUTE_VMWRITE:
	case VMX_EXIT_REASON_EXECUTE_VMXOFF:
	case VMX_EXIT_REASON_EXECUTE_INVEPT:
	case VMX_EXIT_REASON_EXECUTE_INVVPID:								
	case VMX_EXIT_REASON_EXECUTE_VMFUNC:               vmexit_vm_instruction(vcpu);    break;

	default:
	{
		//其他vmexit，先注入#GP异常
		hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
		break;
	}
	}
}


void skip_instruction()
{
	// increment RIP
	auto const old_rip = hv::vmread(GUEST_RIP);
	auto new_rip = old_rip + hv::vmread(VM_EXIT_INSTRUCTION_LENGTH);

	// handle wrap-around for 32-bit addresses
	// 处理32位地址，判断newip是否超过了32位地址
	// https://patchwork.kernel.org/project/kvm/patch/20200427165917.31799-1-pbonzini@redhat.com/
	if (old_rip < (1ull << 32) && new_rip >= (1ull << 32)) {
		vmx_segment_access_rights cs_access_rights;
		cs_access_rights.flags = static_cast<uint32_t>(hv::vmread(GUEST_CS_ACCESS_RIGHTS));

		// make sure guest is in 32-bit mode
		// 如果guest是32位模式，则修正newip确保是32位地址
		if (!cs_access_rights.long_mode)
			new_rip &= 0xFFFF'FFFF;
	}

	hv::vmwrite(GUEST_RIP, new_rip);

	// if we're currently blocking interrupts (due to mov ss or sti)
	// then we should unblock them since we just emulated an instruction
	// 如果我们当前正在阻止中断（由于 mov ss 或 sti），那么我们应该解除对它们的阻止，因为我们刚刚模拟了一条指令。
	auto interrupt_state = hv::read_interruptibility_state();
	interrupt_state.blocking_by_mov_ss = 0;
	interrupt_state.blocking_by_sti = 0;
	hv::write_interruptibility_state(interrupt_state);

	ia32_debugctl_register debugctl;
	debugctl.flags = hv::vmread(GUEST_DEBUG_CONTROL);

	rflags rflags;
	rflags.flags = hv::vmread(GUEST_RFLAGS);

	// if we're single-stepping, inject a debug exception
	// just like normal instruction execution would
	// 如果我们单步执行，则像正常指令执行一样注入调试异常
	if (rflags.trap_flag && !debugctl.btf) {
		vmx_pending_debug_exceptions dbg_exception;
		dbg_exception.flags = hv::vmread(GUEST_PENDING_DEBUG_EXCEPTION);
		dbg_exception.bs = 1;
		hv::vmwrite(GUEST_PENDING_DEBUG_EXCEPTION, dbg_exception.flags);
	}
}

//调整rip指向下一条指令
void adjust_rip(__vcpu* vcpu)
{
	//skip_instruction();
	hv::vmwrite(GUEST_RIP, vcpu->vmexit_info.guest_rip + vcpu->vmexit_info.instruction_length);
	if (vcpu->vmexit_info.guest_rflags.trap_flag)  //判断guest是否开启单步
	{
		__vmx_pending_debug_exceptions pending_debug = { hv::vmread(GUEST_PENDING_DEBUG_EXCEPTION) };
		__vmx_interruptibility_state interruptibility = { hv::vmread(GUEST_INTERRUPTIBILITY_STATE) };

		pending_debug.bs = true;  //开启单步
		hv::vmwrite(GUEST_PENDING_DEBUG_EXCEPTION, pending_debug.all);

		//如果guest里开启单步 则我们必须将阻塞取消
		interruptibility.blocking_by_sti = false;
		interruptibility.blocking_by_mov_ss = false;
		hv::vmwrite(GUEST_INTERRUPTIBILITY_STATE, interruptibility.all);
	}
}

void vmexit_unimplemented(__vcpu* vcpu)
{
	hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
}

void vmexit_nmi_window_handler(__vcpu* vcpu)
{
	--vcpu->queued_nmis;

	// inject the NMI into the guest
	// 注入nmi到guest
	hv::inject_nmi();

	if (vcpu->queued_nmis == 0) {
		// disable NMI-window exiting since we have no more NMIs to inject
		auto ctrl = hv::read_ctrl_proc_based();
		ctrl.nmi_window_exiting = 0;
		hv::write_ctrl_proc_based(ctrl);
	}

	// there is the possibility that a host NMI occurred right before we
	// disabled NMI-window exiting. make sure to re-enable it if this is the case.
	if (vcpu->queued_nmis > 0) {
		auto ctrl = hv::read_ctrl_proc_based();
		ctrl.nmi_window_exiting = 1;
		hv::write_ctrl_proc_based(ctrl);
	}
}

void CleanUp(__vcpu* vcpu, __ept_hooked_page_info* hooked_page_info)
{
	//别忘了扫尾工作
	hooked_page_info->ID = -1;
	hooked_page_info->isBp = false;
	hooked_page_info->isInt3 = false;
	vcpu->ept_state->page_to_change = nullptr;
}

void vmexit_monitor_trap_flag_handler(__vcpu* vcpu)
{	
	hv::set_mtf(false);  //关闭mtf

	//切记不可滥用page_to_change
	//page_to_change只有在触发ept违例时才会被记录，
	//当这个变量为真时，说明我们的mtf是主动由ept违例里开启的，
	//是我们希望处理的地址，记得用完后要将page_to_change清空
	const auto hooked_page_info = vcpu->ept_state->page_to_change;
	if (hooked_page_info)
	{
		if (hooked_page_info->Options == EPTO_HOOK_FUNCTION)
		{
			//将原页修改为不可执行，从而再次触发ept违例进host换回伪页
			hooked_page_info->original_entry.execute = 0;
			vcpu->ept_state->page_to_change = nullptr;
			ept::swap_pml1_and_invalidate_tlb(*vcpu->ept_state,
				hooked_page_info->entry_address,
				hooked_page_info->original_entry,
				invept_type::invept_single_context);
		}

		if (hooked_page_info->Options == EPTO_VIRTUAL_BREAKPOINT)
		{
			spinlock::lock(&eptWatchList_lock);
			int ID = hooked_page_info->ID;		
			if ((ID == -1) || (eptWatchList[ID].inuse == 0))
			{
				CleanUp(vcpu, hooked_page_info);
				spinlock::unlock(&eptWatchList_lock);
				return;
			}

			if (hooked_page_info->isInt3) //int3
			{
				cr3 guest_cr3;
				guest_cr3.flags = eptWatchList[ID].cr3;
				//同步原页内容到伪页
				if (PAGE_SIZE != hv::read_guest_virtual_memory(guest_cr3, PAGE_ALIGN(eptWatchList[ID].VirtualAddress), &hooked_page_info->fake_page_contents, PAGE_SIZE))
				{
					//读取数据可能不完整
					CleanUp(vcpu, hooked_page_info);
					spinlock::unlock(&eptWatchList_lock);
					return;
				}

				//同步原页内容后，可能会将我们伪页的cc断点覆盖掉
				//故我们需要再次找出这些断点位置，重新设置上cc断点
				for (int i = 0; i < EPTWATCHLISTSIZE; i++)
				{
					//找出与eptWatchList[ID].PhysicalAddress同一页的所有id
					if (ept::ept_isWatchPage(GET_PFN(eptWatchList[ID].PhysicalAddress), i))
					{
						if (eptWatchList[i].bpType == 3)
						{
							//写入cc断点
							int offset = eptWatchList[i].VirtualAddress & 0xFFF;
							hooked_page_info->fake_page_contents[offset] = 0xCC;
						}
					}
				}

				//将原页修改为不可执行，从而再次触发ept违例进host换回伪页
				hooked_page_info->original_entry.execute = 0;
				ept::swap_pml1_and_invalidate_tlb(*vcpu->ept_state, 
					hooked_page_info->entry_address,
					hooked_page_info->original_entry, 
					invept_type::invept_single_context);		
			}
			else
			{
				switch (eptWatchList[ID].Type)
				{
				case EPTW_WRITE:
				{
					//重新设置为不可写
					hooked_page_info->entry_address->write = 0;

					//判断是否是我们监视的地址，如果是的话就向Guest注入单步异常
					if (hooked_page_info->isBp)
					{
						hv::inject_single_step(vcpu);
					}
					break;
				}
				//case EPTW_READ:
				//{
				//	//重新设置为不可读
				//	hooked_page_info->entry_address->read = 0;
				//	break;
				//}
				case EPTW_READWRITE:
				{
					//重新设置为不可读写
					hooked_page_info->entry_address->read = 0;
					hooked_page_info->entry_address->write = 0;
					//判断是否是我们监视的地址，如果是的话就向Guest注入单步异常
					if (hooked_page_info->isBp)
					{
						hv::inject_single_step(vcpu);
					}
					break;
				}
				case EPTW_EXECUTE:
				{
					//重新设置不可执行
					hooked_page_info->entry_address->execute = 0;
					//判断是否是我们监视的地址，如果是的话就向Guest注入单步异常
					if (hooked_page_info->isBp)
					{
						hv::inject_single_step(vcpu);
					}
					break;
				}
				default:
					break;
				}				
				//刷新当前逻辑处理器
				invept_single_context_func(vcpu->ept_state->ept_pointer->all);
			}
			CleanUp(vcpu, hooked_page_info);
			spinlock::unlock(&eptWatchList_lock);
		}
	}	
}

//处理int3断点中断
unsigned __int32 handler_breakpoint(__ept_state& ept_state, __vmexit_interrupt_info interrupt_info)
{
	//DbgBreakPoint();
	unsigned __int64 guest_rip = hv::vmread(GUEST_RIP);
	unsigned __int64 physical_address = MmGetPhysicalAddress((PVOID)guest_rip).QuadPart;

	PLIST_ENTRY current = &ept_state.hooked_page_list;
	while (&ept_state.hooked_page_list != current->Flink)
	{
		current = current->Flink;
		//从列表中取出挂钩页
		__ept_hooked_page_info* hooked_page_info = CONTAINING_RECORD(current, __ept_hooked_page_info, hooked_page_list);

		//判断目标地址是否是被挂钩的页
		//仅比较页帧号
		if (hooked_page_info->pfn_of_hooked_page == GET_PFN(physical_address))
		{
			//LogInfo("页面已挂钩");

			//检查虚拟地址是否已经挂钩
			PLIST_ENTRY current_hooked_function = &hooked_page_info->hooked_functions_list;
			while (&hooked_page_info->hooked_functions_list != current_hooked_function->Flink)
			{
				current_hooked_function = current_hooked_function->Flink;
				__ept_hooked_function_info* hooked_function_info = CONTAINING_RECORD(current_hooked_function, __ept_hooked_function_info, hooked_function_list);

				if (hooked_function_info->virtual_address == (PVOID)guest_rip)
				{
					if (hooked_function_info->breakpoint_address == guest_rip)
					{
						return 1;  //注入int3中断
					}
					else
					{
						hv::vmwrite(GUEST_RIP, hooked_function_info->handler_function);
						return 2;  //不注入中断
					}										
				}
			}			
		}		
	}	
	return 1;
}

//处理int1
unsigned __int32 handler_debug(__ept_state& ept_state, __vmexit_interrupt_info interrupt_info)
{
	//DbgBreakPoint();
	unsigned __int64 guest_rip = hv::vmread(GUEST_RIP);
	unsigned __int64 physical_address = MmGetPhysicalAddress((PVOID)guest_rip).QuadPart;

	PLIST_ENTRY current = &ept_state.hooked_page_list;
	while (&ept_state.hooked_page_list != current->Flink)
	{
		current = current->Flink;
		//从列表中取出挂钩页
		__ept_hooked_page_info* hooked_page_info = CONTAINING_RECORD(current, __ept_hooked_page_info, hooked_page_list);

		//判断目标地址是否是被挂钩的页
		//仅比较页帧号
		if (hooked_page_info->pfn_of_hooked_page == GET_PFN(physical_address))
		{
			//LogInfo("页面已挂钩");

			//检查虚拟地址是否已经挂钩
			PLIST_ENTRY current_hooked_function = &hooked_page_info->hooked_functions_list;
			while (&hooked_page_info->hooked_functions_list != current_hooked_function->Flink)
			{
				current_hooked_function = current_hooked_function->Flink;
				__ept_hooked_function_info* hooked_function_info = CONTAINING_RECORD(current_hooked_function, __ept_hooked_function_info, hooked_function_list);

				if (hooked_function_info->virtual_address == (PVOID)guest_rip)
				{
					hv::vmwrite(GUEST_RIP, hooked_function_info->handler_function);
					return 2;  //不注入中断
				}
			}
		}
	}
	return 1;
}

void vmexit_exception_or_nmi_handler(__vcpu* vcpu)
{
	//递增nmi计数
	++vcpu->queued_nmis;

	//只有在“NMI exiting”以及“virtual-NMIs”都为 1 时，“NMI-window exiting”才能被置位。
	//当“NMI-window exiting”为 1 时，在没有blocking by NMI 阻塞的情况下，VM-entry操作完成后将直接引发 VM - exit。
	auto ctrl = hv::read_ctrl_proc_based();
	ctrl.nmi_window_exiting = 1;
	hv::write_ctrl_proc_based(ctrl);
}

void vmexit_ept_misconfiguration_handler(__vcpu* vcpu)
{
	auto const phys = hv::vmread(GUEST_PHYSICAL_ADDRESS);
}


//GUEST_PHYSICAL_ADDRESS字段仅在由EPT violation 或 EPT misconfiguration引起的vmexit时有效
//对于其他情况，这个字段是未定义值。
void vmexit_ept_violation_handler(__vcpu* vcpu)
{
	__ept_violation ept_violation;

	ept_violation.all = vcpu->vmexit_info.qualification;
	unsigned __int64 guest_physical_adddress = hv::vmread(ept_violation.caused_by_translation ?
		GUEST_PHYSICAL_ADDRESS : GUEST_LINEAR_ADDRESS);

	//触发EPT违例后 枚举hook列表 判断是否是由我们设置的hook页引发的
	PLIST_ENTRY current = &vcpu->ept_state->hooked_page_list;
	while (&vcpu->ept_state->hooked_page_list != current->Flink)
	{
		current = current->Flink;
		__ept_hooked_page_info* hooked_page_info = CONTAINING_RECORD(current, __ept_hooked_page_info, hooked_page_list);

		//guest里的物理地址不会改变，还是指向原来的GPA
		//所以在触发读写访问违例时，GUEST_PHYSICAL_ADDRESS还是源pfn_of_hooked_page
		if (hooked_page_info->pfn_of_hooked_page == GET_PFN(guest_physical_adddress))
		{			
			if ((ept_violation.read_access || ept_violation.write_access) && (!ept_violation.ept_readable || !ept_violation.ept_writeable))
			{				
				if (ept_violation.write_access)
				{
					current = current->Flink;
				}

				if (hooked_page_info->Options == EPTO_VIRTUAL_BREAKPOINT)
				{
					//处理断点监视事件的
					int bpType = 0;
					if (ept::ept_handleWatchEvent(vcpu, ept_violation, hooked_page_info, guest_physical_adddress, bpType))
					{
						if (bpType == 3)  //int3
						{
							hv::set_mtf(true);  //开启mtf
							hooked_page_info->isInt3 = true;
							hooked_page_info->original_entry.execute = 1;
							vcpu->ept_state->page_to_change = hooked_page_info;
							ept::swap_pml1_and_invalidate_tlb(*vcpu->ept_state,
								hooked_page_info->entry_address,
								hooked_page_info->original_entry, 
								invept_type::invept_single_context);
						}
						else
						{
							vcpu->ept_state->page_to_change = hooked_page_info;
							//刷新当前逻辑处理器的eptp
							invept_single_context_func(vcpu->ept_state->ept_pointer->all);
						}					
					}
				}

				if (hooked_page_info->Options == EPTO_HOOK_FUNCTION)
				{
					//下面是处理函数hook的
					hv::set_mtf(true);  //开启mtf
					//处理自引用的情况，我们必须将原页可执行属性还原，
					//开启mtf让guest进入单步模式，然后在vmexit_monitor_trap_flag_handler里将伪页换上去。
					hooked_page_info->original_entry.execute = 1;
					vcpu->ept_state->page_to_change = hooked_page_info;
					ept::swap_pml1_and_invalidate_tlb(*vcpu->ept_state, 
						hooked_page_info->entry_address,
						hooked_page_info->original_entry, 
						invept_type::invept_single_context);
				}
			}
			else if (ept_violation.execute_access && (ept_violation.ept_readable || ept_violation.ept_writeable))
			{
				if (hooked_page_info->Options == EPTO_VIRTUAL_BREAKPOINT)
				{
					//交换物理页面并刷新tlb
					ept::swap_pml1_and_invalidate_tlb(*vcpu->ept_state, 
						hooked_page_info->entry_address, 
						hooked_page_info->changed_entry, 
						invept_type::invept_single_context);

					//处理断点监视事件的
					//int bpType = 0;
					//if (ept::ept_handleWatchEvent(vcpu, ept_violation, hooked_page_info, guest_physical_adddress, bpType))
					//{
					//	vcpu->ept_state->page_to_change = hooked_page_info;
					//	//刷新当前逻辑处理器的eptp
					//	invept_single_context_func(vcpu->ept_state->ept_pointer->all);
					//}
				}

				if (hooked_page_info->Options == EPTO_HOOK_FUNCTION)
				{
					//交换物理页面并刷新tlb
					ept::swap_pml1_and_invalidate_tlb(*vcpu->ept_state, 
						hooked_page_info->entry_address,
						hooked_page_info->changed_entry,
						invept_type::invept_single_context);
				}
			}

			break;
		}
	}


	//vmx_exit_qualification_ept_violation qualification;
	//qualification.flags = vcpu->vmexit_info.qualification;

	//// guest physical address that caused the ept-violation
	//// 触发ept违例的实际地址
	//auto const physical_address = hv::vmread(qualification.caused_by_translation ?
	//	GUEST_PHYSICAL_ADDRESS : GUEST_LINEAR_ADDRESS);

	//auto const pte = get_ept_pte(cpu->ept, physical_address);

	//for (auto const& entry : cpu->ept.mmr) {
	//	// ignore pages that aren't being monitored
	//	if (physical_address < (entry.start & ~0xFFFull))
	//		continue;
	//	if (physical_address >= ((entry.start + entry.size + 0xFFF) & ~0xFFFull))
	//		continue;

	//	pte->read_access = 1;
	//	pte->write_access = 1;
	//	pte->execute_access = 1;

	//	auto const is_relevant_mode =
	//		(qualification.read_access && (entry.mode & mmr_memory_mode_r))
	//		|| (qualification.write_access && (entry.mode & mmr_memory_mode_w))
	//		|| (qualification.execute_access && (entry.mode & mmr_memory_mode_x));

	//	if (is_relevant_mode &&
	//		physical_address >= entry.start &&
	//		physical_address < (entry.start + entry.size)) {

	//		char name[16] = {};
	//		current_guest_image_file_name(name);

	//		char mode[4] = "---";
	//		if (qualification.read_access)
	//			mode[0] = 'r';
	//		if (qualification.write_access)
	//			mode[1] = 'w';
	//		if (qualification.execute_access)
	//			mode[2] = 'x';
	//	}

	//	cpu->ept.mmr_mtf_pte = pte;
	//	cpu->ept.mmr_mtf_mode = entry.mode;

	//	enable_monitor_trap_flag();

	//	return;
	//}

	//if (qualification.execute_access &&
	//	(qualification.write_access || qualification.read_access)) {
	//	HV_LOG_ERROR("Invalid EPT access combination. PhysAddr = %p.", physical_address);
	//	inject_hw_exception(general_protection, 0);
	//	return;
	//}

	//auto const hook = find_ept_hook(cpu->ept, physical_address >> 12);

	//if (!hook) {
	//	HV_LOG_ERROR("Failed to find EPT hook. PhysAddr = %p.", physical_address);
	//	inject_hw_exception(general_protection, 0);
	//	return;
	//}

	//if (qualification.execute_access) {
	//	pte->read_access = 0;
	//	pte->write_access = 0;
	//	pte->execute_access = 1;
	//	pte->page_frame_number = hook->exec_pfn;  //伪页
	//}
	//else {
	//	pte->read_access = 1;
	//	pte->write_access = 1;
	//	pte->execute_access = 0;
	//	pte->page_frame_number = hook->orig_pfn;  //原页
	//}
}

bool mov_to_cr3_handler(__vcpu* vcpu, unsigned __int64 guest_cr3)
{
	cr3 new_cr3;
	new_cr3.flags = guest_cr3;

	auto const curr_cr4 = hv::read_effective_guest_cr4();

	bool invalidate_tlb = true;

	//判断pcid是否开启 与 cr3 bit63是否为1
	if (curr_cr4.pcid_enable && (new_cr3.flags & (1ull << 63))) {
		invalidate_tlb = false;
		new_cr3.flags &= ~(1ull << 63);
	}

	// a mask where bits [63:MAXPHYSADDR] are set to 1
	cpuid_eax_80000008 cpuid_80000008;
	__cpuid(reinterpret_cast<int*>(&cpuid_80000008), 0x80000008);
	uint64_t max_phys_addr = cpuid_80000008.eax.number_of_physical_address_bits;

	//应该为所有保留位注入#GP异常
	auto const reserved_mask = ~((1ull << max_phys_addr) - 1);
	if (new_cr3.flags & reserved_mask) {
		hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
		return false;
	}

	// 3.28.4.3.3
	if (invalidate_tlb) {
		invvpid_single_context_except_global_translations(guest_vpid);
	}

	// it is now safe to write the new guest cr3
	hv::vmwrite(GUEST_CR3, new_cr3.flags);
	return true;
}

void emulate_lmsw(__vcpu* vcpu, uint16_t const value)
{
	// 3.25.1.3

	cr0 new_cr0;
	new_cr0.flags = value;

	// update the guest CR0 read shadow
	cr0 shadow_cr0;
	shadow_cr0.flags = hv::vmread(CR0_READ_SHADOW);
	shadow_cr0.protection_enable = new_cr0.protection_enable;
	shadow_cr0.monitor_coprocessor = new_cr0.monitor_coprocessor;
	shadow_cr0.emulate_fpu = new_cr0.emulate_fpu;
	shadow_cr0.task_switched = new_cr0.task_switched;
	hv::vmwrite(CR0_READ_SHADOW, shadow_cr0.flags);

	// update the real guest CR0.
	// we don't have to worry about VMX reserved bits since CR0.PE (the only
	// reserved bit) can't be cleared to 0 by the LMSW instruction while in
	// protected mode.
	cr0 real_cr0;
	real_cr0.flags = hv::vmread(GUEST_CR0);
	real_cr0.protection_enable = new_cr0.protection_enable;
	real_cr0.monitor_coprocessor = new_cr0.monitor_coprocessor;
	real_cr0.emulate_fpu = new_cr0.emulate_fpu;
	real_cr0.task_switched = new_cr0.task_switched;
	hv::vmwrite(GUEST_CR0, real_cr0.flags);
}

//Vol3A[2.5 CONTROL REGISTERS]
bool mov_to_cr0_handler(__vcpu* vcpu, unsigned __int64 guest_cr0)
{
	union
	{
		__cr0 cr0;
		__cr3 cr3;
		__cr4 cr4;
		unsigned __int64 all;
	}cr_registers;

	cr_registers.cr0.all = guest_cr0;

	auto const curr_cr0 = hv::read_effective_guest_cr0();
	auto const curr_cr4 = hv::read_effective_guest_cr4();

	// Any attempt to clear cr0 PG bit cause #GP
	if ((cr_registers.cr0.paging_enable == 0) || 
		(cr_registers.cr0.paging_enable && !cr_registers.cr0.protection_enable)  /*清除PE位设置PG位会导致#GP*/ ||
		(!cr_registers.cr0.cache_disable && cr_registers.cr0.not_write_through)  ||
		(!cr_registers.cr0.write_protect && curr_cr4.control_flow_enforcement_enable) ||
		(cr_registers.cr0.all & 0xFFFFFFFF00000000) /*判断cr0高32位是否为1*/)
	{
		hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
		return false;
	}

	if (cr_registers.cr0.cache_disable != curr_cr0.cache_disable ||
		cr_registers.cr0.not_write_through != curr_cr0.not_write_through) {
		// TODO: should we care about NW?
		if (cr_registers.cr0.cache_disable)
			hv::set_ept_memory_type(*vcpu->ept_state, MEMORY_TYPE_UNCACHEABLE);
		else
			hv::update_ept_memory_type(*vcpu->ept_state);

		invept_all_contexts_func();
	}

	//模拟正常模式下即使对这些位进行设置后也会被写0
	__cr0 cr0;
	cr0.all = cr_registers.cr0.all;
	cr0.reserved_1 = 0;
	cr0.reserved_2 = 0;
	cr0.reserved_3 = 0;
	cr0.extension_type = 1;
	hv::vmwrite(CR0_READ_SHADOW, cr0.all);

	__cr_fixed cr_fixed;
	cr_fixed.all = __readmsr(IA32_VMX_CR0_FIXED0);
	cr0.all |= cr_fixed.split.low;
	cr_fixed.all = __readmsr(IA32_VMX_CR0_FIXED1);
	cr0.all &= cr_fixed.split.low;
	hv::vmwrite(GUEST_CR0, cr0.all);  //固定GUEST_CR0的固定位
	return true;
}

bool mov_to_cr4_handler(unsigned __int64 guest_cr4)
{
	cr4 new_cr4;
	new_cr4.flags = guest_cr4;

	__cr3 guest_cr3;
	guest_cr3.all = hv::vmread(GUEST_CR3);

	//
	// Any attempt to write a 1 to any reserved bit cause #GP or 
	// Trying to leave IA-32e mode by clearing cr pae bit cause #GP
	// Trying to change cr4 pcide from 0 to 1 while cr3[11:0] != 0 cause #GP
	//

	auto const curr_cr0 = hv::read_effective_guest_cr0();
	auto const curr_cr4 = hv::read_effective_guest_cr4();

	if ((new_cr4.reserved1 != 0) ||
		(new_cr4.reserved2 != 0) ||
		(new_cr4.physical_address_extension == 0) ||
		(new_cr4.linear_addresses_57_bit) ||
		(new_cr4.control_flow_enforcement_enable && !curr_cr0.write_protect) ||
		((new_cr4.pcid_enable && !curr_cr4.pcid_enable) && guest_cr3.pcid != 0) ||
		(new_cr4.flags & 0xFFFFFFFF00000000) /*判断cr4高32位是否为1*/)
	{
		hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
		return false;
	}

	// invalidate TLB entries if required
	if (new_cr4.page_global_enable != curr_cr4.page_global_enable ||
		!new_cr4.pcid_enable && curr_cr4.pcid_enable ||
		new_cr4.smep_enable && !curr_cr4.smep_enable)
	{
		invvpid_single_context_func(guest_vpid);
	}

	hv::vmwrite<unsigned __int64>(CR4_READ_SHADOW, new_cr4.flags);

	//guest CR4 VMXE 位（bit 13）必须固定1
	__cr4 cr4;
	__cr_fixed cr_fixed;
	cr_fixed.all = __readmsr(IA32_VMX_CR4_FIXED0);
	cr4.all = new_cr4.flags;
	cr4.all |= cr_fixed.split.low;
	cr_fixed.all = __readmsr(IA32_VMX_CR4_FIXED1);
	cr4.all &= cr_fixed.split.low;
	hv::vmwrite<unsigned __int64>(GUEST_CR4, cr4.all);
	return true;
}

void vmexit_cr_handler(__vcpu* vcpu)
{
	__cr0 guest_cr0;
	__cr3 guest_cr3;

	vmx_exit_qualification_mov_cr qualification;
	qualification.flags = vcpu->vmexit_info.qualification;

	union
	{
		__cr0 cr0;
		__cr3 cr3;
		__cr4 cr4;
		unsigned __int64 all;
	}cr_registers;

	cr_registers.all = hv::read_guest_gpr(vcpu->vmexit_info.guest_registers, qualification.general_purpose_register);
	switch (qualification.access_type)
	{
	case CR_ACCESS_MOV_TO_CR:
	{
		switch (qualification.control_register)
		{
		case 0:
		{
			if (mov_to_cr0_handler(vcpu, cr_registers.cr0.all) == false)
			{
				return;
			}
			break;
		}
		case 3:
		{
			if (mov_to_cr3_handler(vcpu, cr_registers.cr3.all) == false)
			{
				return;
			}
			break;
		}
		case 4:
		{
			if (mov_to_cr4_handler(cr_registers.cr4.all) == false)
			{
				return;
			}
			break;
		}
		}

		break;
	}
	case CR_ACCESS_MOV_FROM_CR:
	{
		switch (qualification.control_register)
		{
		case 3:
		{
			hv::write_guest_gpr(vcpu->vmexit_info.guest_registers, 
				qualification.general_purpose_register, hv::vmread(GUEST_CR3));
			break;
		}
		}
		break;
	}
	case CR_ACCESS_CLTS:
	{
		// clear CR0.TS in the read shadow
		hv::vmwrite<unsigned __int64>(CR0_READ_SHADOW,
			hv::vmread(CR0_READ_SHADOW) & ~CR0_TASK_SWITCHED_FLAG);

		// clear CR0.TS in the real CR0 register
		hv::vmwrite<unsigned __int64>(GUEST_CR0,
			hv::vmread(GUEST_CR0) & ~CR0_TASK_SWITCHED_FLAG);

		break;
	}

	//
	// Loads the source operand into the machine status word, bits 0 through 15 of register CR0.
	// The source operand can be a 16-bit general-purpose register or a memory location. 
	// Only the low-order 4 bits of the source operand (which contains the PE, MP, EM, and TS flags) are loaded into CR0. 
	// The PG, CD, NW, AM, WP, NE, and ET flags of CR0 are not affected. The operand-size attribute has no effect on this instruction.
	// If the PE flag of the source operand(bit 0) is set to 1, the instruction causes the processor to switch to protected mode.
	// While in protected mode, the LMSW instruction cannot be used to clear the PE flagand force a switch back to real - address mode.
	// The LMSW instruction is provided for use in operating - system software; it should not be used in application programs.
	// In protected or virtual - 8086 mode, it can only be executed at CPL 0.
	// This instruction is provided for compatibility with the Intel 286 processor
	// programs and procedures intended to run on IA - 32 and Intel 64 processors beginning with Intel386 processors should use
	// the MOV(control registers) instruction to load the whole CR0 register.The MOV CR0 instruction can be used to set and clear the PE flag
	// in CR0, allowing a procedure or program to switch between protectedand real - address modes.
	//
	case CR_ACCESS_LMSW:
	{
		emulate_lmsw(vcpu, qualification.lmsw_source_data);
		break;
	}
	}

	set_hide_vm_exit_overhead(vcpu, true);
	adjust_rip(vcpu);
}

//VT-x instructions handler
void vmexit_vm_instruction(__vcpu* vcpu)
{
	UNREFERENCED_PARAMETER(vcpu);
	//为每个 VMX 指令注入 #UD，因为我们不允许guest进入 VMX operation
	hv::inject_interruption(EXCEPTION_VECTOR_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, false);
}

//Vol1[5.23 SAFER MODE EXTENSIONS]
//SMX: GETSEC.
void vmexit_getsec_handler(__vcpu* vcpu)
{
	//向guest注入#GP异常,因为我们已经在IA32_FEATURE_CONTROL寄存器中禁用了SMX模式
	hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
}

void vmexit_vmx_on_handler(__vcpu* vcpu)
{
	if (!hv::read_effective_guest_cr4().vmx_enable)
	{
		hv::inject_interruption(EXCEPTION_VECTOR_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, false);
	}
	else
	{
		hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
	}
}

//三重故障处理
void vmexit_triple_fault_handler(__vcpu* vcpu)
{
	hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
}

//处理失败的vmexit
void vmexit_failed(__vcpu* vcpu)
{
	hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
}

//清除cpu缓存,并将数据写回到内存
void vmexit_invd_handler(__vcpu* vcpu)
{
	//hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
	__wbinvd();
	adjust_rip(vcpu);
}

void vmexit_invlpg_handler(__vcpu* vcpu)
{
	invvpid_invidual_address_func(vcpu->vmexit_info.qualification, 1);

	adjust_rip(vcpu);
}

//处理rdtscp指令
void vmexit_rdtscp_handler(__vcpu* vcpu)
{
	//
	// Reads the current value of the processor抯 time-stamp counter (a 64-bit MSR) into the EDX:EAX registers
	// and also reads the value of the IA32_TSC_AUX MSR (address C0000103H) into the ECX register.
	// The EDX register is loaded with the high-order 32 bits of the IA32_TSC MSR; 
	// the EAX register is loaded with the low-order 32 bits of the IA32_TSC MSR; 
	// and the ECX register is loaded with the low-order 32-bits of IA32_TSC_AUX MSR.
	// On processors that support the Intel 64 architecture, the high-order 32 bits of each of RAX, RDX, and RCX are cleared.
	//

	unsigned __int32 processor_id;
	unsigned __int64 tsc = __rdtscp(&processor_id);
	vcpu->vmexit_info.guest_registers->rcx = processor_id;
	vcpu->vmexit_info.guest_registers->rdx = MASK_GET_HIGHER_32BITS(tsc) >> 32;
	vcpu->vmexit_info.guest_registers->rax = MASK_GET_LOWER_32BITS(tsc);

	adjust_rip(vcpu);
}

void vmexit_vmx_preemption_handler(__vcpu* vcpu)
{
	//这里目前什么都不做
}

void vmexit_xsetbv_handler(__vcpu* vcpu)
{
	__xcr0 new_xcr0;
	__xcr0 current_xcr0;

	// CR4.OSXSAVE must be 1否则注入#UD异常
	if (!hv::read_effective_guest_cr4().os_xsave) {
		hv::inject_interruption(EXCEPTION_VECTOR_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, false);
		return;
	}

	// 在支持 Intel 64 架构的处理器上，RCX 的高 32 位将被忽略。
	// guest_registers->rcx & 0xFFFF'FFFF目的是为了忽略高32位保留低32位
	unsigned __int64 xcr_number = vcpu->vmexit_info.guest_registers->rcx & 0xFFFF'FFFF;

	new_xcr0.all = (vcpu->vmexit_info.guest_registers->rdx << 32) | MASK_GET_LOWER_32BITS(vcpu->vmexit_info.guest_registers->rax);

	current_xcr0.all = _xgetbv(0);


	//获取xcr0寄存器中不受支持的位
	cpuid_eax_0d_ecx_00 cpuid_0d;
	__cpuidex(reinterpret_cast<int*>(&cpuid_0d), 0x0D, 0x00);
	auto const unsupported_mask = ~((static_cast<uint64_t>(cpuid_0d.edx.flags) << 32) | cpuid_0d.eax.flags);


	//
	// 如果 xcr_number 大于 0，则注入 #GP
	// If value in edx:eax sets bits that are reserved in the xcr specified by ecx then inject #GP
	// If an attempt is made to clear bit 0 of xcr0 then inject #GP
	// If an attempt is made to set new_xcr0[2:1] = 0 then inject #GP
	// 如果设置了不受支持的位，则注入#GP
	//
	if ((xcr_number != 0) ||
		(new_xcr0.reserved1 != current_xcr0.reserved1) ||
		(new_xcr0.reserved2 != current_xcr0.reserved2) ||
		(new_xcr0.reserved3 != current_xcr0.reserved3) ||
		(new_xcr0.reserved4 != current_xcr0.reserved4) ||
		(new_xcr0.all & unsupported_mask) ||
		(new_xcr0.x87 == 0) ||
		(new_xcr0.sse == 0 && new_xcr0.avx == 1) ||
		(new_xcr0.avx == 0 && (new_xcr0.opmask || new_xcr0.zmm_hi256 || new_xcr0.hi16_zmm)) ||
		(new_xcr0.bndreg != new_xcr0.bndcsr) ||
		(new_xcr0.opmask != new_xcr0.zmm_hi256 || new_xcr0.zmm_hi256 != new_xcr0.hi16_zmm) ||
		(new_xcr0.TILECFG != new_xcr0.TILEDATA)
		)
	{
		hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
		return;
	}

	//
	// 将寄存器 EDX:EAX 的内容写入 ECX 寄存器指定的 64 位扩展控制寄存器 (XCR)。
	// （在支持 Intel 64 架构的处理器上，RCX 的高 32 位将被忽略。）
	// The contents of the EDX register are copied to high-order 32 bits of the selected XCR and the contents of the EAX register are copied
	// to low-order 32 bits of the XCR. (On processors that support the Intel 64 architecture,
	// the high-order 32 bits of each of RAX and RDX are ignored.) Undefined or reserved bits in an XCR should be set to values previously read.
	// This instruction must be executed at privilege level 0 or in real - address mode; otherwise, a general protection exception #GP(0)
	// is generated.Specifying a reserved or unimplemented XCR in ECX will also cause a general protection exception.
	// The processor will also generate a general protection exception if software attempts to write to reserved bits in an XCR.
	// 目前仅支持 XCR0。因此ECX的所有其他值都被保留，设置保留寄存器则触发#GP
	//
	hv::host_exception_info e;
	hv::xsetbv_safe(e, xcr_number, new_xcr0.all);

	//如果xsetbv_safe发生了异常则向guest里注入#GP异常
	if (e.exception_occurred) {
		hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
		return;
	}

	set_hide_vm_exit_overhead(vcpu, true);
	adjust_rip(vcpu);
}

void vmexit_rdtsc_handler(__vcpu* vcpu)
{
	//
	// Loads the current value of the processor's time-stamp counter into the EDX:EAX registers.
	// The time-stamp counter is contained in a 64-bit MSR.
	// The high-order 32 bits of the MSR are loaded into the EDX register, and the low-order 32 bits are loaded into the EAX register.
	// The processor monotonically increments the time-stamp counter MSR every clock cycle and resets it to 0 whenever the processor is reset.
	// See "Time Stamp Counter" in Chapter 15 of the IA-32 Intel Architecture Software Developer's Manual, 
	// Volume 3 for specific details of the time stamp counter behavior.
	//

	unsigned __int64 tsc = __rdtsc();

	vcpu->vmexit_info.guest_registers->rdx = MASK_GET_HIGHER_32BITS(tsc) >> 32;
	vcpu->vmexit_info.guest_registers->rax = MASK_GET_LOWER_32BITS(tsc);

	adjust_rip(vcpu);
}

void vmexit_cpuid_handler(__vcpu* vcpu)
{
	//__cpuid_info cpuid_reg = { 0 };

	//if (vcpu->vmexit_info.guest_registers->rax >= 0x40000000 &&
	//	vcpu->vmexit_info.guest_registers->rax <= 0x4FFFFFFF)
	//	__cpuidex((int*)&cpuid_reg.eax, g_vmm_context.highest_basic_leaf, 0);

	//else
	//	__cpuidex((int*)&cpuid_reg.eax, vcpu->vmexit_info.guest_registers->rax, vcpu->vmexit_info.guest_registers->rcx);


	//switch (vcpu->vmexit_info.guest_registers->rax)
	//{
	//case CPUID_PROCESSOR_FEATURES:
	//{
	//	//处理cpuid 1  ecx bit31
	//	cpuid_reg.cpuid_eax_01.feature_information_ecx.hypervisor_present = 0; // Hypervisor present bit
	//	break;
	//}
	//case CPUID_EXTENDED_FEATURES:
	//	if (vcpu->vmexit_info.guest_registers->rcx == 0)
	//		CLR_CPUID_BIT(cpuid_reg.ecx, 5); // TPAUSE UMONITOR and UWAIT are not supported
	//	break;
	//}

	int regs[4];
	__cpuidex(regs, vcpu->vmexit_info.guest_registers->eax, vcpu->vmexit_info.guest_registers->ecx);

	vcpu->vmexit_info.guest_registers->rax = regs[0];
	vcpu->vmexit_info.guest_registers->rbx = regs[1];
	vcpu->vmexit_info.guest_registers->rcx = regs[2];
	vcpu->vmexit_info.guest_registers->rdx = regs[3];

	set_hide_vm_exit_overhead(vcpu, true);
	adjust_rip(vcpu);
}

/*当“use MSR bitmap”为 1 时，使用 RDMSR 指令读 MSR，但 ECX 寄存器提
供的 MSR 地址值不在 00000000H～00001FFFH 或者 C0000000H～C0001FFFH 范围内，
将导致 VM - exit。

如在 00000000H～00001FFFH 或者 C0000000H～C0001FFFH 范围内，则由MSR bitmap区域来决定
是否触发vmexit

将 ECX 指定的 MSR 读入 EDX:EAX。
*/
void vmexit_msr_read_handler(__vcpu* vcpu)
{
	__msr msr;
	unsigned __int64 msr_index = vcpu->vmexit_info.guest_registers->rcx & 0xFFFF'FFFF;

	switch (msr_index)
	{
	case IA32_FEATURE_CONTROL:
	{
		__ia32_feature_control_msr feature_msr = { 0 };
		feature_msr.all = __readmsr(IA32_FEATURE_CONTROL);
		feature_msr.lock = 1;
		feature_msr.vmxon_inside_smx = 0;
		feature_msr.vmxon_outside_smx = 0; //伪造BIOS没有开启VT  禁用SMX模式
		feature_msr.senter_local = 0;
		feature_msr.senter_global = 0;
		msr.all = feature_msr.all;
		break;
	}
	default:
	{		
		hv::host_exception_info e;
		msr.all = hv::rdmsr_safe(e, msr_index);

		if (e.exception_occurred) {
			// reflect the exception back into the guest
			hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
			return;
		}
		break;
	}
	}

	vcpu->vmexit_info.guest_registers->edx = msr.high;
	vcpu->vmexit_info.guest_registers->eax = msr.low;

	set_hide_vm_exit_overhead(vcpu, true);
	adjust_rip(vcpu);
}

//将EDX:EAX中的值写入ECX指定的MSR。
void vmexit_msr_write_handler(__vcpu* vcpu)
{
	unsigned __int64 msr_index = vcpu->vmexit_info.guest_registers->rcx & 0xFFFF'FFFF;

	__msr msr;
	msr.high = vcpu->vmexit_info.guest_registers->edx;
	msr.low = vcpu->vmexit_info.guest_registers->eax;

	switch (msr_index)
	{
	default:
	{
		hv::host_exception_info e;
		hv::wrmsr_safe(e, msr_index, msr.all);

		if (e.exception_occurred) {
			hv::inject_interruption(EXCEPTION_VECTOR_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0, true);
			return;
		}
		break;
	}
	}

	// we need to make sure to update EPT memory types if the guest
	// modifies any of the MTRR registers
	if (msr_index == IA32_MTRR_DEF_TYPE || msr_index == IA32_MTRR_FIX64K_00000 ||
		msr_index == IA32_MTRR_FIX16K_80000 || msr_index == IA32_MTRR_FIX16K_A0000 ||
		(msr_index >= IA32_MTRR_FIX4K_C0000 && msr_index <= IA32_MTRR_FIX4K_F8000) ||
		(msr_index >= IA32_MTRR_PHYSBASE0 && msr_index <= IA32_MTRR_PHYSBASE0 + 511)) {
		// update EPT memory types
		if (!hv::read_effective_guest_cr0().cache_disable)
			hv::update_ept_memory_type(*vcpu->ept_state);

		invept_all_contexts_func();
	}

	set_hide_vm_exit_overhead(vcpu, true);
	adjust_rip(vcpu);
}
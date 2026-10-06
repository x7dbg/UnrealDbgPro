#include "Driver.h"
#include "poolmanager.h"
#include "Globals.h"
#include "segment.h"
#include "exception.h"
#include "msr.h"
#include "AsmCallset.h"
#include "vm_context.h"
#include "mtrr.h"
#include "EPT.h"
#include "vmcs.h"
#include "hypervisor_routines.h"


//源自英特尔手册Voulme 3第24.8.1节表24-13。VM入口控制的定义
void set_entry_control(__vmx_entry_control& entry_control)
{
	/**
	* This control determines whether DR7 and the IA32_DEBUGCTL MSR are loaded on VM entry.
	* The first processors to support the virtual-machine extensions supported only the 1-setting of
	* this control.
	* 当“load debug controls”为 1，在 VM-entry 时将从 guest-state 区域的相应字段中,
	* 加载 DR7 和 IA32_DEBUGCTL 寄存器的值。否则不会加载。
	*/
	entry_control.load_dbg_controls = true;

	/**
	* On processors that support Intel 64 architecture, this control determines whether the logical
	* processor is in IA-32e mode after VM entry. Its value is loaded into IA32_EFER.LMA as part of
	* VM entry. 1
	* This control must be 0 on processors that do not support Intel 64 architecture.
	*/
	entry_control.ia32e_mode_guest = true;

	/**
	* This control determines whether the logical processor is in system-management mode (SMM)
	* after VM entry. This control must be 0 for any VM entry from outside SMM.
	*/
	entry_control.entry_to_smm = false;

	/**
	* If set to 1, the default treatment of SMIs and SMM is in effect after the VM entry (see Section
	* 34.15.7). This control must be 0 for any VM entry from outside SMM.
	*/
	entry_control.deactivate_dual_monitor_treament = false;

	/**
	* This control determines whether the IA32_PERF_GLOBAL_CTRL MSR is loaded on VM entry.
	* 当“load IA32_PERF_GLOBAL_CTRL”为 1 时，表明在 VM-entry 时，
	* 需要从 gueststate 区域的 IA32_PERF_GLOBAL_CTRL 字段中读取值加载到 IA32_PERF_GLOBAL_CTRL 寄存器中
	*/
	entry_control.load_ia32_perf_global_control = true;

	/**
	* This control determines whether the IA32_PAT MSR is loaded on VM entry.
	* 当“load IA32_PAT”为 1 时，表明在 VM-entry 时，需要从 guest-state 区域的
    * IA32_PAT 字段中读取值加载到 IA32_PAT 寄存器中。
	*/
	entry_control.load_ia32_pat = true;

	/**
	* This control determines whether the IA32_EFER MSR is loaded on VM entry.
	*/
	entry_control.load_ia32_efer = false;

	/**
	* This control determines whether the IA32_BNDCFGS MSR is loaded on VM entry.
	*/
	entry_control.load_ia32_bndcfgs = false;

	/**
	* If this control is 1, Intel Processor Trace does not produce a paging information packet (PIP) on
	* a VM entry or a VMCS packet on a VM entry that returns from SMM (see Chapter 35).
	*/
	entry_control.conceal_vmx_from_pt = true;

	/**
	* This control determines whether the IA32_RTIT_CTL MSR is loaded on VM entry.
	*/
	entry_control.load_ia32_rtit_ctl = false;

	/**
	* This control determines whether CET-related MSRs and SPP are loaded on VM entry.
	*/
	entry_control.load_cet_state = false;

	/**
	* This control determines whether CET-related MSRs and SPP are loaded on VM entry.
	*/
	entry_control.load_pkrs = false;
}

//源自英特尔手册Voulme 3第24.7.1节表24-11。VM退出控制的定义
void set_exit_control(__vmx_exit_control& exit_control)
{
	/**
	* This control determines whether DR7 and the IA32_DEBUGCTL MSR are saved on VM exit.
	* The first processors to support the virtual-machine extensions supported only the 1-
	* setting of this control.
	* 当“save debug controls”为 1 值时，在 VM-exit 时，处理器在 guest-state 区域的相应字段中，
	* 保存 DR7 与 IA32_DEBUGCTL 寄存器的值。
	*/
	exit_control.save_dbg_controls = true;

	/**
	* On processors that support Intel 64 architecture, this control determines whether a logical
	* processor is in 64-bit mode after the next VM exit. Its value is loaded into CS.L,
	* IA32_EFER.LME, and IA32_EFER.LMA on every VM exit. 1
	* This control must be 0 on processors that do not support Intel 64 architecture.
	*/
	exit_control.host_address_space_size = true;

	/**
	* This control determines whether the IA32_PERF_GLOBAL_CTRL MSR is loaded on VM exit.
	*/
	exit_control.load_ia32_perf_global_control = true;


	exit_control.acknowledge_interrupt_on_exit = true;

	/**
	* This control determines whether the IA32_PAT MSR is saved on VM exit.
	*/
	exit_control.save_ia32_pat = true;

	/**
	* This control determines whether the IA32_PAT MSR is loaded on VM exit.
	*/
	exit_control.load_ia32_pat = true;

	/**
	* This control determines whether the IA32_EFER MSR is saved on VM exit.
	*/
	exit_control.save_ia32_efer = false;

	/**
	* This control determines whether the IA32_EFER MSR is loaded on VM exit.
	*/
	exit_control.load_ia32_efer = false;

	/**
	* This control determines whether the value of the VMX-preemption timer is saved on
	* VM exit.
	*/
	exit_control.save_vmx_preemption_timer_value = false;

	/**
	* This control determines whether the IA32_BNDCFGS MSR is cleared on VM exit.
	*/
	exit_control.clear_ia32_bndcfgs = false;

	/**
	* If this control is 1, Intel Processor Trace does not produce a paging information packet (PIP)
	* on a VM exit or a VMCS packet on an SMM VM exit (see Chapter 35).
	*/
	exit_control.conceal_vmx_from_pt = true;

	/**
	* This control determines whether the IA32_RTIT_CTL MSR is cleared on VM exit.
	*/
	exit_control.clear_ia32_rtit_ctl = false;

	/**
	* This control determines whether CET-related MSRs and SPP are loaded on VM exit.
	*/
	exit_control.load_cet_state = false;

	/**
	* This control determines whether the IA32_PKRS MSR is loaded on VM exit.
	*/
	exit_control.load_pkrs = false;
}

//源自英特尔手册Voulme 3第24.6.2节表24-6。基于主处理器的VM执行控制的定义
void set_primary_controls(__vmx_primary_processor_based_control& primary_controls)
{
	/**
	* If this control is 1, a VM exit occurs at the beginning of any instruction if RFLAGS.IF = 1 and
	* there are no other blocking of interrupts (see Section 24.4.2).
	*/
	primary_controls.interrupt_window_exiting = false;

	/**
	* This control determines whether executions of RDTSC, executions of RDTSCP, and executions
	* of RDMSR that read from the IA32_TIME_STAMP_COUNTER MSR return a value modified by
	* the TSC offset field (see Section 24.6.5 and Section 25.3).
	*/
	primary_controls.use_tsc_offsetting = false;

	/**
	* This control determines whether executions of HLT cause VM exits.
	*/
	primary_controls.hlt_exiting = false;

	/**
	* This determines whether executions of INVLPG cause VM exits.
	*/

#ifdef _MINIMAL
	primary_controls.invlpg_exiting = false;
#else
	primary_controls.invlpg_exiting = true;
#endif

	/**
	* This control determines whether executions of MWAIT cause VM exits.
	*/
	primary_controls.mwait_exiting = false;

	/**
	* This control determines whether executions of RDPMC cause VM exits.
	*/
	primary_controls.rdpmc_exiting = false;

	/**
	* This control determines whether executions of RDTSC and RDTSCP cause VM exits.
	*/
#ifdef _MINIMAL
	primary_controls.rdtsc_exiting = false;
#else
	primary_controls.rdtsc_exiting = true;
#endif

	/**
	* 当“CR3-load exiting”为 1 时，在 VMX non-root operation 中使用 MOV to CR3 指令
    * 来写 CR3 寄存器时，将根据 CR3-target value 与 CR3-target count 字段的值来决定是否产
    * 生 VM-exit。
	*/
#ifdef _MINIMAL
	primary_controls.cr3_load_exiting = true;
#else
	primary_controls.cr3_load_exiting = true;
#endif

	/**
	* 当“CR3-store exiting”为 1 时，在 VMX non-root operation 中使用 MOV from CR3 指
    * 令读 CR3 寄存器将产生 VM-exit。
	*/
#ifdef _MINIMAL
	primary_controls.cr3_store_exiting = false;
#else
	primary_controls.cr3_store_exiting = true;
#endif

	/**
	* This control determines whether executions of MOV to CR8 cause VM exits.
	*/
	primary_controls.cr8_load_exiting = false;

	/**
	* This control determines whether executions of MOV from CR8 cause VM exits.
	*/
	primary_controls.cr8_store_exiting = false;

	/**
	* Setting this control to 1 enables TPR virtualization and other APIC-virtualization features. See
	* Chapter 29.
	*/
	primary_controls.use_tpr_shadow = false;

	/**
	* If this control is 1, a VM exit occurs at the beginning of any instruction if there is no virtual-
	* NMI blocking (see Section 24.4.2).
	* 只有在“NMI exiting”以及“virtual-NMIs”都为 1 时，“NMI-window exiting”才能被置位。
	* 当“NMI-window exiting”为 1 时，在没有blocking by NMI 阻塞的情况下，VM-entry操作完成后将直接引发 VM - exit。
	*/
	primary_controls.nmi_window_exiting = false;

	/**
	* This control determines whether executions of MOV DR cause VM exits.
	*/
#ifdef _MINIMAL
	primary_controls.mov_dr_exiting = false;
#else
	primary_controls.mov_dr_exiting = true;
#endif

	/**
	* This control determines whether executions of I/O instructions (IN, INS/INSB/INSW/INSD, OUT,
	* and OUTS/OUTSB/OUTSW/OUTSD) cause VM exits.
	*/
	primary_controls.unconditional_io_exiting = false;

	/**
	* This control determines whether I/O bitmaps are used to restrict executions of I/O instructions
	(see Section 24.6.4 and Section 25.1.3).
	For this control, ??means 揹o not use I/O bitmaps?and ??means 搖se I/O bitmaps.?If the I/O
	bitmaps are used, the setting of the 搖nconditional I/O exiting?control is ignored
	*/
#ifdef _MINIMAL
	primary_controls.use_io_bitmaps = false;
#else
	primary_controls.use_io_bitmaps = true;
#endif

	/**
	* If this control is 1, the monitor trap flag debugging feature is enabled. See Section 25.5.2.
	*/
	primary_controls.monitor_trap_flag = false;

	/**
	* This control determines whether MSR bitmaps are used to control execution of the RDMSR
	* and WRMSR instructions (see Section 24.6.9 and Section 25.1.3).
	* 当“use MSR bitmap”为 1 时，使用 RDMSR 指令读 MSR，但 ECX 寄存器提
    * 供的 MSR 地址值不在 00000000H～00001FFFH 或者 C0000000H～C0001FFFH 范围内，
    * 将导致 VM - exit。

    * 如在 00000000H～00001FFFH 或者 C0000000H～C0001FFFH 范围内，则由MSR bitmap区域来决定
    * 是否触发vmexit
	*/
	//监视msr的读写
	primary_controls.use_msr_bitmaps = true;

	/**
	* This control determines whether executions of MONITOR cause VM exits.
	*/
	primary_controls.monitor_exiting = false;

	/**
	* This control determines whether executions of PAUSE cause VM exits.
	*/
	primary_controls.pause_exiting = false;

	/**
	* This control determines whether the secondary processor-based VM-execution controls are
	* used. If this control is 0, the logical processor operates as if all the secondary processor-based
	* VM-execution controls were also 0.
	*/
	primary_controls.active_secondary_controls = true;
}

//源自英特尔手册Voulme 3第24.6.2节表24-7。基于辅助处理器的VM执行控制的定义
void set_secondary_controls(__vmx_secondary_processor_based_control& secondary_controls)
{
	/**
	* If this control is 1, the logical processor treats specially accesses to the page with the APIC-
	* access address. See Section 29.4.
	*/
	secondary_controls.virtualize_apic_accesses = false;

	/**
	* If this control is 1, extended page tables (EPT) are enabled. See Section 28.2.
	*/
	secondary_controls.enable_ept = true;

	/**
	* This control determines whether executions of LGDT, LIDT, LLDT, LTR, SGDT, SIDT, SLDT, and
	* STR cause VM exits.
	*/
#ifdef _MINIMAL
	secondary_controls.descriptor_table_exiting = false;
#else
	secondary_controls.descriptor_table_exiting = true;
#endif

	/**
	* If this control is 0, any execution of RDTSCP causes an invalid-opcode exception (#UD).
	*/
	secondary_controls.enable_rdtscp = true;

	/**
	* If this control is 1, the logical processor treats specially RDMSR and WRMSR to APIC MSRs (in
	* the range 800H?FFH). See Section 29.5.
	*/
	secondary_controls.virtualize_x2apic_mode = false;

	/**
	* If this control is 1, cached translations of linear addresses are associated with a virtual-
	* processor identifier (VPID). See Section 28.1.
	*/
	secondary_controls.enable_vpid = true;

	/**
	* This control determines whether executions of WBINVD cause VM exits.
	*/
#ifdef _MINIMAL
	secondary_controls.wbinvd_exiting = false;
#else
	secondary_controls.wbinvd_exiting = true;
#endif

	/**
	* This control determines whether guest software may run in unpaged protected mode or in real-
	* address mode.
	*/
	secondary_controls.unrestricted_guest = false;

	/**
	* If this control is 1, the logical processor virtualizes certain APIC accesses. See Section 29.4 and
	* Section 29.5.
	*/
	secondary_controls.apic_register_virtualization = false;

	/**
	* This controls enables the evaluation and delivery of pending virtual interrupts as well as the
	* emulation of writes to the APIC registers that control interrupt prioritization.
	*/
	secondary_controls.virtual_interrupt_delivery = false;

	/**
	* This control determines whether a series of executions of PAUSE can cause a VM exit (see
	* Section 24.6.13 and Section 25.1.3).
	*/
	secondary_controls.pause_loop_exiting = false;

	/**
	* This control determines whether executions of RDRAND cause VM exits.
	*/
#ifdef _MINIMAL
	secondary_controls.rdrand_exiting = false;
#else
	secondary_controls.rdrand_exiting = true;
#endif

	/**
	* If this control is 0, any execution of INVPCID causes a #UD.
	*/
	secondary_controls.enable_invpcid = true;

	/**
	* Setting this control to 1 enables use of the VMFUNC instruction in VMX non-root operation. See
	* Section 25.5.6.
	*/
	secondary_controls.enable_vmfunc = false;

	/**
	* If this control is 1, executions of VMREAD and VMWRITE in VMX non-root operation may access
	* a shadow VMCS (instead of causing VM exits). See Section 24.10 and Section 30.3.
	*/
	secondary_controls.vmcs_shadowing = false;

	/**
	* If this control is 1, executions of ENCLS consult the ENCLS-exiting bitmap to determine whether
	* the instruction causes a VM exit. See Section 24.6.16 and Section 25.1.3.
	*/
	secondary_controls.enable_encls_exiting = false;

	/**
	* This control determines whether executions of RDSEED cause VM exits.
	*/
#ifdef _MINIMAL
	secondary_controls.rdseed_exiting = false;
#else
	secondary_controls.rdseed_exiting = true;
#endif

	/**
	* If this control is 1, an access to a guest-physical address that sets an EPT dirty bit first adds an
	* entry to the page-modification log. See Section 28.2.6.
	*/
	secondary_controls.enable_pml = false;

	/**
	* If this control is 1, EPT violations may cause virtualization exceptions (#VE) instead of VM exits.
	* See Section 25.5.7.
	*/
	secondary_controls.ept_violation = false;

	/**
	* If this control is 1, Intel Processor Trace suppresses from PIPs an indication that the processor
	* was in VMX non-root operation and omits a VMCS packet from any PSB+ produced in VMX non-
	* root operation (see Chapter 35).
	*/
	secondary_controls.conceal_vmx_from_pt = true;

	/**
	* If this control is 0, any execution of XSAVES or XRSTORS causes a #UD.
	*/
	secondary_controls.enable_xsaves_xrstors = true;

	/**
	* If this control is 1, EPT execute permissions are based on whether the linear address being
	* accessed is supervisor mode or user mode. See Chapter 28.
	*/
	secondary_controls.mode_based_execute_control_ept = false;

	/**
	* This control determines whether executions of RDTSC, executions of RDTSCP, and executions
	* of RDMSR that read from the IA32_TIME_STAMP_COUNTER MSR return a value modified by the
	* TSC multiplier field (see Section 24.6.5 and Section 25.3).
	*/
	secondary_controls.sub_page_write_permission_for_ept = false;

	/**
	* This control determines whether executions of RDTSC, executions of RDTSCP, and executions
	* of RDMSR that read from the IA32_TIME_STAMP_COUNTER MSR return a value modified by the
	* TSC multiplier field (see Section 24.6.5 and Section 25.3).
	*/
	secondary_controls.intel_pt_uses_guest_physical_address = false;

	/**
	* This control determines whether executions of RDTSC, executions of RDTSCP, and executions
	* of RDMSR that read from the IA32_TIME_STAMP_COUNTER MSR return a value modified by the
	* TSC multiplier field (see Section 24.6.5 and Section 25.3).
	*/
	secondary_controls.use_tsc_scaling = false;

	/**
	* If this control is 0, any execution of TPAUSE, UMONITOR, or UMWAIT causes a #UD.
	*/
	secondary_controls.enable_user_wait_and_pause = true;

	/**
	* If this control is 1, executions of ENCLV consult the ENCLV-exiting bitmap to determine whether
	* the instruction causes a VM exit. See Section 24.6.17 and Section 25.1.3.
	*/
	secondary_controls.enable_enclv_exiting = false;
}

//设置触发vmexit的中断
void set_exception_bitmap(__exception_bitmap& exception_bitmap)
{
	exception_bitmap.divide_error = false;

	exception_bitmap.debug = false;

	exception_bitmap.nmi_interrupt = false;

	exception_bitmap.breakpoint = false;

	exception_bitmap.overflow = false;

	exception_bitmap.bound = false;

	exception_bitmap.invalid_opcode = false;

	exception_bitmap.coprocessor_segment_overrun = false;

	exception_bitmap.invalid_tss = false;

	exception_bitmap.segment_not_present = false;

	exception_bitmap.stack_segment_fault = false;

	exception_bitmap.general_protection = false;

	exception_bitmap.page_fault = false;

	exception_bitmap.floating_point_error = false;

	exception_bitmap.alignment_check = false;

	exception_bitmap.machine_check = false;

	exception_bitmap.simd_floating_point_exception = false;

	exception_bitmap.virtualization_exception = false;

	exception_bitmap.control_protection_exception = false;
}

//源自英特尔手册Voulme 3第24.6.1节表24-5。基于引脚的VM执行控制的定义
void set_pinbased_control_msr(__vmx_pinbased_control_msr& pinbased_controls)
{
	/**
	* If this control is 1, external interrupts cause VM exits. Otherwise, they are delivered normally
	* through the guest interrupt-descriptor table (IDT). If this control is 1, the value of RFLAGS.IF
	* does not affect interrupt blocking.
	*/
	pinbased_controls.external_interrupt_exiting = false;

	/**
	* If this control is 1, non-maskable interrupts (NMIs) cause VM exits. Otherwise, they are
	* delivered normally using descriptor 2 of the IDT. This control also determines interactions
	* between IRET and blocking by NMI (see Section 25.3).
	*/
	pinbased_controls.nmi_exiting = true;

	pinbased_controls.virtual_nmis = true;

	/**
	* If this control is 1, the VMX-preemption timer counts down in VMX non-root operation; see
	* Section 25.5.1. A VM exit occurs when the timer counts down to zero; see Section 25.2.
	* 如果此控制为 1，则 VMX 抢占计时器在 VMX non-root operation中倒计时；
	* 请参阅第 25.5.1 节。当计时器倒计时到零时，会发生 VM exit；请参阅第 25.2 节。
	*/
	pinbased_controls.activate_vmxpreemption_timer = false;

	/**
	* If this control is 1, the processor treats interrupts with the posted-interrupt notification vector
	* (see Section 24.6.8) specially, updating the virtual-APIC page with posted-interrupt requests
	* (see Section 29.6).
	*/
	pinbased_controls.process_posted_interrupts = false;
}

unsigned __int64 ajdust_controls(unsigned __int64 ctl, unsigned __int64 msr)
{
	__msr msr_value = { 0 };
	msr_value.all = __readmsr(msr);
	ctl &= msr_value.high;
	ctl |= msr_value.low;
	return ctl;
}

//从全局描述符表gdt中找到对应的段
//返回段基址
unsigned __int64 get_segment_base(unsigned __int16 selector, unsigned __int8* gdt_base)
{
	__segment_descriptor* segment_descriptor;

	segment_descriptor = (__segment_descriptor*)(gdt_base + (selector & ~0x7));

	unsigned __int64 segment_base = segment_descriptor->base_low | segment_descriptor->base_middle << 16 | segment_descriptor->base_high << 24;

	//判断是否是系统段
	if (segment_descriptor->descriptor_type == SEGMENT_DESCRIPTOR_TYPE_SYSTEM)
		segment_base = (segment_base & MASK_32BITS) | (unsigned __int64)segment_descriptor->base_upper << 32;

	return segment_base;
}

uint64_t segment_base(__pseudo_descriptor64 const& gdtr, unsigned __int16 arg_selector)
{
	segment_selector selector;
	selector.flags = arg_selector;
	// null selector
	if (selector.index == 0)
		return 0;

	// fetch the segment descriptor from the gdtr
	auto const descriptor = reinterpret_cast<segment_descriptor_64*>(gdtr.base_address + static_cast<uint64_t>(selector.index) * 8);

	// 3.3.4.5
	// calculate the segment base address
	auto base_address =
		(uint64_t)descriptor->base_address_low |
		((uint64_t)descriptor->base_address_middle << 16) |
		((uint64_t)descriptor->base_address_high << 24);

	// 3.3.5.2
	// system descriptors are expanded to 16 bytes for ia-32e
	if (descriptor->descriptor_type == SEGMENT_DESCRIPTOR_TYPE_SYSTEM)
		base_address |= (uint64_t)descriptor->base_address_upper << 32;

	return base_address;
}

// calculate a segment's access rights
// 计算段的访问权限
vmx_segment_access_rights segment_access(
	unsigned __int8* gdt_base,
	unsigned __int16 selector)
{
	// fetch the segment descriptor from the gdtr
	auto const descriptor = reinterpret_cast<segment_descriptor_64*>(gdt_base + (selector & ~0x7));

	segment_selector selector2;
	selector2.flags = selector;

	vmx_segment_access_rights access;
	access.flags = 0;

	// 3.24.4.1
	access.type = descriptor->type;
	access.descriptor_type = descriptor->descriptor_type;
	access.descriptor_privilege_level = descriptor->descriptor_privilege_level;
	access.present = descriptor->present;
	access.available_bit = descriptor->system;
	access.long_mode = descriptor->long_mode;
	access.default_big = descriptor->default_big;
	access.granularity = descriptor->granularity;
	access.unusable = (selector2.index == 0);

	return access;
}

// VM-exit 发生时，cpu寄存器的状态将从host-state区字段中加载
void fill_vmcs_host_fields(__vcpu* vcpu)
{
	const unsigned __int8 selector_mask = 7;

	//指向我们host自己的gdt全局描述符表
	//将空描述符加载到ds es fs gs段不会产生异常
	hv::vmwrite<unsigned __int64>(HOST_CS_SELECTOR, hv::host_cs_selector.flags);
	hv::vmwrite<unsigned __int64>(HOST_SS_SELECTOR, 0);
	hv::vmwrite<unsigned __int64>(HOST_DS_SELECTOR, 0);
	hv::vmwrite<unsigned __int64>(HOST_ES_SELECTOR, 0);
	hv::vmwrite<unsigned __int64>(HOST_FS_SELECTOR, 0);
	hv::vmwrite<unsigned __int64>(HOST_GS_SELECTOR, 0);
	hv::vmwrite<unsigned __int64>(HOST_TR_SELECTOR, hv::host_tr_selector.flags);

	//寄存器base
	hv::vmwrite<unsigned __int64>(HOST_FS_BASE, reinterpret_cast<size_t>(vcpu));
	hv::vmwrite<unsigned __int64>(HOST_GS_BASE, 0);
	hv::vmwrite<unsigned __int64>(HOST_TR_BASE, reinterpret_cast<size_t>(&vcpu->host_tss));
	hv::vmwrite<unsigned __int64>(HOST_GDTR_BASE, reinterpret_cast<size_t>(&vcpu->host_gdt));
	hv::vmwrite<unsigned __int64>(HOST_IDTR_BASE, reinterpret_cast<size_t>(&vcpu->host_idt));

	//控制寄存器
	hv::vmwrite<unsigned __int64>(HOST_CR0, __readcr0());

	cr3 host_cr3;
	host_cr3.flags = 0;
	host_cr3.page_level_cache_disable = 0;
	host_cr3.page_level_write_through = 0;
	host_cr3.address_of_page_directory = MmGetPhysicalAddress(&hv::ghv.host_page_tables.pml4).QuadPart >> 12;
	hv::vmwrite<unsigned __int64>(HOST_CR3, host_cr3.flags);

	cr4 host_cr4;
	host_cr4.flags = __readcr4();

	// 这些标志可能由 Windows 设置，也可能不设置
	host_cr4.fsgsbase_enable = 1;
	host_cr4.os_xsave = 1;
	host_cr4.smap_enable = 0;
	host_cr4.smep_enable = 0;
	hv::vmwrite<unsigned __int64>(HOST_CR4, host_cr4.flags);

	//栈对齐
	auto const rsp = ((reinterpret_cast<size_t>(vcpu->host_stack) + VMM_STACK_SIZE) & ~0b1111ull) - 8;
	hv::vmwrite<unsigned __int64>(HOST_RSP, rsp);
	hv::vmwrite<void*>(HOST_RIP, hv::vm_exit);

	// MSRS Host
	hv::vmwrite<unsigned __int64>(HOST_IA32_SYSENTER_CS, 0);
	hv::vmwrite<unsigned __int64>(HOST_IA32_SYSENTER_ESP, 0);
	hv::vmwrite<unsigned __int64>(HOST_IA32_SYSENTER_EIP, 0);
	hv::vmwrite<unsigned __int64>(HOST_EFER, __readmsr(IA32_EFER));

	//页属性表
	ia32_pat_register host_pat;
	host_pat.flags = 0;
	host_pat.pa0 = MEMORY_TYPE_WRITE_BACK;
	host_pat.pa1 = MEMORY_TYPE_WRITE_THROUGH;
	host_pat.pa2 = MEMORY_TYPE_UNCACHEABLE_MINUS;
	host_pat.pa3 = MEMORY_TYPE_UNCACHEABLE;
	host_pat.pa4 = MEMORY_TYPE_WRITE_BACK;
	host_pat.pa5 = MEMORY_TYPE_WRITE_THROUGH;
	host_pat.pa6 = MEMORY_TYPE_UNCACHEABLE_MINUS;
	host_pat.pa7 = MEMORY_TYPE_UNCACHEABLE;
	hv::vmwrite<unsigned __int64>(HOST_PAT, host_pat.flags);

	// disable every PMC
	hv::vmwrite<unsigned __int64>(HOST_PERF_GLOBAL_CTRL, 0);
}

//填充guest字段
void fill_vmcs_guest_fields(__vcpu* vcpu, void* guest_rsp)
{
	__pseudo_descriptor64 gdtr = { 0 };
	__pseudo_descriptor64 idtr = { 0 };

	__sgdt(&gdtr);
	__sidt(&idtr); //将中断描述符表寄存器 (IDTR) 值读到idtr里

	// Global descriptor table and local one
	hv::vmwrite<unsigned __int64>(GUEST_GDTR_LIMIT, gdtr.limit);
	hv::vmwrite<unsigned __int64>(GUEST_IDTR_LIMIT, idtr.limit);
	hv::vmwrite<unsigned __int64>(GUEST_GDTR_BASE, gdtr.base_address);
	hv::vmwrite<unsigned __int64>(GUEST_IDTR_BASE, idtr.base_address);

	//段选择器
	hv::vmwrite<unsigned __int64>(GUEST_CS_SELECTOR, __read_cs());
	hv::vmwrite<unsigned __int64>(GUEST_SS_SELECTOR, __read_ss());
	hv::vmwrite<unsigned __int64>(GUEST_DS_SELECTOR, __read_ds());
	hv::vmwrite<unsigned __int64>(GUEST_ES_SELECTOR, __read_es());
	hv::vmwrite<unsigned __int64>(GUEST_FS_SELECTOR, __read_fs());
	hv::vmwrite<unsigned __int64>(GUEST_GS_SELECTOR, __read_gs());
	hv::vmwrite<unsigned __int64>(GUEST_TR_SELECTOR, __read_tr());
	hv::vmwrite<unsigned __int64>(GUEST_LDTR_SELECTOR, __read_ldtr());

	//段基址
	//hv::vmwrite<unsigned __int64>(GUEST_CS_BASE, get_segment_base(__read_cs(), (unsigned __int8*)gdtr.base_address));
	//hv::vmwrite<unsigned __int64>(GUEST_SS_BASE, get_segment_base(__read_ss(), (unsigned __int8*)gdtr.base_address));
	//hv::vmwrite<unsigned __int64>(GUEST_DS_BASE, get_segment_base(__read_ds(), (unsigned __int8*)gdtr.base_address));
	//hv::vmwrite<unsigned __int64>(GUEST_ES_BASE, get_segment_base(__read_es(), (unsigned __int8*)gdtr.base_address));
	//hv::vmwrite<unsigned __int64>(GUEST_FS_BASE, __readmsr(IA32_FS_BASE));
	//hv::vmwrite<unsigned __int64>(GUEST_GS_BASE, __readmsr(IA32_GS_BASE));
	//hv::vmwrite<unsigned __int64>(GUEST_TR_BASE, get_segment_base(__read_tr(), (unsigned __int8*)gdtr.base_address));
	//hv::vmwrite<unsigned __int64>(GUEST_LDTR_BASE, get_segment_base(__read_ldtr(), (unsigned __int8*)gdtr.base_address));

	hv::vmwrite<unsigned __int64>(GUEST_CS_BASE, segment_base(gdtr, __read_cs()));
	hv::vmwrite<unsigned __int64>(GUEST_SS_BASE, segment_base(gdtr, __read_ss()));
	hv::vmwrite<unsigned __int64>(GUEST_DS_BASE, segment_base(gdtr, __read_ds()));
	hv::vmwrite<unsigned __int64>(GUEST_ES_BASE, segment_base(gdtr, __read_es()));
	hv::vmwrite<unsigned __int64>(GUEST_FS_BASE, __readmsr(IA32_FS_BASE));
	hv::vmwrite<unsigned __int64>(GUEST_GS_BASE, __readmsr(IA32_GS_BASE));
	hv::vmwrite<unsigned __int64>(GUEST_TR_BASE, segment_base(gdtr, __read_tr()));
	hv::vmwrite<unsigned __int64>(GUEST_LDTR_BASE, segment_base(gdtr, __read_ldtr()));

	//段limit
	hv::vmwrite<unsigned __int64>(GUEST_CS_LIMIT, __segmentlimit(__read_cs()));
	hv::vmwrite<unsigned __int64>(GUEST_SS_LIMIT, __segmentlimit(__read_ss()));
	hv::vmwrite<unsigned __int64>(GUEST_DS_LIMIT, __segmentlimit(__read_ds()));
	hv::vmwrite<unsigned __int64>(GUEST_ES_LIMIT, __segmentlimit(__read_es()));
	hv::vmwrite<unsigned __int64>(GUEST_FS_LIMIT, __segmentlimit(__read_fs()));
	hv::vmwrite<unsigned __int64>(GUEST_GS_LIMIT, __segmentlimit(__read_gs()));
	hv::vmwrite<unsigned __int64>(GUEST_TR_LIMIT, __segmentlimit(__read_tr()));
	hv::vmwrite<unsigned __int64>(GUEST_LDTR_LIMIT, __segmentlimit(__read_ldtr()));

	//段访问权限
	hv::vmwrite<unsigned __int64>(GUEST_CS_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_cs()).flags);
	hv::vmwrite<unsigned __int64>(GUEST_SS_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_ss()).flags);
	hv::vmwrite<unsigned __int64>(GUEST_DS_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_ds()).flags);
	hv::vmwrite<unsigned __int64>(GUEST_ES_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_es()).flags);
	hv::vmwrite<unsigned __int64>(GUEST_FS_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_fs()).flags);
	hv::vmwrite<unsigned __int64>(GUEST_GS_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_gs()).flags);
	hv::vmwrite<unsigned __int64>(GUEST_TR_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_tr()).flags);
	hv::vmwrite<unsigned __int64>(GUEST_LDTR_ACCESS_RIGHTS, segment_access((unsigned __int8*)gdtr.base_address, __read_ldtr()).flags);

	// Cr registers
	hv::vmwrite<unsigned __int64>(GUEST_CR0, __readcr0());
	hv::vmwrite<unsigned __int64>(CR0_READ_SHADOW, __readcr0());
	hv::vmwrite<unsigned __int64>(CR0_GUEST_HOST_MASK, 0xFFFFFFFF'FFFFFFFF);  //监视全部位

	hv::vmwrite<unsigned __int64>(GUEST_CR3, __readcr3());

	/**
	* CR3-target count 字段值指示前 N 个CR3 target value 有效
	* 当写入 CR3 寄存器的值等于这 N 个 CR3-target value 字段的其中一个值时，不会产生 VM-exit。
  * 如果写入 CR3 寄存器的值不匹配这 N 个 CR3-target value 字段中任何一个值或者
    * CR3-target count 字段值为 0 时（N = 0），将产生 VM-exit。
	*/
	hv::vmwrite<unsigned __int64>(CR3_TARGET_COUNT, 1);
	hv::vmwrite<unsigned __int64>(CR3_TARGET_VALUE0, hv::get_system_directory_table_base());  //此处应该过滤system进程的cr3

	hv::vmwrite<unsigned __int64>(GUEST_CR4, __readcr4());
	hv::vmwrite<unsigned __int64>(CR4_READ_SHADOW, __readcr4() & ~0x2000);
	hv::vmwrite<unsigned __int64>(CR4_GUEST_HOST_MASK, 0x2000); // 只监视cr4.VMXE位
	//hv::vmwrite<unsigned __int64>(CR4_GUEST_HOST_MASK, 0xFFFFFFFF'FFFFFFFF); //监视全部位

	// Debug register
	hv::vmwrite<unsigned __int64>(GUEST_DR7, __readdr(7));

	// RFLAGS
	hv::vmwrite<unsigned __int64>(GUEST_RFLAGS, __readeflags());

	// RSP and RIP
	// 在vm-launch.asm文件里进行设置
	hv::vmwrite<void*>(GUEST_RSP, 0);
	//当执行vmlaunch指令时，从何处继续执行guest
	hv::vmwrite<void*>(GUEST_RIP, 0);


	// MSRS Guest
	//hv::vmwrite<unsigned __int64>(GUEST_DEBUG_CONTROL, __readmsr(IA32_DEBUGCTL));
	hv::vmwrite<unsigned __int64>(GUEST_DEBUG_CONTROL, 0);
	hv::vmwrite<unsigned __int64>(GUEST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
	hv::vmwrite<unsigned __int64>(GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
	hv::vmwrite<unsigned __int64>(GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
	hv::vmwrite<unsigned __int64>(GUEST_PAT, __readmsr(IA32_PAT));
	hv::vmwrite<unsigned __int64>(GUEST_PERF_GLOBAL_CONTROL, __readmsr(IA32_PERF_GLOBAL_CTRL));
	hv::vmwrite<unsigned __int64>(GUEST_EFER, __readmsr(IA32_EFER));

	hv::vmwrite<unsigned __int64>(GUEST_ACTIVITY_STATE, vmx_active);
	hv::vmwrite<unsigned __int64>(GUEST_INTERRUPTIBILITY_STATE, 0);  //中断阻塞状态
	hv::vmwrite<unsigned __int64>(GUEST_SM_BASE, 0);
	hv::vmwrite<unsigned __int64>(GUEST_PENDING_DEBUG_EXCEPTION, 0);
	hv::vmwrite<unsigned __int64>(GUEST_VMX_PREEMPTION_TIMER_VALUE, MAXULONG64);
}

//填充vmcs区域
void fill_vmcs(__vcpu* vcpu, void* guest_rsp)
{
	__exception_bitmap exception_bitmap = { 0 };
	__vmx_basic_msr vmx_basic = { 0 };
	__vmx_entry_control entry_controls = { 0 };
	__vmx_exit_control exit_controls = { 0 };
	__vmx_pinbased_control_msr pinbased_controls = { 0 };
	__vmx_primary_processor_based_control primary_controls = { 0 };
	__vmx_secondary_processor_based_control secondary_controls = { 0 };
	unsigned char res = 0;  //操作成功

	vmx_basic.all = __readmsr(IA32_VMX_BASIC);

	set_entry_control(entry_controls);

	set_exit_control(exit_controls);	

	/* VM Execution control fields */
	set_primary_controls(primary_controls);          //主处理器拦截

	set_secondary_controls(secondary_controls);      //辅助处理器拦截

	set_pinbased_control_msr(pinbased_controls);     //引脚拦截

	//
	set_exception_bitmap(exception_bitmap);          //异常拦截	

	//
	//memset(vcpu->vcpu_bitmaps.io_bitmap_a, 0xff, PAGE_SIZE);
	//memset(vcpu->vcpu_bitmaps.io_bitmap_b, 0xff, PAGE_SIZE);

#ifndef _MINIMAL
	memset(vcpu->vcpu_bitmaps.msr_bitmap, 0xff, PAGE_SIZE);
#endif

	hv::set_msr_bitmap(vcpu->msr_bitmap, IA32_FEATURE_CONTROL, true);

	// Hypervisor features
	//VM-execution 控制字段
	hv::vmwrite<unsigned __int64>(PIN_BASED_VM_EXEC_CONTROL, ajdust_controls(pinbased_controls.all, vmx_basic.true_controls ? IA32_VMX_TRUE_PINBASED_CTLS : IA32_VMX_PINBASED_CTLS));
	hv::vmwrite<unsigned __int64>(PRIMARY_PROCESSOR_BASED_VM_EXEC_CONTROL, ajdust_controls(primary_controls.all, vmx_basic.true_controls ? IA32_VMX_TRUE_PROCBASED_CTLS : IA32_VMX_PROCBASED_CTLS));
	hv::vmwrite<unsigned __int64>(SECONDARY_PROCESSOR_BASED_VM_EXEC_CONTROL, ajdust_controls(secondary_controls.all, IA32_VMX_PROCBASED_CTLS2));

	//
	hv::vmwrite<unsigned __int64>(VM_EXIT_CONTROLS, ajdust_controls(exit_controls.all, vmx_basic.true_controls ? IA32_VMX_TRUE_EXIT_CTLS : IA32_VMX_EXIT_CTLS));
	hv::vmwrite<unsigned __int64>(VM_ENTRY_CONTROLS, ajdust_controls(entry_controls.all, vmx_basic.true_controls ? IA32_VMX_TRUE_ENTRY_CTLS : IA32_VMX_ENTRY_CTLS));



	// Intel 推荐这个字段的初始值设为 FFFFFFFF_FFFFFFFFh，这样在非 SMM 双重监控
	// 处理机制下的 VM - entry 可以不检查这个字段，避免由于这个字段的检查无效而造成 VMentry 失败。
	hv::vmwrite<unsigned __int64>(VMCS_LINK_POINTER, MAXULONG64);

	//拦截guest中断
	hv::vmwrite(EXCEPTION_BITMAP, exception_bitmap.all);

	//如果需要所有的#PF 异常都产生 VM-exit，可以设置 PFEC_MASK 与 PFEC_MATCH
	//都为 0 值。而如果希望所有的#PF 都不产生 VM - exit，可以设置 PFEC_MASK 为 0 值，
    //PFEC_MATCH 为 FFFFFFFFH 值
	hv::vmwrite<unsigned int>(PAGE_FAULT_ERROR_CODE_MASK, 0);
	hv::vmwrite<unsigned int>(PAGE_FAULT_ERROR_CODE_MATCH, 0);

	if (primary_controls.use_msr_bitmaps == true)
		hv::vmwrite(MSR_BITMAP_ADDRESS, MmGetPhysicalAddress(&vcpu->msr_bitmap).QuadPart);

	if (primary_controls.use_io_bitmaps == true)
	{
		hv::vmwrite(IO_BITMAP_A_ADDRESS, vcpu->vcpu_bitmaps.io_bitmap_a_physical);
		hv::vmwrite(IO_BITMAP_B_ADDRESS, vcpu->vcpu_bitmaps.io_bitmap_b_physical);
	}

	if (secondary_controls.enable_vpid == true)
		hv::vmwrite(VIRTUAL_PROCESSOR_ID, guest_vpid); //我们只有一个vm所以指定为1即可

	if (secondary_controls.enable_ept == true && secondary_controls.enable_vpid == true)
		hv::vmwrite(EPT_POINTER, vcpu->ept_state->ept_pointer->all);

	hv::vmwrite<unsigned __int64>(VM_EXIT_MSR_LOAD_COUNT, 0);
	hv::vmwrite<unsigned __int64>(VM_EXIT_MSR_LOAD_ADDR, 0);

	hv::vmwrite<unsigned __int64>(VM_ENTRY_INTERRUPTION_INFO_FIELD, 0);
	hv::vmwrite<unsigned __int64>(VM_ENTRY_EXCEPTION_ERROR_CODE, 0);
	hv::vmwrite<unsigned __int64>(VM_ENTRY_INSTRUCTION_LENGTH, 0);
	
	fill_vmcs_guest_fields(vcpu, guest_rsp);	
	fill_vmcs_host_fields(vcpu);
}
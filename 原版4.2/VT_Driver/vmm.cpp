#include "Driver.h"
#include "poolmanager.h"
#include "Globals.h"
#include "cpuid.h"
#include "ntapi.h"
#include "mtrr.h"
#include "EPT.h"
#include "AllocateMem.h"
#include "msr.h"
#include "vmcs.h"
#include "crx.h"
#include "hypervisor_routines.h"
#include "vmm.h"

EXTERN_C void vmx_save_state();

void free_vmm_context()
{
	//if (g_vmm_context != nullptr)
	{
		// POOL MANAGER
		if (g_vmm_context.pool_manager != nullptr)
		{
			pool_manager::uninitialize();
			free_pool(g_vmm_context.pool_manager);
		}

		// VCPU TABLE
		if (g_vmm_context.vcpu != nullptr)
		{
			for (unsigned int i = 0; i < g_vmm_context.processor_count; i++)
			{
				// VCPU
				//if (g_vmm_context.vcpu[i] != nullptr)
				{
					// VCPU VMM STACK
					//if (g_vmm_context.vcpu_table[i]->vmm_stack != nullptr)
					//{
					//	free_pool(g_vmm_context.vcpu_table[i]->vmm_stack);
					//}

					// IO BITMAP A
					if (g_vmm_context.vcpu[i].vcpu_bitmaps.io_bitmap_a != nullptr)
					{
						free_pool(g_vmm_context.vcpu[i].vcpu_bitmaps.io_bitmap_a);
					}

					// IO BITMAP B
					if (g_vmm_context.vcpu[i].vcpu_bitmaps.io_bitmap_b != nullptr)
					{
						free_pool(g_vmm_context.vcpu[i].vcpu_bitmaps.io_bitmap_b);
					}

					// EPT_STATE
					if (g_vmm_context.vcpu[i].ept_state != nullptr)
					{
						// EPT POINTER
						if (g_vmm_context.vcpu[i].ept_state->ept_pointer != nullptr)
						{
							free_pool(g_vmm_context.vcpu[i].ept_state->ept_pointer);
						}
						// EPT PAGE TABLE
						if (g_vmm_context.vcpu[i].ept_state->ept_page_table != nullptr)
						{
							free_pool(g_vmm_context.vcpu[i].ept_state->ept_page_table);
						}

						free_pool(g_vmm_context.vcpu[i].ept_state);
					}

					//free_pool(g_vmm_context.vcpu_table[i]);
				}
			}
			free_pool(g_vmm_context.vcpu);
		}

		//free_pool(g_vmm_context);
	}

	//g_vmm_context = nullptr;
}

//分配g_vmm_context上下文
bool allocate_vmm_context()
{
	__cpuid_info cpuid_reg = { 0 };

	//
	// Allocate virtual cpu context for every logical core
	// 为每个逻辑处理器分配虚拟 CPU 上下文
	//
	//g_vmm_context.processor_count = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	g_vmm_context.processor_count = KeQueryActiveProcessorCount(NULL);
	auto const arr_size = sizeof(__vcpu) * g_vmm_context.processor_count;
	g_vmm_context.vcpu = allocate_pool<__vcpu*>(arr_size);
	if (g_vmm_context.vcpu == nullptr)
	{
		outDebug("vcpu_table could not be allocated");
		return false;
	}
	RtlSecureZeroMemory(g_vmm_context.vcpu, arr_size);

	//
	// Build mtrr map for physcial memory caching informations
	// 构建 mtrr 映射来存储物理内存缓存信息
	//
	ept::build_mtrr_map();

	//提前在guest里分配了内存
	if (pool_manager::initialize() == false)
	{
		outDebug("预分配内存失败!\n");
		return false;
	}

	for (unsigned int iter = 0; iter < g_vmm_context.processor_count; iter++)
	{
		if (init_vcpu(&g_vmm_context.vcpu[iter]) == false)
		{
			outDebug("init_vcpu失败!\n");
			return false;
		}			
	}

	g_vmm_context.hv_presence = true;

	__cpuid((int*)&cpuid_reg.eax, 0);
	g_vmm_context.highest_basic_leaf = cpuid_reg.eax;

	//创建host页表
	//将所有物理内存映射到我们的地址空间
	create_host_page_tables();

	return true;
}

//分配vcpu结构内存
bool init_vcpu(__vcpu* vcpu)
{

	//vcpu->vmm_stack = allocate_pool<void*>(VMM_STACK_SIZE);
	//if (vcpu->vmm_stack == nullptr)
	//{
	//	LogError("vmm stack could not be allocated");
	//	return false;
	//}
	//RtlSecureZeroMemory(vcpu->vmm_stack, VMM_STACK_SIZE);

	vcpu->vcpu_bitmaps.io_bitmap_a = allocate_pool<unsigned __int8*>(PAGE_SIZE);
	if (vcpu->vcpu_bitmaps.io_bitmap_a == nullptr)
	{
		outDebug("io bitmap a could not be allocated");
		return false;
	}
	RtlSecureZeroMemory(vcpu->vcpu_bitmaps.io_bitmap_a, PAGE_SIZE);
	vcpu->vcpu_bitmaps.io_bitmap_a_physical = MmGetPhysicalAddress(vcpu->vcpu_bitmaps.io_bitmap_a).QuadPart;

	vcpu->vcpu_bitmaps.io_bitmap_b = allocate_pool<unsigned __int8*>(PAGE_SIZE);
	if (vcpu->vcpu_bitmaps.io_bitmap_b == nullptr)
	{
		outDebug("io bitmap b could not be allocated");
		return false;
	}
	RtlSecureZeroMemory(vcpu->vcpu_bitmaps.io_bitmap_b, PAGE_SIZE);
	vcpu->vcpu_bitmaps.io_bitmap_b_physical = MmGetPhysicalAddress(vcpu->vcpu_bitmaps.io_bitmap_b).QuadPart;

	//
	// Allocate ept state structure
	//
	vcpu->ept_state = allocate_pool<__ept_state>();
	if (vcpu->ept_state == nullptr)
	{
		outDebug("ept state could not be allocated");
		return false;
	}
	RtlSecureZeroMemory(vcpu->ept_state, sizeof(__ept_state));
	InitializeListHead(&vcpu->ept_state->hooked_page_list);

	RtlSecureZeroMemory(&vcpu->host_tss, sizeof(task_state_segment_64));
	RtlSecureZeroMemory(&vcpu->host_gdt, sizeof(segment_descriptor_32) * HOST_GDT_DESCRIPTOR_COUNT);
	RtlSecureZeroMemory(&vcpu->host_idt, sizeof(segment_descriptor_interrupt_gate_64) * HOST_IDT_DESCRIPTOR_COUNT);

	//
	// Initialize ept structure
	// 初始化 ept 结构
	//
	if (ept::initialize(*vcpu->ept_state) == false)
	{
		outDebug("初始化 ept 结构失败!\n");
		return false;
	}

	outDebug("vcpu entry allocated successfully at %llX", vcpu);

	return true;
}

//分配vmxon区域
bool init_vmxon(__vcpu* vcpu)
{
	//__vmx_basic_msr vmx_basic = { 0 };

	//vmx_basic.all = __readmsr(IA32_VMX_BASIC);

	//if (vmx_basic.vmxon_region_size > PAGE_SIZE)
	//	vcpu->vmxon = allocate_contignous_memory<__vmcs*>(PAGE_SIZE);

	//else
	//	vcpu->vmxon = allocate_contignous_memory<__vmcs*>(vmx_basic.vmxon_region_size);

	//if (vcpu->vmxon == nullptr)
	//{
	//	LogError("vmxon could not be allocated");
	//	return false;
	//}

	//vcpu->vmxon_physical = MmGetPhysicalAddress(vcpu->vmxon).QuadPart;
	//if (vcpu->vmxon_physical == 0)
	//{
	//	LogError("Could not get vmxon physical address");
	//	return false;
	//}

	//RtlSecureZeroMemory(vcpu->vmxon, PAGE_SIZE);
	//vcpu->vmxon->header.all = vmx_basic.vmcs_revision_identifier;
	//vcpu->vmxon->header.shadow_vmcs_indicator = 0;


	//vcpu->vmxon.revision_id = vmx_basic.vmcs_revision_identifier;
	//vcpu->vmxon.must_be_zero = 0;

	//vcpu->vmxon_physical = MmGetPhysicalAddress(&vcpu->vmxon).QuadPart;
	//NT_ASSERT(vcpu->vmxon_physical % 0x1000 == 0);
	//if (vcpu->vmxon_physical == 0)
	//{
	//	LogError("Could not get vmxon physical address");
	//	return false;
	//}

	return true;
}

//分配vmcs区域
bool init_vmcs(__vcpu* vcpu)
{
	//__vmx_basic_msr vmx_basic = { 0 };
	//PHYSICAL_ADDRESS physical_max;

	//vmx_basic.all = __readmsr(IA32_VMX_BASIC);

	//physical_max.QuadPart = ~0ULL;
	//vcpu->vmcs = allocate_contignous_memory<__vmcs*>(PAGE_SIZE);
	//if (vcpu->vmcs == NULL)
	//{
	//	LogError("Vmcs structure could not be allocated");
	//	return false;
	//}

	//vcpu->vmcs_physical = MmGetPhysicalAddress(vcpu->vmcs).QuadPart;
	//if (vcpu->vmcs_physical == NULL)
	//{
	//	LogError("Could not get physical address of vmcs");
	//	return false;
	//}

	//RtlSecureZeroMemory(vcpu->vmcs, PAGE_SIZE);
	//vcpu->vmcs->header.revision_identifier = vmx_basic.vmcs_revision_identifier;

	//// Indicates if it's shadow vmcs or not
	//vcpu->vmcs->header.shadow_vmcs_indicator = 0;

	return true;
}

//调节控制寄存器 cr4 cr0来启用vmx模式
void adjust_control_registers()
{
	__cr4 cr4;
	__cr0 cr0;
	__cr_fixed cr_fixed;

	_disable();
	cr_fixed.all = __readmsr(IA32_VMX_CR0_FIXED0);
	cr0.all = __readcr0();
	cr0.all |= cr_fixed.split.low;
	cr_fixed.all = __readmsr(IA32_VMX_CR0_FIXED1);
	cr0.all &= cr_fixed.split.low;
	__writecr0(cr0.all);
	cr_fixed.all = __readmsr(IA32_VMX_CR4_FIXED0);
	cr4.all = __readcr4();
	cr4.all |= cr_fixed.split.low;
	cr_fixed.all = __readmsr(IA32_VMX_CR4_FIXED1);
	cr4.all &= cr_fixed.split.low;
	__writecr4(cr4.all);
	_enable();

	//设置IA32_FEATURE_CONTROL寄存器的bit0 bit2支持开启vmx模式
	__ia32_feature_control_msr feature_msr = { 0 };
	feature_msr.all = __readmsr(IA32_FEATURE_CONTROL);

	if (feature_msr.lock == 0)
	{
		feature_msr.vmxon_outside_smx = 1;
		feature_msr.lock = 1;

		__writemsr(IA32_FEATURE_CONTROL, feature_msr.all);
	}
}

//初始化逻辑处理器并启动当前vmcs管理的虚拟机
EXTERN_C
void init_logical_processor(void* guest_rsp)
{
}

void init_logical_processor2(unsigned int iter)
{
	//DbgBreakPoint();
	unsigned __int64 processor_number = iter;

	__vcpu* vcpu = &g_vmm_context.vcpu[processor_number];

	//调节控制寄存器 cr4 cr0来启用vmx模式
	adjust_control_registers();

	if (!hv::enter_vmx_operation(vcpu->vmxon))  //进入vmx模式
	{
		LogError("Failed to put vcpu %d into VMX operation.\n", processor_number);
		return;
	}


	if (!hv::load_vmcs_pointer(vcpu->vmcs))
	{
		LogError("load_vmcs_pointer失败.\n", processor_number);
		return;
	}

	//创建host的idt和gdt
	hv::prepare_external_structures(vcpu);
	vcpu->vcpu_status.vmx_on = true;
	LogInfo("vcpu %d is now in VMX operation.\n", processor_number);

	//配置vmcs区域
	fill_vmcs(vcpu, 0);
	vcpu->vcpu_status.vmm_launched = true;

	//从GUEST_RIP指定的位置继续执行
	//运行vm虚拟机	
	if (!hv::vm_launch()) {
		vcpu->vmexit_info.instruction_error = hv::vmread(VM_INSTRUCTION_ERROR);
		LogError("Vmlaunch failed error: %d", vcpu->vmexit_info.instruction_error);
		vcpu->vcpu_status.vmm_launched = false;
		vcpu->vcpu_status.vmx_on = false;
		__vmx_off();  //退出vmx模式
	}
}

//创建host页表
void create_host_page_tables()
{
	PEPROCESS Process = NULL;


	for (Process = PsGetNextProcess(NULL);
		Process != NULL;
		Process = PsGetNextProcess(Process))
	{
		WCHAR SubStr[256] = { 0 };
		UNICODE_STRING ImageFileName, targetImage;
		NTSTATUS Status = GetProcessName(Process, &SubStr[0]);		
		if (NT_SUCCESS(Status))
		{
			RtlInitUnicodeString(&ImageFileName, SubStr);
			RtlInitUnicodeString(&targetImage, L"dwm.exe");
			if (RtlEqualUnicodeString(&ImageFileName, &targetImage, TRUE))
			{
				hv::ghv.system_cr3.flags = ((__nt_kprocess*)Process)->DirectoryTableBase;

				KAPC_STATE ApcState;
				KeStackAttachProcess(Process, &ApcState);
				hv::prepare_host_page_tables();
				KeUnstackDetachProcess(&ApcState);
				break;
			}
		}
	}

	//hv::ghv.system_cr3.flags = hv::get_system_directory_table_base();
	//hv::prepare_host_page_tables();
}

bool initalize_vcpu(unsigned int iter)
{
	init_logical_processor2(iter);

	return true;
}

//初始化vmm 并运行
bool vmm_init()
{	

	//分配vmm上下文
	if (allocate_vmm_context() == false)
	{
		outDebug("分配vmm上下文失败.\n");
		return false;
	}		

	//我们需要在低于 DISPATCH_LEVEL 的 IRQL 下运行，以便 KeSetSystemAffinityThreadEx 立即生效
	NT_ASSERT(KeGetCurrentIrql() <= APC_LEVEL);	

	// virtualize every cpu
	for (unsigned int iter = 0; iter < g_vmm_context.processor_count; iter++)
	{
		// restrict execution to the specified cpu
		auto const orig_affinity = KeSetSystemAffinityThreadEx(1ull << iter);

		if (!initalize_vcpu(iter)) {
			// TODO: handle this bruh -_-
			KeRevertToUserAffinityThreadEx(orig_affinity);
			outDebug("initalize_vcpu失败.\n");
			return false;
		}

		KeRevertToUserAffinityThreadEx(orig_affinity);
	}
	return true;
}
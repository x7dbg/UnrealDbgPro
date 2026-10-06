#include "Driver.h"
#include "poolmanager.h"
#include "Globals.h"
#include "mtrr.h"
#include "EPT.h"
#include "msr.h"
#include "spinlock.h"
#include "AllocateMem.h"
#include "vmx.h"
#include "hypervisor_routines.h"
#include "vmcs.h"

EXTERN_C size_t __fastcall LDE(const void* lpData, unsigned int size);

namespace ept
{
	/// <summary>
	/// Build mtrr map to track physical memory type
	/// 构建mtrr映射以跟踪物理内存类型(又称缓存类型)
	/// 首先，您需要了解MTRR（Memory Type Range Registers）是什么。
	/// MTRR是一组寄存器，用于定义物理内存地址范围的缓存类型（例如回写缓存WB、直写缓存WT等）。
	/// 然后，您需要获取系统中的MTRR配置信息。可以通过读取和解析系统的MTRR寄存器来获取这些信息。
	/// 具体的方法可能因操作系统和硬件平台而异。	
	/// 解析MTRR配置信息并构建MTRR映射。您可以将MTRR配置信息转换为易于理解的格式，例如使用数据结构或表格。
	/// 根据MTRR的设置，将物理内存地址范围映射到相应的缓存类型。		
	/// </summary>
	void build_mtrr_map()
	{
		__mtrr_cap_reg mtrr_cap = { 0 };
		__mtrr_physbase_reg current_phys_base = { 0 };
		__mtrr_physmask_reg current_phys_mask = { 0 };
		__mtrr_def_type mtrr_def_type = { 0 };
		__mtrr_range_descriptor* descriptor;

		//
		// 内存类型范围寄存器 (MTRR) 提供了一种关联物理内存类型的机制
		// 用于指定物理内存的缓存类型，提升CPU性能。

		mtrr_cap.all = __readmsr(IA32_MTRRCAP);

		//对于没有被MTRR寄存器涵盖的物理内存区域，使用IA32_MTRR_DEF_TYPE寄存器来指定其默认属性
		mtrr_def_type.all = __readmsr(IA32_MTRR_DEF_TYPE);

		if (mtrr_def_type.mtrr_enabled == false)
		{
			// 不可缓存
			// MTRRs被禁用这意味着所有的物理内存都将被视为UC
			g_vmm_context.mtrr_info.default_memory_type = MEMORY_TYPE_UNCACHEABLE;
			return;
		}

		g_vmm_context.mtrr_info.default_memory_type = mtrr_def_type.memory_type;

		//判断处理器是否支持smrr
		if (mtrr_cap.smrr_support == true)
		{
			current_phys_base.all = __readmsr(IA32_SMRR_PHYSBASE);
			current_phys_mask.all = __readmsr(IA32_SMRR_PHYSMASK);

			if (current_phys_mask.valid && current_phys_base.type != mtrr_def_type.memory_type)
			{
				descriptor = &g_vmm_context.mtrr_info.memory_range[g_vmm_context.mtrr_info.enabled_memory_ranges++];
				descriptor->physcial_base_address = current_phys_base.physbase << PAGE_SHIFT;

				unsigned long bits_in_mask = 0;
				_BitScanForward64(&bits_in_mask, current_phys_mask.physmask << PAGE_SHIFT);

				descriptor->physcial_end_address = descriptor->physcial_base_address + ((1ULL << bits_in_mask) - 1ULL);
				descriptor->memory_type = (unsigned __int8)current_phys_base.type;
				descriptor->fixed_range = false;
			}
		}

		//判断处理器是否支持固定范围MTRR
		//MTRR 机制允许在物理内存中定义多个范围，并定义了一组(MSR)寄存器，用于指定每个范围中包含的内存类型
		//固定内存范围映射为 11 个固定范围寄存器，每个寄存器 64 位。每个寄存器分为 8 个字段，用于指定寄存器控制的每个子范围的内存类型：
		if (mtrr_cap.fixed_range_support == true && mtrr_def_type.fixed_range_mtrr_enabled)
		{
			constexpr auto k64_base = 0x0;
			constexpr auto k64_size = 0x10000; //64KB
			constexpr auto k16_base = 0x80000;
			constexpr auto k16_size = 0x4000; //16KB
			constexpr auto k4_base = 0xC0000;
			constexpr auto k4_size = 0x1000; //4KB

			//寄存器 IA32_MTRR_FIX64K_00000 — 映射 512 KB 地址范围，从 0H 到 7FFFFH。此范围分为8个 64 KB 子范围。
			__mtrr_fixed_range_type k64_types = { __readmsr(IA32_MTRR_FIX64K_00000) };

			for (unsigned int i = 0; i < 8; i++)
			{
				descriptor = &g_vmm_context.mtrr_info.memory_range[g_vmm_context.mtrr_info.enabled_memory_ranges++];
				descriptor->memory_type = k64_types.types[i];
				descriptor->physcial_base_address = k64_base + (k64_size * i);
				descriptor->physcial_end_address = k64_base + (k64_size * i) + (k64_size - 1);
				descriptor->fixed_range = true;
			}

			//寄存器 IA32_MTRR_FIX16K_80000 和 IA32_MTRR_FIX16K_A0000 — 映射两个 128 KB 地址范围，从 80000H 到 BFFFFH。
			//每个寄存器 8 个范围。
			for (unsigned int i = 0; i < 2; i++)
			{
				__mtrr_fixed_range_type k16_types = { __readmsr(IA32_MTRR_FIX16K_80000 + i) };

				for (unsigned int j = 0; j < 8; j++)
				{
					descriptor = &g_vmm_context.mtrr_info.memory_range[g_vmm_context.mtrr_info.enabled_memory_ranges++];
					descriptor->memory_type = k16_types.types[j];
					descriptor->physcial_base_address = (k16_base + (i * k16_size * 8)) + (k16_size * j);
					descriptor->physcial_end_address = (k16_base + (i * k16_size * 8)) + (k16_size * j) + (k16_size - 1);
					descriptor->fixed_range = true;
				}
			}

			//寄存器 IA32_MTRR_FIX4K_C0000 至 IA32_MTRR_FIX4K_F8000 — 映射 8 个 32 KB 地址范围，
			//从 C0000H 到 FFFFFH。此范围分为 64 个 4 KB 子范围，每个寄存器 8 个范围。
			for (unsigned int i = 0; i < 8; i++)
			{
				__mtrr_fixed_range_type k4_types = { __readmsr(IA32_MTRR_FIX4K_C0000 + i) };

				for (unsigned int j = 0; j < 8; j++)
				{
					descriptor = &g_vmm_context.mtrr_info.memory_range[g_vmm_context.mtrr_info.enabled_memory_ranges++];
					descriptor->memory_type = k4_types.types[j];
					descriptor->physcial_base_address = (k4_base + (i * k4_size * 8)) + (k4_size * j);
					descriptor->physcial_end_address = (k4_base + (i * k4_size * 8)) + (k4_size * j) + (k4_size - 1);
					descriptor->fixed_range = true;
				}
			}
		}


		//Indicates the number of variable ranges
		//implemented on the processor.
		//处理器中可变MTRRs寄存器的数量。
		//Pentium 4、Intel Xeon 和 P6 系列处理器允许软件为 m 个可变大小地址范围指定内存类型，每个范围使用一对 MTRR。
		//支持的范围数 m 在 IA32_MTRRCAP MSR 的位 7:0 中给出
		for (int i = 0; i < mtrr_cap.range_register_number; i++)
		{
			// 每对中的第一个条目（IA32_MTRR_PHYSBASEn）定义范围的基地址和内存类型；
			//
			current_phys_base.all = __readmsr(IA32_MTRR_PHYSBASE0 + (i * 2));
			current_phys_mask.all = __readmsr(IA32_MTRR_PHYSMASK0 + (i * 2));

			//
			// If range is enabled
			// 如果启用范围
			if (current_phys_mask.valid && current_phys_base.type != mtrr_def_type.memory_type)
			{
				descriptor = &g_vmm_context.mtrr_info.memory_range[g_vmm_context.mtrr_info.enabled_memory_ranges++];

				//
				// Calculate base address, physbase is truncated by 12 bits so we have to left shift it by 12
				// 计算基址，physbase 被截断了 12 位，因此我们必须将其左移 12
				//
				descriptor->physcial_base_address = current_phys_base.physbase << PAGE_SHIFT;

				//
				// Index of first bit set to one determines how much do we have to bit shift to get size of range
				// physmask is truncated by 12 bits so we have to left shift it by 12
				// 第一个设置为 1 的位的索引决定了我们需要移位多少位才能得到范围 physmask 的大小，
				// 它被截断了 12 位，所以我们必须将其左移 12 位
				//
				unsigned long bits_in_mask = 0;
				_BitScanForward64(&bits_in_mask, current_phys_mask.physmask << PAGE_SHIFT);

				//
				// Calculate the end of range specified by mtrr
				// 计算 mtrr 指定的范围的结束位置
				//
				descriptor->physcial_end_address = descriptor->physcial_base_address + ((1ULL << bits_in_mask) - 1ULL);

				//
				// Get memory type of range
				// 获取范围的内存类型
				//
				descriptor->memory_type = (unsigned __int8)current_phys_base.type;
				descriptor->fixed_range = false;
			}
		}
	}

	/// <summary>
	/// Get page cache memory type
	/// 获取页缓存内存类型
	/// </summary>
	/// <param name="pfn"></param>
	/// <param name="is_large_page"></param>  是大页面
	/// <returns></returns>
	unsigned __int8 get_memory_type(unsigned __int64 pfn, bool is_large_page)
	{
		unsigned __int64 page_start_address = is_large_page == true ? pfn * LARGE_PAGE_SIZE : pfn * PAGE_SIZE;
		unsigned __int64 page_end_address = is_large_page == true ? (pfn * LARGE_PAGE_SIZE) + (LARGE_PAGE_SIZE - 1) : (pfn * PAGE_SIZE) + (PAGE_SIZE - 1);

		//未被 MTRR 映射的地址范围应设置为默认类型
		unsigned __int8 memory_type = g_vmm_context.mtrr_info.default_memory_type;

		//在MTRR中寻找给定地址的内存类型
		for (unsigned int i = 0; i < g_vmm_context.mtrr_info.enabled_memory_ranges; i++)
		{
			if (page_start_address >= g_vmm_context.mtrr_info.memory_range[i].physcial_base_address &&
				page_end_address <= g_vmm_context.mtrr_info.memory_range[i].physcial_end_address)
			{
				memory_type = g_vmm_context.mtrr_info.memory_range[i].memory_type;

				//固定范围
				if (g_vmm_context.mtrr_info.memory_range[i].fixed_range == true)
					break;

				if (memory_type == MEMORY_TYPE_UNCACHEABLE)  //不可缓存
					break;
			}
		}

		return memory_type;
	}

	/// <summary>
	/// Check if potential large page doesn't land on two or more different cache memory types
	/// </summary>
	/// <param name="pfn"></param>
	/// <returns></returns>
	bool is_valid_for_large_page(unsigned __int64 pfn)
	{
		unsigned __int64 page_start_address = pfn * LARGE_PAGE_SIZE;
		unsigned __int64 page_end_address = (pfn * LARGE_PAGE_SIZE) + (LARGE_PAGE_SIZE - 1);

		for (unsigned int i = 0; i < g_vmm_context.mtrr_info.enabled_memory_ranges; i++)
		{
			if (page_start_address <= g_vmm_context.mtrr_info.memory_range[i].physcial_end_address &&
				page_end_address > g_vmm_context.mtrr_info.memory_range[i].physcial_end_address)
				return false;

			else if (page_start_address < g_vmm_context.mtrr_info.memory_range[i].physcial_base_address &&
				page_end_address >= g_vmm_context.mtrr_info.memory_range[i].physcial_base_address)
				return false;
		}

		return true;
	}

	/// <summary> 
	/// Setup page memory type
	/// 设置页内存类型
	/// </summary>
	/// <param name="entry"> Pointer to pml2 entry </param>
	/// <param name="pfn"> Page frame number </param>
	bool setup_pml2_entry(mtrr_data const& mtrrs, __ept_state& ept_state, __ept_pde& entry, unsigned __int64 pfn)
	{
		entry.page_directory_entry.page_frame_number = pfn;

		//判断是否是大页面
		if (is_valid_for_large_page(pfn) == true)
		{
			entry.page_directory_entry.memory_type = get_memory_type(pfn, true);
			//entry.page_directory_entry.memory_type = hv::calc_mtrr_mem_type(mtrrs, pfn << 21, 0x1000 << 9);
			return true;
		}
		else
		{
			//不是大页面则 说明是PT页表 
			//分配PT页表
			void* split_buffer = pool_manager::request_pool<void*>(pool_manager::INTENTION_SPLIT_PML2, true, sizeof(__ept_dynamic_split));
			if (split_buffer == nullptr)
			{
				LogError("Failed to allocate split buffer");
				return false;
			}

			//将2MB页面分割为512个4KB页面
			return split_pml2(mtrrs, ept_state, split_buffer, pfn * LARGE_PAGE_SIZE);
		}
	}

	/// <summary>
	/// Create ept page table
	/// 创建ept页表
	/// </summary>
	/// <returns> status </returns>
	bool create_ept_page_table(__ept_state& ept_state)
	{
		PHYSICAL_ADDRESS max_size;
		max_size.QuadPart = MAXULONG64;

		ept_state.ept_page_table = allocate_pool<__vmm_ept_page_table>();
		if (ept_state.ept_page_table == NULL)
		{
			outDebug("Failed to allocate memory for PageTable.\n");
			return false;
		}

		__vmm_ept_page_table* page_table = ept_state.ept_page_table;
		RtlSecureZeroMemory(page_table, sizeof(__vmm_ept_page_table));

		//
		// Set all pages as rwx to prevent unwanted ept violation
		// 将所有页面设置为 rwx，以防止不必要的 ept 违规
		// 设置第一个 PML4E，使其指向我们的 PDPT
		//
		page_table->pml4[0].page_frame_number = GET_PFN(MmGetPhysicalAddress(&page_table->pml3[0]).QuadPart);
		page_table->pml4[0].read = 1;
		page_table->pml4[0].write = 1;
		page_table->pml4[0].execute = 1;
		page_table->pml4[0].execute_for_usermode = 0;

		__ept_pdpte pdpte_template = { 0 };

		pdpte_template.read = 1;
		pdpte_template.write = 1;
		pdpte_template.execute = 1;
		pdpte_template.execute_for_usermode = 0;

		__stosq((unsigned __int64*)&page_table->pml3[0], pdpte_template.all, 512);

		for (int i = 0; i < 512; i++)
			page_table->pml3[i].page_frame_number = GET_PFN(MmGetPhysicalAddress(&page_table->pml2[i][0]).QuadPart);

		__ept_pde pde_template = { 0 };

		pde_template.page_directory_entry.read = 1;
		pde_template.page_directory_entry.write = 1;
		pde_template.page_directory_entry.execute = 1;

		pde_template.page_directory_entry.large_page = 1;  //使用大页面 2mb
		pde_template.page_directory_entry.execute_for_usermode = 0;

		__stosq((unsigned __int64*)&page_table->pml2[0], pde_template.all, 512 * 512);

		// MTRR data for setting memory types
		// 用于设置内存类型的 MTRR 数据
		auto const mtrrs = hv::read_mtrr_data();
		g_vmm_context.mtrr_info.mtrrs = mtrrs;
		for (int i = 0; i < 512; i++)
		{
			for (int j = 0; j < 512; j++)
			{
				if (setup_pml2_entry(mtrrs, ept_state, page_table->pml2[i][j], (i * 512) + j) == false)
				{
					outDebug("setup_pml2_entry失败\n");
					return false;
				}					
			}
		}

		return true;
	}

	bool create_ept_page_table2(__ept_state& ept_state)
	{
		PHYSICAL_ADDRESS max_size;
		max_size.QuadPart = MAXULONG64;

		ept_state.ept_page_table = allocate_pool<__vmm_ept_page_table>();
		if (ept_state.ept_page_table == NULL)
		{
			LogError("Failed to allocate memory for PageTable");
			return false;
		}

		__vmm_ept_page_table* page_table = ept_state.ept_page_table;
		RtlSecureZeroMemory(page_table, sizeof(__vmm_ept_page_table));

		//
		// Set all pages as rwx to prevent unwanted ept violation
		// 将所有页面设置为 rwx，以防止不必要的 ept 违规
		// 设置第一个 PML4E，使其指向我们的 PDPT
		//
		page_table->pml4[0].page_frame_number = GET_PFN(MmGetPhysicalAddress(&page_table->pml3[0]).QuadPart);
		page_table->pml4[0].read = 1;
		page_table->pml4[0].write = 1;
		page_table->pml4[0].execute = 1;
		page_table->pml4[0].execute_for_usermode = 1;

		auto const mtrrs = hv::read_mtrr_data();

		// TODO: allocate a PT for the fixed MTRRs region so that we can get
		// more accurate memory typing in that area (as opposed to just
		// mapping the whole PDE as UC).
		// 为固定 MTRR 区域分配一个 PT页表，以便我们可以在该区域获得更准确的内存类型（而不是仅将整个 PDE 映射为 UC）。

		for (size_t i = 0; i < EPT_PD_COUNT; ++i) {
			// point each PDPTE to the corresponding PD
			// 将每个 PDPTE 指向相应的 PD
			auto& pdpte = page_table->pml3[i];
			pdpte.read = 1;
			pdpte.write = 1;
			pdpte.execute = 1;
			pdpte.accessed = 0;
			pdpte.execute_for_usermode = 1;
			pdpte.page_frame_number = GET_PFN(MmGetPhysicalAddress(&page_table->pml2[i][0]).QuadPart);

			for (size_t j = 0; j < 512; ++j) {
				// identity-map every GPA to the corresponding HPA
				auto& pde = page_table->pml2[i][j];
				pde.page_directory_entry.read = 1;
				pde.page_directory_entry.write = 1;
				pde.page_directory_entry.execute = 1;
				pde.page_directory_entry.ignore_pat = 0;
				pde.page_directory_entry.large_page = 1;  //表示pde未分割
				pde.page_directory_entry.accessed = 0;
				pde.page_directory_entry.dirty = 0;
				pde.page_directory_entry.execute_for_usermode = 1;
				pde.page_directory_entry.page_frame_number = (i << 9) + j;
				pde.page_directory_entry.memory_type = hv::calc_mtrr_mem_type(mtrrs, pde.page_directory_entry.page_frame_number << 21/*2mb对齐*/, 0x1000 << 9/*2mb大小*/);
			}
		}

		return true;
	}

	/// <summary>
	/// Initialize ept structure
	/// </summary>
	/// <returns></returns>
	bool initialize(__ept_state& ept_state)
	{
		__eptp* ept_pointer = allocate_pool<__eptp*>(PAGE_SIZE);
		if (ept_pointer == NULL)
		{
			outDebug("ept_pointer 空指针\n");
			return false;
		}			

		RtlSecureZeroMemory(ept_pointer, PAGE_SIZE);

		if (create_ept_page_table(ept_state) == false)
		{
			outDebug("create_ept_page_table失败\n");
			return false;
		}			

		//ept_pointer->memory_type = g_vmm_context.mtrr_info.default_memory_type;
		//当前 VMX 架构只支持 UC和WB
		ept_pointer->memory_type = MEMORY_TYPE_WRITE_BACK;

		// Indicates 4 level paging
		// 使用4级分页
		ept_pointer->page_walk_length = 3;

		//注意因为我们是映射的pml4[0]
		//所以这里&ept_state.ept_page_table->pml4，可以取到pml4[0]的地址
		ept_pointer->pml4_address = GET_PFN(MmGetPhysicalAddress(&ept_state.ept_page_table->pml4).QuadPart);

		ept_state.ept_pointer = ept_pointer;

		return true;
	}

	/// <summary>
	/// Get pml2 entry
	/// </summary>
	/// <param name="physical_address"></param>
	/// <returns> pointer to pml2 </returns>
	__ept_pde* get_pml2_entry(__ept_state& ept_state, unsigned __int64 physical_address)
	{
		unsigned __int64 pml4_index = MASK_EPT_PML4_INDEX(physical_address);
		unsigned __int64 pml3_index = MASK_EPT_PML3_INDEX(physical_address);
		unsigned __int64 pml2_index = MASK_EPT_PML2_INDEX(physical_address);

		//pml4_index必须为0，因为我们映射的是ept pml4[0],所以pml4_index大于0的条目都是无效条目
		//因为ept目前只支持映射一项pml4e，一个pml4e条目512GB大小
		if (pml4_index > 0)
		{
			LogError("512GB 以上的地址无效");
			return nullptr;
		}

		//从ept页表中取pde
		return &ept_state.ept_page_table->pml2[pml3_index][pml2_index];
	}

	/// <summary>
	/// 解析GPA取得ept的pte
	/// </summary>
	/// <param name="physical_address"></param>
	/// <returns></returns>
	__ept_pte* get_pml1_entry(__ept_state& ept_state, unsigned __int64 physical_address)
	{
		unsigned __int64 pml4_index = MASK_EPT_PML4_INDEX(physical_address);
		unsigned __int64 pml3_index = MASK_EPT_PML3_INDEX(physical_address);
		unsigned __int64 pml2_index = MASK_EPT_PML2_INDEX(physical_address);

		if (pml4_index > 0)
		{
			LogError("Address above 512GB is invalid");
			return nullptr;
		}

		__ept_pde* pml2 = &ept_state.ept_page_table->pml2[pml3_index][pml2_index];
		if (pml2->page_directory_entry.large_page == 1)
		{
			return nullptr;
		}

		PHYSICAL_ADDRESS pfn;
		pfn.QuadPart = pml2->large_page.page_frame_number << PAGE_SHIFT;
		__ept_pte* pml1 = (__ept_pte*)MmGetVirtualForPhysical(pfn);

		if (pml1 == nullptr)
		{
			return nullptr;
		}

		pml1 = &pml1[MASK_EPT_PML1_INDEX(physical_address)];
		return pml1;
	}

	/// <summary>
	/// Split pml2 into 512 pml1 entries (From one 2MB page to 512 4KB pages)
	/// 将pml2拆分为512个pml1项 (从一个2MB页面到512个4KB页面)
	/// </summary>
	/// <param name="pre_allocated_buffer"> Pre allocated buffer for split </param>
	/// <param name="physical_address"></param>
	/// <returns> status </returns>
	bool split_pml2(mtrr_data const& mtrrs, __ept_state& ept_state, void* pre_allocated_buffer, unsigned __int64 physical_address)
	{
		__ept_pde* pde = get_pml2_entry(ept_state, physical_address);
		if (pde == NULL)
		{
			LogError("Invalid address passed");
			return false;
		}

		//初始化pt页表
		__ept_dynamic_split* new_split = (__ept_dynamic_split*)pre_allocated_buffer;
		RtlSecureZeroMemory(new_split, sizeof(__ept_dynamic_split));

		//
		// Set all pages as rwx to prevent unwanted ept violation
		// 将所有页面设置为 rwx，以防止不必要的 ept 违规
		//
		new_split->entry = pde;

		__ept_pte entry_template = { 0 };
		entry_template.read = 1;
		entry_template.write = 1;
		entry_template.execute = 1;
		entry_template.ept_memory_type = pde->page_directory_entry.memory_type;
		entry_template.ignore_pat = pde->page_directory_entry.ignore_pat;
		entry_template.suppress_ve = pde->page_directory_entry.suppressve;

		__stosq((unsigned __int64*)&new_split->pml1[0], entry_template.all, 512);
		for (int i = 0; i < 512; i++)
		{
			//等价pte.page_frame_number = (pde_2mb->page_frame_number << 9) + i;
			unsigned __int64 pfn = ((pde->page_directory_entry.page_frame_number * LARGE_PAGE_SIZE) >> PAGE_SHIFT) + i;
			new_split->pml1[i].page_frame_number = pfn;
			new_split->pml1[i].ept_memory_type = get_memory_type(pfn, false);
			//new_split->pml1[i].ept_memory_type = hv::calc_mtrr_mem_type(mtrrs, pfn << 21, 0x1000 << 9);
		}

		__ept_pde new_entry = { 0 };
		new_entry.large_page.read = 1;
		new_entry.large_page.write = 1;
		new_entry.large_page.execute = 1;

		new_entry.large_page.page_frame_number = MmGetPhysicalAddress(&new_split->pml1[0]).QuadPart >> PAGE_SHIFT;

		RtlCopyMemory(pde, &new_entry, sizeof(new_entry));

		return true;
	}

	/// <summary>
	/// Swap physcial pages and invalidate tlb
	/// 交换物理页面并刷新tlb
	/// </summary>
	/// <param name="entry_address"> Pointer to page table entry which we want to change </param>
	/// <param name="entry_value"> Pointer to page table entry which we want use to change </param>
	/// <param name="invalidation_type"> Specifiy if we want to invalidate single context or all contexts  </param>
	void swap_pml1_and_invalidate_tlb(__ept_state& ept_state, __ept_pte* entry_address, __ept_pte entry_value, invept_type invalidation_type)
	{
		// 早期设计模式是所有cpu核共用一个ept，故而要上锁
		// 目前的设计是每个cpu独享一份ept页表，故无需上锁
		// Acquire the lock
		//spinlock::lock(&g_vmm_context.pml_lock);

		// Set the value
		entry_address->all = entry_value.all;

		// Invalidate the cache
		if (invalidation_type == invept_single_context)
		{
			invept_single_context_func(ept_state.ept_pointer->all);
		}
		else
		{
			invept_all_contexts_func();
		}

		// Release the lock
		//spinlock::unlock(&g_vmm_context.pml_lock);
	}

	/// <summary>
	/// Write an absolute jump, We aren't touching any register except stack so it's the most safest trampoline
	/// Size: 14 bytes
	/// </summary>
	/// <param name="target_buffer"> Pointer to trampoline buffer </param>
	/// <param name="destination_address"> Address of place where we want to jump </param>
	void hook_write_absolute_jump(unsigned __int8* target_buffer, unsigned __int64 destination_address)
	{
		// push lower 32 bits of destination address	
		target_buffer[0] = 0x68;
		*((unsigned __int32*)&target_buffer[1]) = (unsigned __int32)destination_address;

		// mov dword ptr [rsp + 4]
		target_buffer[5] = 0xc7;
		target_buffer[6] = 0x44;
		target_buffer[7] = 0x24;
		target_buffer[8] = 0x04;

		// higher 32 bits of destination address	
		*((unsigned __int32*)&target_buffer[9]) = (unsigned __int32)(destination_address >> 32);

		// ret
		target_buffer[13] = 0xc3;
	}

	/// <summary>
	/// Write relative jump,
	/// Size: 5 Bytes
	/// </summary>
	/// <param name="target_buffer"> Pointer to trampoline buffer </param>
	/// <param name="destination_address"> Address where we want to jump </param>
	/// <param name="source_address"> Address from which we want to jump </param>
	void hook_write_relative_jump(unsigned __int8* target_buffer, unsigned __int64 destination_address, unsigned __int64 source_address)
	{
		// destination - (source + sizeof instruction)
		__int32 jmp_value = destination_address - (source_address + 0x5);

		// relative jmp opcode
		target_buffer[0] = 0xe9;

		// set jmp offset
		*((__int32*)&target_buffer[1]) = jmp_value;
	}

	//将vmcall指令写入伪造页  通过触发vmcall来实现hook
	bool write_vmcall_instruction_to_memory(__ept_hooked_function_info* hooked_function_info, void* target_function, void* proxy_function, void** origin_function)
	{
		unsigned __int64 hooked_instructions_size = 0;

		// Get offset of hooked function within page
		// 获得线性地址的低12位 页偏移
		unsigned __int64 page_offset = MASK_EPT_PML1_OFFSET((unsigned __int64)target_function);
		unsigned __int8* target_buffer = &hooked_function_info->fake_page_contents[page_offset];
		target_buffer[0] = 0x0f;
		target_buffer[1] = 0x01;
		target_buffer[2] = 0xC1;  //0F 01 C1 vmcall
		hooked_function_info->handler_function = proxy_function;

		//DbgBreakPoint();
		if (origin_function)
		{
			//计算被修改的指令占用多少字节
			while (hooked_instructions_size < 3)
				hooked_instructions_size += LDE((unsigned __int8*)target_function + hooked_instructions_size, 64);

			hooked_function_info->hook_size = hooked_instructions_size;

			// Copy overwritten instructions to trampoline buffer
			// 将覆盖的指令备份到跳板缓冲区
			RtlCopyMemory(hooked_function_info->first_trampoline_address, target_function, hooked_instructions_size);

			// Add the absolute jump back to the original function.
			// 添加绝对跳转回到原来的函数
			hook_write_absolute_jump(&hooked_function_info->first_trampoline_address[hooked_instructions_size], (unsigned __int64)target_function + hooked_instructions_size);

			// Return to user address of trampoline to call original function
			*origin_function = hooked_function_info->first_trampoline_address;
		}

		return true;
	}

	//将int3指令写入伪造页  通过触发int3中断来实现hook
	bool write_cc_instruction_to_memory(__ept_hooked_function_info* hooked_function_info, void* target_function, void* proxy_function, void** origin_function)
	{
		unsigned __int64 hooked_instructions_size = 1;

		// Get offset of hooked function within page
		// 获得线性地址的低12位 页偏移
		unsigned __int64 page_offset = MASK_EPT_PML1_OFFSET((unsigned __int64)target_function);
		hooked_function_info->hook_size = hooked_instructions_size;
		unsigned __int8* target_buffer = &hooked_function_info->fake_page_contents[page_offset];
		target_buffer[0] = 0xCC;
		hooked_function_info->handler_function = proxy_function;  //int3的处理函数

		if (origin_function)
		{
			//计算被修改的指令占用多少字节
			hooked_instructions_size = LDE((unsigned __int8*)target_function, 64);

			// Copy overwritten instructions to trampoline buffer
			// 将覆盖的指令复制到跳板缓冲区
			RtlCopyMemory(hooked_function_info->first_trampoline_address, target_function, hooked_instructions_size);

			// Add the absolute jump back to the original function.
			// 添加绝对跳转回到原来的函数
			hook_write_absolute_jump(&hooked_function_info->first_trampoline_address[hooked_instructions_size], (unsigned __int64)target_function + hooked_instructions_size);

			// Return to user address of trampoline to call original function
			*origin_function = hooked_function_info->first_trampoline_address;
		}

		return true;
	}

	//将int1写入伪页  进行hook
	bool write_int1_instruction_to_memory(__ept_hooked_function_info* hooked_function_info, void* target_function, void* proxy_function, void** origin_function)
	{
		unsigned __int64 hooked_instructions_size = 1;

		// Get offset of hooked function within page
		// 获得线性地址的低12位 页偏移
		unsigned __int64 page_offset = MASK_EPT_PML1_OFFSET((unsigned __int64)target_function);
		hooked_function_info->hook_size = hooked_instructions_size;
		unsigned __int8* target_buffer = &hooked_function_info->fake_page_contents[page_offset];
		target_buffer[0] = 0xf1;  //int1
		hooked_function_info->handler_function = proxy_function;  //int1的处理函数

		if (origin_function)
		{
			//计算被修改的指令占用多少字节
			hooked_instructions_size = LDE((unsigned __int8*)target_function, 64);

			// Copy overwritten instructions to trampoline buffer
			// 将覆盖的指令复制到跳板缓冲区
			RtlCopyMemory(hooked_function_info->first_trampoline_address, target_function, hooked_instructions_size);

			// Add the absolute jump back to the original function.
			// 添加绝对跳转回到原来的函数
			hook_write_absolute_jump(&hooked_function_info->first_trampoline_address[hooked_instructions_size], (unsigned __int64)target_function + hooked_instructions_size);

			// Return to user address of trampoline to call original function
			*origin_function = hooked_function_info->first_trampoline_address;
		}

		return true;
	}

	/// <summary>
	/// 
	/// </summary>
	/// <param name="hooked_page"> Pointer to __ept_hooked_page_info structure which holds info about hooked page </param>
	/// <param name="target_function"> Address of function which we want to hook </param>
	/// <param name="proxy_function"> Address of hooked version of function which we are hooking </param>
	/// <param name="origin_function"> Address used to call original function </param>
	/// <returns></returns>
	bool hook_instruction_memory(__ept_state& ept_state, __ept_hooked_function_info* hooked_function_info, void* target_function, void* proxy_function, void** origin_function)
	{
		unsigned __int64 hooked_instructions_size = 0;

		// Get offset of hooked function within page
		// 获得线性地址的低12位 页偏移
		unsigned __int64 page_offset = MASK_EPT_PML1_OFFSET((unsigned __int64)target_function);

		//if (trampoline != 0)
		//{
		//	hooked_instructions_size = 0;

		//	// If first 5 bytes of function are on 2 separate pages then return (Hypervisor doesn't support function hooking at page boundaries)
		//	if ((page_offset + 5) > PAGE_SIZE - 1)
		//	{
		//		LogError("Function at page boundary");
		//		return false;
		//	}

		//	while (hooked_instructions_size < 5)
		//	{
		//		hooked_instructions_size += LDE((unsigned __int8*)target_function + hooked_instructions_size, 64);
		//	}

		//	// If instructions to hook are on two seperate pages then stop hooking (Hypervisor doesn't support function hooking at page boundaries)
		//	if ((hooked_instructions_size + 5) > PAGE_SIZE - 1)
		//	{
		//		LogError("页面边界处的函数");
		//		return false;
		//	}

		//	hooked_function_info->hook_size = hooked_instructions_size;

		//	//在伪造页里构造相对跳转
		//	hook_write_relative_jump(&hooked_function_info->fake_page_contents[page_offset], (unsigned __int64)trampoline, (unsigned __int64)target_function);

		//	//跳回原始函数
		//	//拷贝源函数字节
		//	RtlCopyMemory(hooked_function_info->first_trampoline_address, target_function, hooked_instructions_size);
		//	//构造跳板
		//	hook_write_absolute_jump(&hooked_function_info->first_trampoline_address[hooked_instructions_size], (unsigned __int64)target_function + hooked_instructions_size);

		//	//源函数
		//	*origin_function = hooked_function_info->first_trampoline_address;

		//	return hook_function(ept_state, trampoline, proxy_function, nullptr);
		//}

		// If first 14 bytes of function are on 2 separate pages then return (Hypervisor doesn't support function hooking at page boundaries)
		// 判断页偏移 >4095  即判断是否跨入到下一个4K页面
		if ((page_offset + 14) > PAGE_SIZE - 1)
		{
			LogError("Function at page boundary");
			return false;
		}

		// Get the full size of instructions necessary to copy
		while (hooked_instructions_size < 14)
			hooked_instructions_size += LDE((unsigned __int8*)target_function + hooked_instructions_size, 64);


		// If instructions to hook are on two seperate pages then return (Hypervisor doesn't support function hooking at page boundaries)
		if ((hooked_instructions_size + 14) > PAGE_SIZE - 1)
		{
			LogError("Function at page boundary");
			return false;
		}

		hooked_function_info->hook_size = hooked_instructions_size;

		//
		// Now it's trampoline so we don't have to store origin function
		if (origin_function == nullptr)
		{
			hook_write_absolute_jump(&hooked_function_info->fake_page_contents[page_offset], (unsigned __int64)proxy_function);

			return true;
		}

		// Copy overwritten instructions to trampoline buffer
		RtlCopyMemory(hooked_function_info->first_trampoline_address, target_function, hooked_instructions_size);

		// Add the absolute jump back to the original function.
		hook_write_absolute_jump(&hooked_function_info->first_trampoline_address[hooked_instructions_size], (unsigned __int64)target_function + hooked_instructions_size);

		// Return to user address of trampoline to call original function
		*origin_function = hooked_function_info->first_trampoline_address;

		// Write the absolute jump to our shadow page memory to jump to our hooked_page.
		hook_write_absolute_jump(&hooked_function_info->fake_page_contents[page_offset], (unsigned __int64)proxy_function);

		return true;
	}

	bool is_page_splitted(__ept_state& ept_state, unsigned __int64 physical_address)
	{
		__ept_pde* entry = get_pml2_entry(ept_state, physical_address);
		return !entry->page_directory_entry.large_page;
	}

	//如果是我们vmcall hook的地址则转到代理函数
	bool handler_vmcall_rip(__ept_state& ept_state)
	{
		unsigned __int64 guest_rip = hv::vmread(GUEST_RIP);
		unsigned __int64 physical_address = hv::get_physical_address(hv::ghv.system_cr3.flags, (PVOID)guest_rip);

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
						return true;
					}
				}
			}
		}
		return false;
	}

	//通过vmcall进行hook 将vmcall指令插入到目标代码
	bool vmcall_hook_function(__ept_state& ept_state, 
		void* target_function/*被挂钩的函数地址*/, 
		void* proxy_function/*新函数地址*/, 
		void** origin_function,
		unsigned __int64 target_cr3)
	{
		//unsigned __int64 physical_address = hv::get_physical_address(target_cr3, (PVOID)target_function);
		unsigned __int64 physical_address = MmGetPhysicalAddress(target_function).QuadPart;

		//
		// 检测该函数是否存在于物理内存中
		// 如果该函数的物理地址为空
		// 说明该函数不存在于物理内存中
		//
		if (physical_address == NULL)
		{
			LogError("请求的虚拟内存在物理内存中不存在");
			return false;
		}

		//
		// 检查页面是否尚未挂钩
		//
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
				if (hooked_page_info->Options == EPTO_HOOK_FUNCTION)
				{
					LogInfo("页面已挂钩");

					__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, TRUE, sizeof(__ept_hooked_function_info));
					if (hooked_function_info == nullptr)
					{
						LogError("挂钩函数结构没有预先分配池内存");
						return false;
					}

					//
					// If we are hooking code cave for second trampoline 
					// then origin function in null and we don't have to get pool for trampoline
					//
					if (origin_function != nullptr)
					{
						hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
						if (hooked_function_info->first_trampoline_address == nullptr)
						{
							pool_manager::release_pool(hooked_function_info);
							LogError("There is no pre-allocated pool for trampoline");
							return false;
						}
					}

					hooked_function_info->virtual_address = target_function;
					hooked_function_info->breakpoint_address = NULL;

					hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

					if (write_vmcall_instruction_to_memory(hooked_function_info, target_function, proxy_function, origin_function) == false)
					{
						if (hooked_function_info->first_trampoline_address != nullptr)
							pool_manager::release_pool(hooked_function_info->first_trampoline_address);
						pool_manager::release_pool(hooked_function_info);
						LogError("Hook failed");
						return false;
					}

					// Track all hooked functions within page
					InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

					return true;
				}
			}
		}

		//判断2mb pde是否已经进行分割
		if (is_page_splitted(ept_state, physical_address) == false)
		{
			//分配pt页表
			void* split_buffer = pool_manager::request_pool<void*>(pool_manager::INTENTION_SPLIT_PML2, true, sizeof(__ept_dynamic_split));
			if (split_buffer == nullptr)
			{
				LogError("There is no preallocated pool for split");
				return false;
			}

			//将2MB页面分割为512个4KB页面
			if (split_pml2(g_vmm_context.mtrr_info.mtrrs, ept_state, split_buffer, physical_address) == false)
			{
				pool_manager::release_pool(split_buffer);
				LogError("Split failed");
				return false;
			}
		}

		//得到目标页
		__ept_pte* target_page = get_pml1_entry(ept_state, physical_address);
		if (target_page == nullptr)
		{
			LogError("Failed to get PML1 entry of the target address");
			return false;
		}

		__ept_hooked_page_info* hooked_page_info = pool_manager::request_pool<__ept_hooked_page_info*>(pool_manager::INTENTION_TRACK_HOOKED_PAGES, true, sizeof(__ept_hooked_page_info));
		if (hooked_page_info == nullptr)
		{
			LogError("There is no preallocated pool for hooked page info");
			return false;
		}

		InitializeListHead(&hooked_page_info->hooked_functions_list);

		__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, true, sizeof(__ept_hooked_function_info));
		if (hooked_function_info == nullptr)
		{
			pool_manager::release_pool(hooked_page_info);
			LogError("hook函数时预分配的缓冲已耗尽");
			return false;
		}

		//
		// If we are hooking code cave for second trampoline 
		// then origin function in null and we don't have to get pool for trampoline
		//
		if (origin_function != nullptr)
		{
			hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
			if (hooked_function_info->first_trampoline_address == nullptr)
			{
				pool_manager::release_pool(hooked_page_info);
				pool_manager::release_pool(hooked_function_info);
				LogError("There is no pre-allocated pool for trampoline");
				return false;
			}
		}

		hooked_page_info->pfn_of_hooked_page = GET_PFN(physical_address);
		hooked_page_info->pfn_of_fake_page_contents = GET_PFN(MmGetPhysicalAddress(hooked_page_info->fake_page_contents).QuadPart);
		hooked_page_info->entry_address = target_page;

		//先改原页为不可执行 回到guest后执行就触发vmexit
		//进入vmm后交换伪造页
		hooked_page_info->entry_address->execute = 0;
		hooked_page_info->entry_address->read = 1;
		hooked_page_info->entry_address->write = 1;

		hooked_page_info->original_entry = *target_page;
		hooked_page_info->changed_entry = *target_page;

		//伪造页不得读写		
		hooked_page_info->changed_entry.read = 0;
		hooked_page_info->changed_entry.write = 0;
		hooked_page_info->changed_entry.execute = 1;

		//将伪造页换上去
		hooked_page_info->changed_entry.page_frame_number = hooked_page_info->pfn_of_fake_page_contents;

		RtlCopyMemory(&hooked_page_info->fake_page_contents, PAGE_ALIGN(target_function), PAGE_SIZE);

		hooked_function_info->virtual_address = target_function;
		hooked_function_info->breakpoint_address = NULL;

		hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

		//开始hook
		if (write_vmcall_instruction_to_memory(hooked_function_info, target_function, proxy_function, origin_function) == false)
		{
			if (hooked_function_info->first_trampoline_address != nullptr)
				pool_manager::release_pool(hooked_function_info->first_trampoline_address);
			pool_manager::release_pool(hooked_function_info);
			pool_manager::release_pool(hooked_page_info);
			LogError("Hook failed");
			return false;
		}

		hooked_page_info->Options = EPTO_HOOK_FUNCTION;

		//记录hook的函数和页 以便日后释放
		// Track all hooked functions
		InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

		// Track all hooked pages
		InsertHeadList(&ept_state.hooked_page_list, &hooked_page_info->hooked_page_list);

		invept_single_context_func(ept_state.ept_pointer->all);

		return true;
	}

	//写伪造页内存
	bool write_fake_page_memory(__ept_hooked_function_info* hooked_function_info, void* target_address, void* buffer, unsigned __int64 buffer_size)
	{
		if ((hooked_function_info != NULL) && (target_address != NULL) && (buffer != NULL))
		{
			// Get offset of hooked function within page
			// 获得线性地址的低12位 页偏移
			// 得到欲修改的指令在页内的偏移
			unsigned __int64 page_offset = MASK_EPT_PML1_OFFSET((unsigned __int64)target_address);
			RtlCopyMemory(&hooked_function_info->fake_page_contents[page_offset], buffer, buffer_size);
		}
		return true;
	}

	//读伪造页内存
	bool read_fake_page_memory(__ept_hooked_function_info* hooked_function_info, void* target_address, void* buffer, unsigned __int64 buffer_size)
	{
		if ((hooked_function_info != NULL) && (target_address != NULL) && (buffer != NULL))
		{
			// Get offset of hooked function within page
			// 获得线性地址的低12位 页偏移
			// 得到欲读取的指令在页内的偏移
			unsigned __int64 page_offset = MASK_EPT_PML1_OFFSET((unsigned __int64)target_address);
			RtlCopyMemory(buffer, &hooked_function_info->fake_page_contents[page_offset], buffer_size);
		}
		return true;
	}

	//读取隐形软件断点
	bool get_hide_software_breakpoint(__ept_state& ept_state, PVT_BREAK_POINT vmcallinfo)
	{
		VT_BREAK_POINT tmp_vmcallinfo = { 0 };

		if (sizeof(VT_BREAK_POINT) != hv::read_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
		{
			//读取数据可能不完整
			return false;
		}

		PLIST_ENTRY current = &ept_state.hooked_page_list;
		while (&ept_state.hooked_page_list != current->Flink)
		{
			current = current->Flink;
			//从列表中取出挂钩页
			__ept_hooked_page_info* hooked_page_info = CONTAINING_RECORD(current, __ept_hooked_page_info, hooked_page_list);

			//判断目标地址是否是被挂钩的页
			//仅比较页帧号
			if (hooked_page_info->pfn_of_hooked_page == GET_PFN(tmp_vmcallinfo.PhysicalAddress))
			{
				if (hooked_page_info->Options == EPTO_VIRTUAL_BREAKPOINT)
				{
					LogInfo("页面已挂钩");

					int offset = tmp_vmcallinfo.VirtualAddress & 0xFFF;
					memcpy(&tmp_vmcallinfo.buffer[0], &hooked_page_info->fake_page_contents[offset], tmp_vmcallinfo.Size);

					//返回给调试器
					if (sizeof(VT_BREAK_POINT) != hv::write_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
					{
						//写入数据可能不完整
						return false;
					}

					return true;
				}
			}
		}

		//cr3 guest_cr3;
		//guest_cr3.flags = tmp_vmcallinfo.cr3;

		////读目标进程的内存
		//if (tmp_vmcallinfo.Size != hv::read_guest_virtual_memory(guest_cr3, 
		//	(PVOID)tmp_vmcallinfo.VirtualAddress, 
		//	&tmp_vmcallinfo.buffer[0], 
		//	tmp_vmcallinfo.Size))
		//{
		//	//读取数据可能不完整
		//	return false;
		//}

		return false;
	}

	//隐藏cc断点
	bool hide_cc_breakpoint(__ept_state& ept_state,
		VT_BREAK_POINT vmcallinfo,
		unsigned __int64 physical_address,
		unsigned __int64 Type)
	{
		if (physical_address == NULL)
		{
			LogError("请求的虚拟内存在物理内存中不存在");
			return false;
		}

		//
		// 检查页面是否尚未挂钩
		//
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
				if (hooked_page_info->Options == EPTO_VIRTUAL_BREAKPOINT)
				{
					LogInfo("页面已挂钩");

					//写入cc断点
					int offset = vmcallinfo.VirtualAddress & 0xFFF;
					hooked_page_info->fake_page_contents[offset] = 0xCC;

					return true;
				}
			}
		}

		//判断2mb pde是否已经进行分割
		if (is_page_splitted(ept_state, physical_address) == false)
		{
			//分配pt页表
			void* split_buffer = pool_manager::request_pool<void*>(pool_manager::INTENTION_SPLIT_PML2, true, sizeof(__ept_dynamic_split));
			if (split_buffer == nullptr)
			{
				LogError("There is no preallocated pool for split");
				return false;
			}

			//将2MB页面分割为512个4KB页面
			if (split_pml2(g_vmm_context.mtrr_info.mtrrs, ept_state, split_buffer, physical_address) == false)
			{
				pool_manager::release_pool(split_buffer);
				LogError("Split failed");
				return false;
			}
		}

		//得到目标页
		__ept_pte* target_page = get_pml1_entry(ept_state, physical_address);
		if (target_page == nullptr)
		{
			LogError("Failed to get PML1 entry of the target address");
			return false;
		}

		__ept_hooked_page_info* hooked_page_info = pool_manager::request_pool<__ept_hooked_page_info*>(pool_manager::INTENTION_TRACK_HOOKED_PAGES, true, sizeof(__ept_hooked_page_info));
		if (hooked_page_info == nullptr)
		{
			LogError("There is no preallocated pool for hooked page info");
			return false;
		}

		InitializeListHead(&hooked_page_info->hooked_functions_list);

		hooked_page_info->pfn_of_hooked_page = GET_PFN(physical_address);
		hooked_page_info->pfn_of_fake_page_contents = GET_PFN(MmGetPhysicalAddress(hooked_page_info->fake_page_contents).QuadPart);
		hooked_page_info->entry_address = target_page;

		//将原页修改为不可执行
		hooked_page_info->entry_address->read = 1;
		hooked_page_info->entry_address->write = 1;
		hooked_page_info->entry_address->execute = 0;

		hooked_page_info->original_entry = *target_page;  //指向原
		hooked_page_info->changed_entry = *target_page;

		//伪页不可读写
		hooked_page_info->changed_entry.read = 0;
		hooked_page_info->changed_entry.write = 0;
		hooked_page_info->changed_entry.execute = 1;

		//指向伪页pfn
		hooked_page_info->changed_entry.page_frame_number = hooked_page_info->pfn_of_fake_page_contents;

		hooked_page_info->Options = EPTO_VIRTUAL_BREAKPOINT;

		cr3 guest_cr3;
		guest_cr3.flags = vmcallinfo.cr3;

		//备份目标进程原页内容
		if (PAGE_SIZE != hv::read_guest_virtual_memory(guest_cr3, PAGE_ALIGN(vmcallinfo.VirtualAddress), &hooked_page_info->fake_page_contents, PAGE_SIZE))
		{
			//读取数据可能不完整
			return false;
		}

		//写入cc断点
		int offset = vmcallinfo.VirtualAddress & 0xFFF;
		hooked_page_info->fake_page_contents[offset] = 0xCC;

		// Track all hooked pages
		InsertHeadList(&ept_state.hooked_page_list, &hooked_page_info->hooked_page_list);

		return true;
	}

	//设置隐形软件断点
	bool set_hide_software_breakpoint(PVT_BREAK_POINT vmcallinfo)
	{
		int status = 0;
		VT_BREAK_POINT tmp_vmcallinfo = { 0 };

		if (sizeof(VT_BREAK_POINT) != hv::read_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
		{
			//读取数据可能不完整
			return false;
		}

		//获取目标进程的物理地址
		unsigned __int64 physical_address = hv::get_physical_address(tmp_vmcallinfo.cr3, (PVOID)tmp_vmcallinfo.VirtualAddress);

		if (physical_address == NULL)
		{
			LogError("请求的虚拟地址没有映射的物理地址");
			return false;
		}

		spinlock::lock(&eptWatchList_lock);

		//获取空闲的监视id
		int ID = hv::getIdleWatchID();

		if (ID == -1)
		{
			//没有可用空间
			spinlock::unlock(&eptWatchList_lock);
			return false;
		}


		//判断是否跨页了，不支持跨页下断，只会设置当前页面
		unsigned __int64 tmp = physical_address + tmp_vmcallinfo.Size;
		if (GET_PFN(tmp) != GET_PFN(physical_address))
			eptWatchList[ID].Size = 0x1000 - (physical_address & 0xfff);  //如果跨页，则只会设置当前页面，计算当前页面中需要设置的字节数大小
		else
			eptWatchList[ID].Size = tmp_vmcallinfo.Size; //需要设置断点的字节数大小

		eptWatchList[ID].cr3 = tmp_vmcallinfo.cr3;
		eptWatchList[ID].VirtualAddress = tmp_vmcallinfo.VirtualAddress;
		eptWatchList[ID].PhysicalAddress = physical_address;
		eptWatchList[ID].LoopUserMode = tmp_vmcallinfo.LoopUserMode;
		eptWatchList[ID].OriginalByte = tmp_vmcallinfo.OriginalBytes;
		eptWatchList[ID].Type = EPTW_READWRITE;

		eptWatchList[ID].Options = EPTO_VIRTUAL_BREAKPOINT;
		eptWatchList[ID].bpType = 3;

		eptWatchList[ID].inuse = 1;


		//为每个cpu设置监视
		for (int i = 0; i < tmp_vmcallinfo.CPUCount; i++)
		{
			__vcpu* vcpu = &g_vmm_context.vcpu[i];
			if (hide_cc_breakpoint(*vcpu->ept_state, tmp_vmcallinfo, physical_address, EPTW_READWRITE))
			{
				status++;
			}
		}
		if (status == tmp_vmcallinfo.CPUCount)
		{
			tmp_vmcallinfo.watchid = ID;  //记录这个id，卸载监视的时候需要
			tmp_vmcallinfo.PhysicalAddress = physical_address;

			if (sizeof(VT_BREAK_POINT) != hv::write_guest_virtual_memory(vmcallinfo, &tmp_vmcallinfo, sizeof(VT_BREAK_POINT)))
			{
				//写入数据可能不完整
				spinlock::unlock(&eptWatchList_lock);
				return false;
			}

			invept_all_contexts_func(); //在这里刷新全部逻辑处理器的eptp寄存器
			spinlock::unlock(&eptWatchList_lock);

			return true;
		}
		spinlock::unlock(&eptWatchList_lock);
		return false;
	}

	//int3 hook
	bool cc_hook_function(__ept_state& ept_state, void* target_function/*被挂钩的函数地址*/, void* proxy_function/*新函数地址*/, void** origin_function)
	{
		unsigned __int64 physical_address = MmGetPhysicalAddress(target_function).QuadPart;

		//
		// 检测该函数是否存在于物理内存中
		// 如果该函数的物理地址为空
		// 说明该函数不存在于物理内存中
		//
		if (physical_address == NULL)
		{
			LogError("请求的虚拟内存在物理内存中不存在");
			return false;
		}

		//
		// 检查页面是否尚未挂钩
		//
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
				LogInfo("页面已挂钩");

				__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, TRUE, sizeof(__ept_hooked_function_info));
				if (hooked_function_info == nullptr)
				{
					LogError("挂钩函数结构没有预先分配池内存");
					return false;
				}

				//
				// If we are hooking code cave for second trampoline 
				// then origin function in null and we don't have to get pool for trampoline
				//
				if (origin_function != nullptr)
				{
					hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
					if (hooked_function_info->first_trampoline_address == nullptr)
					{
						pool_manager::release_pool(hooked_function_info);
						LogError("There is no pre-allocated pool for trampoline");
						return false;
					}
				}

				hooked_function_info->virtual_address = target_function;
				hooked_function_info->breakpoint_address = NULL;

				hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

				if (write_cc_instruction_to_memory(hooked_function_info, target_function, proxy_function, origin_function) == false)
				{
					if (hooked_function_info->first_trampoline_address != nullptr)
						pool_manager::release_pool(hooked_function_info->first_trampoline_address);
					pool_manager::release_pool(hooked_function_info);
					LogError("Hook failed");
					return false;
				}

				// Track all hooked functions within page
				InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

				return true;
			}
		}

		if (is_page_splitted(ept_state, physical_address) == false)
		{
			void* split_buffer = pool_manager::request_pool<void*>(pool_manager::INTENTION_SPLIT_PML2, true, sizeof(__ept_dynamic_split));
			if (split_buffer == nullptr)
			{
				LogError("There is no preallocated pool for split");
				return false;
			}

			//将2MB页面分割为512个4KB页面
			if (split_pml2(g_vmm_context.mtrr_info.mtrrs, ept_state, split_buffer, physical_address) == false)
			{
				pool_manager::release_pool(split_buffer);
				LogError("Split failed");
				return false;
			}
		}

		__ept_pte* target_page = get_pml1_entry(ept_state, physical_address);
		if (target_page == nullptr)
		{
			LogError("Failed to get PML1 entry of the target address");
			return false;
		}

		__ept_hooked_page_info* hooked_page_info = pool_manager::request_pool<__ept_hooked_page_info*>(pool_manager::INTENTION_TRACK_HOOKED_PAGES, true, sizeof(__ept_hooked_page_info));
		if (hooked_page_info == nullptr)
		{
			LogError("There is no preallocated pool for hooked page info");
			return false;
		}

		InitializeListHead(&hooked_page_info->hooked_functions_list);

		__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, true, sizeof(__ept_hooked_function_info));
		if (hooked_function_info == nullptr)
		{
			pool_manager::release_pool(hooked_page_info);
			LogError("hook函数时预分配的缓冲已耗尽");
			return false;
		}

		//
		// If we are hooking code cave for second trampoline 
		// then origin function in null and we don't have to get pool for trampoline
		//
		if (origin_function != nullptr)
		{
			hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
			if (hooked_function_info->first_trampoline_address == nullptr)
			{
				pool_manager::release_pool(hooked_page_info);
				pool_manager::release_pool(hooked_function_info);
				LogError("There is no pre-allocated pool for trampoline");
				return false;
			}
		}

		hooked_page_info->pfn_of_hooked_page = GET_PFN(physical_address);
		hooked_page_info->pfn_of_fake_page_contents = GET_PFN(MmGetPhysicalAddress(hooked_page_info->fake_page_contents).QuadPart);
		hooked_page_info->entry_address = target_page;

		//先改原页为不可执行 回到guest后执行就触发vmexit
		//进入vmm后交换伪造页
		hooked_page_info->entry_address->execute = 0;
		hooked_page_info->entry_address->read = 1;
		hooked_page_info->entry_address->write = 1;

		hooked_page_info->original_entry = *target_page;
		hooked_page_info->changed_entry = *target_page;

		//伪造页不得读写		
		hooked_page_info->changed_entry.read = 0;
		hooked_page_info->changed_entry.write = 0;
		hooked_page_info->changed_entry.execute = 1;

		//将伪造页换上去
		hooked_page_info->changed_entry.page_frame_number = hooked_page_info->pfn_of_fake_page_contents;

		RtlCopyMemory(&hooked_page_info->fake_page_contents, PAGE_ALIGN(target_function), PAGE_SIZE);

		hooked_function_info->virtual_address = target_function;
		hooked_function_info->breakpoint_address = NULL;

		hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

		//开始hook
		if (write_cc_instruction_to_memory(hooked_function_info, target_function, proxy_function, origin_function) == false)
		{
			if (hooked_function_info->first_trampoline_address != nullptr)
				pool_manager::release_pool(hooked_function_info->first_trampoline_address);
			pool_manager::release_pool(hooked_function_info);
			pool_manager::release_pool(hooked_page_info);
			LogError("Hook failed");
			return false;
		}

		//记录hook的函数和页 以便日后释放
		// Track all hooked functions
		InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

		// Track all hooked pages
		InsertHeadList(&ept_state.hooked_page_list, &hooked_page_info->hooked_page_list);

		invept_single_context_func(ept_state.ept_pointer->all);

		return true;
	}

	//#DB hook
	bool int1_hook_function(__ept_state& ept_state, void* target_function/*被挂钩的函数地址*/, void* proxy_function/*新函数地址*/, void** origin_function)
	{
		unsigned __int64 physical_address = MmGetPhysicalAddress(target_function).QuadPart;

		//
		// 检测该函数是否存在于物理内存中
		// 如果该函数的物理地址为空
		// 说明该函数不存在于物理内存中
		//
		if (physical_address == NULL)
		{
			LogError("请求的虚拟内存在物理内存中不存在");
			return false;
		}

		//
		// 检查页面是否尚未挂钩
		//
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
				LogInfo("页面已挂钩");

				__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, TRUE, sizeof(__ept_hooked_function_info));
				if (hooked_function_info == nullptr)
				{
					LogError("挂钩函数结构没有预先分配池内存");
					return false;
				}

				//
				// If we are hooking code cave for second trampoline 
				// then origin function in null and we don't have to get pool for trampoline
				//
				if (origin_function != nullptr)
				{
					hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
					if (hooked_function_info->first_trampoline_address == nullptr)
					{
						pool_manager::release_pool(hooked_function_info);
						LogError("There is no pre-allocated pool for trampoline");
						return false;
					}
				}

				hooked_function_info->virtual_address = target_function;
				hooked_function_info->breakpoint_address = NULL;

				hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

				if (write_int1_instruction_to_memory(hooked_function_info, target_function, proxy_function, origin_function) == false)
				{
					if (hooked_function_info->first_trampoline_address != nullptr)
						pool_manager::release_pool(hooked_function_info->first_trampoline_address);
					pool_manager::release_pool(hooked_function_info);
					LogError("Hook failed");
					return false;
				}

				// Track all hooked functions within page
				InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

				return true;
			}
		}

		if (is_page_splitted(ept_state, physical_address) == false)
		{
			void* split_buffer = pool_manager::request_pool<void*>(pool_manager::INTENTION_SPLIT_PML2, true, sizeof(__ept_dynamic_split));
			if (split_buffer == nullptr)
			{
				LogError("There is no preallocated pool for split");
				return false;
			}

			//将2MB页面分割为512个4KB页面
			if (split_pml2(g_vmm_context.mtrr_info.mtrrs, ept_state, split_buffer, physical_address) == false)
			{
				pool_manager::release_pool(split_buffer);
				LogError("Split failed");
				return false;
			}
		}

		__ept_pte* target_page = get_pml1_entry(ept_state, physical_address);
		if (target_page == nullptr)
		{
			LogError("Failed to get PML1 entry of the target address");
			return false;
		}

		__ept_hooked_page_info* hooked_page_info = pool_manager::request_pool<__ept_hooked_page_info*>(pool_manager::INTENTION_TRACK_HOOKED_PAGES, true, sizeof(__ept_hooked_page_info));
		if (hooked_page_info == nullptr)
		{
			LogError("There is no preallocated pool for hooked page info");
			return false;
		}

		InitializeListHead(&hooked_page_info->hooked_functions_list);

		__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, true, sizeof(__ept_hooked_function_info));
		if (hooked_function_info == nullptr)
		{
			pool_manager::release_pool(hooked_page_info);
			LogError("hook函数时预分配的缓冲已耗尽");
			return false;
		}

		//
		// If we are hooking code cave for second trampoline 
		// then origin function in null and we don't have to get pool for trampoline
		//
		if (origin_function != nullptr)
		{
			hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
			if (hooked_function_info->first_trampoline_address == nullptr)
			{
				pool_manager::release_pool(hooked_page_info);
				pool_manager::release_pool(hooked_function_info);
				LogError("There is no pre-allocated pool for trampoline");
				return false;
			}
		}

		hooked_page_info->pfn_of_hooked_page = GET_PFN(physical_address);
		hooked_page_info->pfn_of_fake_page_contents = GET_PFN(MmGetPhysicalAddress(hooked_page_info->fake_page_contents).QuadPart);
		hooked_page_info->entry_address = target_page;

		//先改原页为不可执行 回到guest后执行就触发vmexit
		//进入vmm后交换伪造页
		hooked_page_info->entry_address->execute = 0;
		hooked_page_info->entry_address->read = 1;
		hooked_page_info->entry_address->write = 1;

		hooked_page_info->original_entry = *target_page;
		hooked_page_info->changed_entry = *target_page;

		//伪造页不得读写		
		hooked_page_info->changed_entry.read = 0;
		hooked_page_info->changed_entry.write = 0;
		hooked_page_info->changed_entry.execute = 1;

		//将伪造页换上去
		hooked_page_info->changed_entry.page_frame_number = hooked_page_info->pfn_of_fake_page_contents;

		RtlCopyMemory(&hooked_page_info->fake_page_contents, PAGE_ALIGN(target_function), PAGE_SIZE);

		hooked_function_info->virtual_address = target_function;
		hooked_function_info->breakpoint_address = NULL;

		hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

		//开始hook
		if (write_int1_instruction_to_memory(hooked_function_info, target_function, proxy_function, origin_function) == false)
		{
			if (hooked_function_info->first_trampoline_address != nullptr)
				pool_manager::release_pool(hooked_function_info->first_trampoline_address);
			pool_manager::release_pool(hooked_function_info);
			pool_manager::release_pool(hooked_page_info);
			LogError("Hook failed");
			return false;
		}

		//记录hook的函数和页 以便日后释放
		// Track all hooked functions
		InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

		// Track all hooked pages
		InsertHeadList(&ept_state.hooked_page_list, &hooked_page_info->hooked_page_list);

		invept_single_context_func(ept_state.ept_pointer->all);

		return true;
	}

	/// <summary>
	/// Perfrom a hook
	/// </summary>
	/// <param name="target_address" > Address of function which we want to hook </param>
	/// <param name="proxy_function"> Address of hooked version of function which we are hooking </param>
	/// <param name="(Optional) trampoline">Address of code cave which is located in 2gb range of target function (Use only if you need smaller trampoline)</param>
	/// <param name="origin_function"> Address used to call original function </param>
	/// <returns></returns>
	bool hook_function(__ept_state& ept_state, void* target_function/*被挂钩的函数地址*/, void* proxy_function/*新函数地址*/, void** origin_function)
	{
		unsigned __int64 physical_address = MmGetPhysicalAddress(target_function).QuadPart;

		//
		// 检测该函数是否存在于物理内存中
		// 如果该函数的物理地址为空
		// 说明该函数不存在于物理内存中
		//
		if (physical_address == NULL)
		{
			LogError("请求的虚拟内存在物理内存中不存在");
			return false;
		}

		//
		// 检查页面是否尚未挂钩
		//
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
				LogInfo("页面已挂钩");

				__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, TRUE, sizeof(__ept_hooked_function_info));
				if (hooked_function_info == nullptr)
				{
					LogError("挂钩函数结构没有预先分配池内存");
					return false;
				}

				//
				// If we are hooking code cave for second trampoline 
				// then origin function in null and we don't have to get pool for trampoline
				//
				if (origin_function != nullptr)
				{
					hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
					if (hooked_function_info->first_trampoline_address == nullptr)
					{
						pool_manager::release_pool(hooked_function_info);
						LogError("There is no pre-allocated pool for trampoline");
						return false;
					}
				}

				hooked_function_info->virtual_address = target_function;

				hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

				if (hook_instruction_memory(ept_state, hooked_function_info, target_function, proxy_function, origin_function) == false)
				{
					if (hooked_function_info->first_trampoline_address != nullptr)
						pool_manager::release_pool(hooked_function_info->first_trampoline_address);
					pool_manager::release_pool(hooked_function_info);
					LogError("Hook failed");
					return false;
				}

				// Track all hooked functions within page
				InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

				return true;
			}
		}

		if (is_page_splitted(ept_state, physical_address) == false)
		{
			void* split_buffer = pool_manager::request_pool<void*>(pool_manager::INTENTION_SPLIT_PML2, true, sizeof(__ept_dynamic_split));
			if (split_buffer == nullptr)
			{
				LogError("There is no preallocated pool for split");
				return false;
			}

			//将2MB页面分割为512个4KB页面
			if (split_pml2(g_vmm_context.mtrr_info.mtrrs, ept_state, split_buffer, physical_address) == false)
			{
				pool_manager::release_pool(split_buffer);
				LogError("Split failed");
				return false;
			}
		}

		__ept_pte* target_page = get_pml1_entry(ept_state, physical_address);
		if (target_page == nullptr)
		{
			LogError("Failed to get PML1 entry of the target address");
			return false;
		}

		__ept_hooked_page_info* hooked_page_info = pool_manager::request_pool<__ept_hooked_page_info*>(pool_manager::INTENTION_TRACK_HOOKED_PAGES, true, sizeof(__ept_hooked_page_info));
		if (hooked_page_info == nullptr)
		{
			LogError("There is no preallocated pool for hooked page info");
			return false;
		}

		InitializeListHead(&hooked_page_info->hooked_functions_list);

		__ept_hooked_function_info* hooked_function_info = pool_manager::request_pool<__ept_hooked_function_info*>(pool_manager::INTENTION_TRACK_HOOKED_FUNCTIONS, true, sizeof(__ept_hooked_function_info));
		if (hooked_function_info == nullptr)
		{
			pool_manager::release_pool(hooked_page_info);
			LogError("There is no preallocated pool for hooked function info");
			return false;
		}

		//
		// If we are hooking code cave for second trampoline 
		// then origin function in null and we don't have to get pool for trampoline
		//
		if (origin_function != nullptr)
		{
			hooked_function_info->first_trampoline_address = pool_manager::request_pool<unsigned __int8*>(pool_manager::INTENTION_EXEC_TRAMPOLINE, TRUE, 100);
			if (hooked_function_info->first_trampoline_address == nullptr)
			{
				pool_manager::release_pool(hooked_page_info);
				pool_manager::release_pool(hooked_function_info);
				LogError("There is no pre-allocated pool for trampoline");
				return false;
			}
		}

		hooked_page_info->pfn_of_hooked_page = GET_PFN(physical_address);
		hooked_page_info->pfn_of_fake_page_contents = GET_PFN(MmGetPhysicalAddress(hooked_page_info->fake_page_contents).QuadPart);
		hooked_page_info->entry_address = target_page;

		//先改原页为不可执行 回到guest后执行就触发vmexit
		//进入vmm后交换伪造页
		hooked_page_info->entry_address->execute = 0;
		hooked_page_info->entry_address->read = 1;
		hooked_page_info->entry_address->write = 1;

		hooked_page_info->original_entry = *target_page;
		hooked_page_info->changed_entry = *target_page;

		//伪造页不得读写		
		hooked_page_info->changed_entry.read = 0;
		hooked_page_info->changed_entry.write = 0;
		hooked_page_info->changed_entry.execute = 1;

		//将伪造页换上去
		hooked_page_info->changed_entry.page_frame_number = hooked_page_info->pfn_of_fake_page_contents;

		RtlCopyMemory(&hooked_page_info->fake_page_contents, PAGE_ALIGN(target_function), PAGE_SIZE);

		hooked_function_info->virtual_address = target_function;

		hooked_function_info->fake_page_contents = hooked_page_info->fake_page_contents;

		//开始hook
		if (hook_instruction_memory(ept_state, hooked_function_info, target_function, proxy_function, origin_function) == false)
		{
			if (hooked_function_info->first_trampoline_address != nullptr)
				pool_manager::release_pool(hooked_function_info->first_trampoline_address);
			pool_manager::release_pool(hooked_function_info);
			pool_manager::release_pool(hooked_page_info);
			LogError("Hook failed");
			return false;
		}

		hooked_page_info->Options = EPTO_HOOK_FUNCTION;

		//记录hook的函数和页 以便日后释放
		// Track all hooked functions
		InsertHeadList(&hooked_page_info->hooked_functions_list, &hooked_function_info->hooked_function_list);

		// Track all hooked pages
		InsertHeadList(&ept_state.hooked_page_list, &hooked_page_info->hooked_page_list);

		invept_single_context_func(ept_state.ept_pointer->all);

		return true;
	}

	/// <summary>
	/// Unhook single function
	/// </summary>
	/// <param name="virtual_address"></param>
	/// <returns></returns>
	bool unhook_function(__ept_state& ept_state, unsigned __int64 virtual_address)
	{
		//
		// Check if function which we want to unhook exist in physical memory
		unsigned __int64 physical_address = MmGetPhysicalAddress((void*)virtual_address).QuadPart;
		if (physical_address == 0)
			return false;

		PLIST_ENTRY current_hooked_page = &ept_state.hooked_page_list;
		while (&ept_state.hooked_page_list != current_hooked_page->Flink)
		{
			current_hooked_page = current_hooked_page->Flink;
			__ept_hooked_page_info* hooked_page_info = CONTAINING_RECORD(current_hooked_page, __ept_hooked_page_info, hooked_page_list);

			//
			// Check if function pfn is equal to pfn saved in hooked page info
			if (hooked_page_info->pfn_of_hooked_page == GET_PFN(physical_address))
			{
				PLIST_ENTRY current_hooked_function;
				current_hooked_function = &hooked_page_info->hooked_functions_list;

				while (&hooked_page_info->hooked_functions_list != current_hooked_function->Flink)
				{
					current_hooked_function = current_hooked_function->Flink;
					__ept_hooked_function_info* hooked_function_info = CONTAINING_RECORD(current_hooked_function, __ept_hooked_function_info, hooked_function_list);

					unsigned __int64 function_page_offset = MASK_EPT_PML1_OFFSET(virtual_address);

					//
					// Check if the address of function which we want to unhook is 
					// the same as address of function in hooked function info struct
					//
					if (function_page_offset == MASK_EPT_PML1_OFFSET(hooked_function_info->virtual_address))
					{
						// Restore overwritten data
						RtlCopyMemory(&hooked_function_info->fake_page_contents[function_page_offset], hooked_function_info->virtual_address, hooked_function_info->hook_size);

						// If hook uses two trampolines unhook second one
						if (hooked_function_info->second_trampoline_address != nullptr)
							unhook_function(ept_state, (unsigned __int64)hooked_function_info->second_trampoline_address);

						RemoveEntryList(current_hooked_function);

						if (hooked_function_info->first_trampoline_address != nullptr)
							pool_manager::release_pool(hooked_function_info->first_trampoline_address);
						pool_manager::release_pool(hooked_function_info);

						//
						// If there is no more function hooks free hooked page info struct
						if (hooked_page_info->hooked_functions_list.Flink == hooked_page_info->hooked_functions_list.Blink)
						{
							hooked_page_info->original_entry.execute = 1;
							swap_pml1_and_invalidate_tlb(ept_state, hooked_page_info->entry_address, hooked_page_info->original_entry, invept_single_context);

							RemoveEntryList(current_hooked_page);
							pool_manager::release_pool(hooked_page_info);
							return true;
						}

						invept_all_contexts_func();
						return true;
					}
				}
			}
		}
		return false;
	}

	/// <summary>
	/// Unhook all functions and invalidate tlb
	/// </summary>
	void unhook_all_functions(__ept_state& ept_state)
	{
		PLIST_ENTRY current_hooked_page = ept_state.hooked_page_list.Flink;
		while (&ept_state.hooked_page_list != current_hooked_page)
		{
			__ept_hooked_page_info* hooked_entry = CONTAINING_RECORD(current_hooked_page, __ept_hooked_page_info, hooked_page_list);

			PLIST_ENTRY current_hooked_function;

			current_hooked_function = hooked_entry->hooked_functions_list.Flink;
			while (&hooked_entry->hooked_functions_list != current_hooked_function)
			{
				__ept_hooked_function_info* hooked_function_info = CONTAINING_RECORD(current_hooked_function, __ept_hooked_function_info, hooked_function_list);

				// If hook uses two trampolines unhook second one
				if (hooked_function_info->first_trampoline_address != nullptr)
					pool_manager::release_pool(hooked_function_info->first_trampoline_address);

				RemoveEntryList(current_hooked_function);

				current_hooked_function = current_hooked_function->Flink;

				pool_manager::release_pool(hooked_function_info);
			}

			// Restore original pte value
			// 恢复原页的执行属性
			hooked_entry->original_entry.execute = 1;
			swap_pml1_and_invalidate_tlb(ept_state, hooked_entry->entry_address, hooked_entry->original_entry, invept_single_context);

			RemoveEntryList(current_hooked_page);

			current_hooked_page = current_hooked_page->Flink;

			pool_manager::release_pool(hooked_entry);
		}
	}

	void set_ept_watch(__ept_hooked_page_info* hooked_page_info, unsigned __int64 Type)
	{
		if (Type == EPTW_WRITE)  //监视写
		{
			hooked_page_info->entry_address->write = 0;
		}
		//else if (Type == EPTW_READ) //监视读
		//{
		//	hooked_page_info->entry_address->read = 0;
		//}
		else if (Type == EPTW_READWRITE) //监视读写
		{
			hooked_page_info->entry_address->read = 0;
			hooked_page_info->entry_address->write = 0;
		}
		else if (Type == EPTW_EXECUTE)  //监视执行
		{
			hooked_page_info->entry_address->execute = 0;
		}
		hooked_page_info->Options = EPTO_VIRTUAL_BREAKPOINT;
	}

	bool ept_watch_activate_internal(__ept_state& ept_state, unsigned __int64 physical_address, unsigned __int64 Type)
	{
		if (physical_address == NULL)
		{
			LogError("请求的虚拟内存在物理内存中不存在");
			return false;
		}

		//
		// 检查页面是否尚未挂钩
		//
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
				if (hooked_page_info->Options == EPTO_VIRTUAL_BREAKPOINT)
				{
					LogInfo("页面已挂钩");

					set_ept_watch(hooked_page_info, Type);

					return true;
				}
			}
		}

		//判断2mb pde是否已经进行分割
		if (is_page_splitted(ept_state, physical_address) == false)
		{
			//分配pt页表
			void* split_buffer = pool_manager::request_pool<void*>(pool_manager::INTENTION_SPLIT_PML2, true, sizeof(__ept_dynamic_split));
			if (split_buffer == nullptr)
			{
				LogError("There is no preallocated pool for split");
				return false;
			}

			//将2MB页面分割为512个4KB页面
			if (split_pml2(g_vmm_context.mtrr_info.mtrrs, ept_state, split_buffer, physical_address) == false)
			{
				pool_manager::release_pool(split_buffer);
				LogError("Split failed");
				return false;
			}
		}

		//得到目标页
		__ept_pte* target_page = get_pml1_entry(ept_state, physical_address);
		if (target_page == nullptr)
		{
			LogError("Failed to get PML1 entry of the target address");
			return false;
		}

		__ept_hooked_page_info* hooked_page_info = pool_manager::request_pool<__ept_hooked_page_info*>(pool_manager::INTENTION_TRACK_HOOKED_PAGES, true, sizeof(__ept_hooked_page_info));
		if (hooked_page_info == nullptr)
		{
			LogError("There is no preallocated pool for hooked page info");
			return false;
		}

		InitializeListHead(&hooked_page_info->hooked_functions_list);

		hooked_page_info->pfn_of_hooked_page = GET_PFN(physical_address);  //获取guest物理页面的页帧号
		hooked_page_info->entry_address = target_page; //ept_pte

		set_ept_watch(hooked_page_info, Type);

		// Track all hooked pages
		InsertHeadList(&ept_state.hooked_page_list, &hooked_page_info->hooked_page_list);

		return true;
	}


	bool ept_watch_activate(VT_BREAK_POINT vmcallinfo, unsigned __int64 Type, int* outID, int& errorCode)
	{
		//获取guest的物理地址
		unsigned __int64 physical_address = hv::get_physical_address(vmcallinfo.cr3, (PVOID)vmcallinfo.VirtualAddress);

		if (physical_address == NULL)
		{
			LogError("请求的虚拟地址没有映射的物理地址");
			errorCode = 100;
			return false;
		}

		int status = 0;

		spinlock::lock(&eptWatchList_lock);

		//获取空闲的监视id
		int ID = hv::getIdleWatchID();

		if (ID == -1)
		{
			//没有可用空间
			spinlock::unlock(&eptWatchList_lock);
			errorCode = 101;
			return false;
		}


		//判断是否跨页了，不支持跨页下断，只会设置当前页面
		unsigned __int64 tmp = physical_address + vmcallinfo.Size;
		if (GET_PFN(tmp) != GET_PFN(physical_address))
			eptWatchList[ID].Size = 0x1000 - (physical_address & 0xfff);  //如果跨页，则只会设置当前页面，计算当前页面中需要设置的字节数大小
		else
			eptWatchList[ID].Size = vmcallinfo.Size; //需要设置断点的字节数大小

		eptWatchList[ID].cr3 = vmcallinfo.cr3;
		eptWatchList[ID].PhysicalAddress = physical_address;
		eptWatchList[ID].LoopUserMode = vmcallinfo.LoopUserMode;
		eptWatchList[ID].Type = Type;

		eptWatchList[ID].Options = EPTO_VIRTUAL_BREAKPOINT;
		eptWatchList[ID].bpType = 1;

		eptWatchList[ID].inuse = 1;


		//为每个cpu设置监视
		for (int i = 0; i < vmcallinfo.CPUCount; i++)
		{
			__vcpu* vcpu = &g_vmm_context.vcpu[i];
			if (ept::ept_watch_activate_internal(*vcpu->ept_state, physical_address, Type))
			{
				status++;
			}
		}
		if (status == vmcallinfo.CPUCount)
		{
			//一切正常，返回成功：
			*outID = ID;  //记录这个id，卸载监视的时候需要			
			spinlock::unlock(&eptWatchList_lock);
			invept_all_contexts_func(); //在这里刷新全部逻辑处理器的eptp寄存器
			return true;
		}

		spinlock::unlock(&eptWatchList_lock);
		errorCode = 102;
		return false;
	}

	//取消监视页
	bool ept_watch_deactivate_internal(__ept_state& ept_state,
		unsigned __int64 VirtualAddress,
		unsigned __int64 physical_address,
		unsigned __int64 Type,
		int bpType,
		unsigned char OriginalByte,
		int has)
	{
		if (physical_address == NULL)
			return false;

		PLIST_ENTRY current_hooked_page = &ept_state.hooked_page_list;
		while (&ept_state.hooked_page_list != current_hooked_page->Flink)
		{
			current_hooked_page = current_hooked_page->Flink;
			__ept_hooked_page_info* hooked_page_info = CONTAINING_RECORD(current_hooked_page, __ept_hooked_page_info, hooked_page_list);

			//
			// 检查函数 pfn 是否等于挂钩页面信息中保存的 pfn
			if (hooked_page_info->pfn_of_hooked_page == GET_PFN(physical_address))
			{
				if (hooked_page_info->Options == EPTO_VIRTUAL_BREAKPOINT)
				{

					if (bpType == 3)
					{
						int offset = VirtualAddress & 0xFFF;
						hooked_page_info->fake_page_contents[offset] = OriginalByte;

						if (has == 0) //如果has为0则说明没有存在其他的监视断点，那么我们可以将页进行回收
						{
							hooked_page_info->original_entry.execute = 1;
							//换回原页
							//hooked_page_info->entry_address->all = hooked_page_info->original_entry.all;

							swap_pml1_and_invalidate_tlb(ept_state,
								hooked_page_info->entry_address,
								hooked_page_info->original_entry,
								invept_single_context);
						}
					}
					else
					{
						if (Type == EPTW_WRITE)  //撤销写监视
						{
							hooked_page_info->entry_address->write = 1;
						}
						//else if (Type == EPTW_READ)  //撤销读监视
						//{
						//	if (bpType == 3)  //int3
						//	{
						//		//换回原页
						//		hooked_page_info->entry_address->all = hooked_page_info->original_entry.all;
						//	}
						//	hooked_page_info->entry_address->read = 1;
						//}
						else if (Type == EPTW_READWRITE) //撤销读写监视
						{
							hooked_page_info->entry_address->read = 1;
							hooked_page_info->entry_address->write = 1;
						}
						else if (Type == EPTW_EXECUTE)  //撤销执行监视
						{
							hooked_page_info->entry_address->execute = 1;
						}
					}

					if (has == 0) //如果has为0则说明没有存在其他的监视断点，那么我们可以将页进行回收
					{
						RemoveEntryList(current_hooked_page);
						pool_manager::release_pool(hooked_page_info);
					}
					return true;
				}
			}
		}
		return false;
	}


	int ept_watch_deactivate(VT_BREAK_POINT vmcallinfo, int ID)
	{
		int status = 0;
		int has = 0;
		spinlock::lock(&eptWatchList_lock);

		if (ID >= EPTWATCHLISTSIZE)
		{
			spinlock::unlock(&eptWatchList_lock);
			return 1;
		}

		if (eptWatchList[ID].inuse == 0)
		{
			spinlock::unlock(&eptWatchList_lock);
			return 2;
		}

		for (int i = 0; i < EPTWATCHLISTSIZE; i++)
		{
			if ((i != ID) && ept_isWatchPage(GET_PFN(eptWatchList[ID].PhysicalAddress), i))
			{
				has = 1;  //属于同一页，存在其他监视断点
				if (eptWatchList[i].Type == eptWatchList[ID].Type)
				{
					has = 2;  //不能撤销
					break;
				}
			}
		}

		//没有其他的监视断点了，现在可以把页撤销了
		if ((has != 2) || (eptWatchList[ID].bpType == 3))
		{
			//设置每个cpu
			for (int i = 0; i < vmcallinfo.CPUCount; i++)
			{
				__vcpu* vcpu = &g_vmm_context.vcpu[i];
				if (ept_watch_deactivate_internal(*vcpu->ept_state,
					eptWatchList[ID].VirtualAddress,
					eptWatchList[ID].PhysicalAddress,
					eptWatchList[ID].Type,
					eptWatchList[ID].bpType,
					eptWatchList[ID].OriginalByte,
					has))
				{
					status++;
				}
			}
			if (status == vmcallinfo.CPUCount)
			{
				invept_all_contexts_func(); //在这里刷新全部逻辑处理器的eptp寄存器			
			}
			else
			{
				spinlock::unlock(&eptWatchList_lock);
				return 3;
			}
		}
		else
		{
			//还存在其他的断点，所以还不能把页取消
		}
		eptWatchList[ID] = { 0 };  //将当前entry的inuse标记为未使用
		spinlock::unlock(&eptWatchList_lock);
		return 0;
	}

	//填充页事件所需信息
	void fillPageEventBasic(PageEventBasic* peb, guest_context* guest_registers)
	{
		peb->VirtualAddress = hv::vmread(GUEST_LINEAR_ADDRESS);
		peb->PhysicalAddress = hv::vmread(GUEST_PHYSICAL_ADDRESS);
		peb->CR3 = hv::vmread(GUEST_CR3);
		peb->FSBASE = hv::vmread(GUEST_FS_BASE);
		peb->GSBASE = hv::vmread(GUEST_GS_BASE);
		peb->FLAGS = hv::vmread(GUEST_RFLAGS);
		peb->RAX = guest_registers->rax;
		peb->RBX = guest_registers->rbx;
		peb->RCX = guest_registers->rcx;
		peb->RDX = guest_registers->rdx;
		peb->RSI = guest_registers->rsi;
		peb->RDI = guest_registers->rdi;
		peb->R8 = guest_registers->r8;
		peb->R9 = guest_registers->r9;
		peb->R10 = guest_registers->r10;
		peb->R11 = guest_registers->r11;
		peb->R12 = guest_registers->r12;
		peb->R13 = guest_registers->r13;
		peb->R14 = guest_registers->r14;
		peb->R15 = guest_registers->r15;
		peb->RBP = guest_registers->rbp;
		peb->RSP = hv::vmread(GUEST_RSP);
		peb->RIP = hv::vmread(GUEST_RIP);

		peb->DR0 = __readdr(0);
		peb->DR1 = __readdr(1);
		peb->DR2 = __readdr(2);
		peb->DR3 = __readdr(3);

		peb->DR6 = __readdr(6);
		peb->DR7 = hv::vmread(GUEST_DR7);
		peb->CS = hv::vmread(GUEST_CS_SELECTOR);
		peb->DS = hv::vmread(GUEST_DS_SELECTOR);
		peb->ES = hv::vmread(GUEST_ES_SELECTOR);
		peb->SS = hv::vmread(GUEST_SS_SELECTOR);
		peb->FS = hv::vmread(GUEST_FS_SELECTOR);
		peb->GS = hv::vmread(GUEST_GS_SELECTOR);
	}

	void RestoreEptPageProperties(__ept_hooked_page_info* hooked_page_info, int ID)
	{
		//还原ept页的属性，让guest程序能正常执行，正常读写
		hooked_page_info->entry_address->read = 1;
		hooked_page_info->entry_address->write = 1;
		hooked_page_info->entry_address->execute = 1;
		hooked_page_info->ID = ID;
		hv::set_mtf(true);  //开启mtf
	}

	//判断是否是监视的地址
	int ept_isWatchAddress(unsigned __int64 guest_physical_adddress, int ID)
	{
		return ((eptWatchList[ID].inuse) &&
			(
				(guest_physical_adddress >= eptWatchList[ID].PhysicalAddress) &&
				(guest_physical_adddress < eptWatchList[ID].PhysicalAddress + eptWatchList[ID].Size)
				)
			);
	}

	//判断是否是监视的页
	int ept_isWatchPage(unsigned __int64 pfn, int ID)
	{
		//比较页帧
		return ((eptWatchList[ID].inuse) && (GET_PFN(eptWatchList[ID].PhysicalAddress) == pfn));
	}

	int ept_getWatchID(unsigned __int64 guest_physical_adddress)
		/*
		 * returns -1 if not in a page being watched
		 * Note that there can be multiple active on the same page
		 * 如果不在被监视的页面中则返回 -1
		 * 注意同一页面上可以有多个活动
		 */
	{
		unsigned __int64 pfn = GET_PFN(guest_physical_adddress);  //获取页帧
		for (int i = 0; i < EPTWATCHLISTSIZE; i++)
			if (ept_isWatchPage(pfn, i))
				return i;

		return -1;
	}

	//处理断点监视事件
	bool ept_handleWatchEvent(__vcpu* vcpu,
		__ept_violation ept_violation,
		__ept_hooked_page_info* hooked_page_info,
		unsigned __int64 guest_physical_adddress,
		int& bpType)
	{

		//判断是否是被监视的页面
		if (hooked_page_info->pfn_of_hooked_page == GET_PFN(guest_physical_adddress))
		{
			hooked_page_info->ID = -1;
			hooked_page_info->isBp = false;
			hooked_page_info->isInt3 = false;

			spinlock::lock(&eptWatchList_lock);
			int ID = ept_getWatchID(guest_physical_adddress);

			if (ID == -1)
			{
				spinlock::unlock(&eptWatchList_lock);
				return false;
			}

			if (eptWatchList[ID].bpType == 3) //int3
			{
				//如果读到的是int3监视页则进行换页
				bpType = 3;
				hooked_page_info->ID = ID;
				spinlock::unlock(&eptWatchList_lock);
				return true;
			}



			//确定真正的访问原因（如果同一页面上有多个访问）
			for (int i = ID; i < EPTWATCHLISTSIZE; i++)
			{
				if (ept_isWatchPage(GET_PFN(guest_physical_adddress), i)) //判断是否在监视的页里
				{
					if (eptWatchList[i].Type == EPTW_WRITE)
					{
						if (ept_violation.write_access)  //只写
						{
							ID = i;

							if (ept_isWatchAddress(guest_physical_adddress, i)) //判断是否是监视的地址
								break;
						}
					}
					else if (eptWatchList[i].Type == EPTW_READWRITE)
					{
						if (ept_violation.read_access || ept_violation.write_access)  //读或写
						{
							ID = i;
							if (ept_isWatchAddress(guest_physical_adddress, i))
								break;
						}
					}
					else
					{
						if (ept_violation.execute_access) //执行
						{
							ID = i;

							if (ept_isWatchAddress(guest_physical_adddress, i))
								break;
						}
					}
				}
			}


			//判断是否是虚拟化断点
			if ((eptWatchList[ID].Options & EPTO_VIRTUAL_BREAKPOINT) &&
				(guest_physical_adddress >= eptWatchList[ID].PhysicalAddress) &&
				(guest_physical_adddress < eptWatchList[ID].PhysicalAddress + eptWatchList[ID].Size))
			{
				//if (eptWatchList[ID].bpType == 3) //int3
				//{
				//	//如果读到的是int3地址则进行换页
				//	bpType = 3;
				//	hooked_page_info->ID = ID;

				//	cr3 guest_cr3;
				//	guest_cr3.flags = eptWatchList[ID].cr3;

				//	//同步原页内容到伪页
				//	//if (PAGE_SIZE != hv::read_guest_virtual_memory(guest_cr3, PAGE_ALIGN(eptWatchList[ID].VirtualAddress), &hooked_page_info->fake_page_contents, PAGE_SIZE))
				//	//{
				//	//	//读取数据可能不完整
				//	//	spinlock::unlock(&eptWatchList_lock);
				//	//	return false;
				//	//}

				//	int offset = eptWatchList[ID].VirtualAddress & 0xFFF;  //获得cc的位置
				//	hooked_page_info->fake_page_contents[offset] = eptWatchList[ID].OriginalByte; //恢复原字节
				//	spinlock::unlock(&eptWatchList_lock);
				//	return true;
				//}




				//This is the specific address that was being requested
				//if the current state has interrupts disabled or masked (cr8<>0) then skip (todo: step until it is)
				//这是被请求的特定地址，如果当前状态已禁用或屏蔽中断（cr8<>0），则跳过（todo：逐步执行直到完成）

				//Task Priority Register (CR8)
				//系统软件可以使用 TPR 寄存器暂时阻止低优先级中断，中断高优先级任务。
				//这是通过将要阻止的最高优先级中断的值加载到 TPR 来实现的。例如，将 TPR 的值加载为 9 (1001b)
				//将阻止优先级为 9 或更低的所有中断，同时允许识别优先级为 10 或更高的所有中断。将 TPR 加载为 0 可启用所有外部中断。将 TPR 加载为
				//15 (1111b) 可禁用所有外部中断。
				unsigned __int64 CR8 = __readcr8();
				rflags rflags;
				rflags.flags = hv::vmread(GUEST_RFLAGS);
				__vmx_interruptibility_state is = { hv::vmread(GUEST_INTERRUPTIBILITY_STATE) };
				int canBreak = (CR8 == 0) && (rflags.interrupt_enable_flag); //判断中断是否开启
				canBreak = canBreak && ((is.all & (1 << 0)) == 0);

				if (canBreak) //判断能否进行中断
				{
					int kernelmode = 0;

					//判断触发ept违例之前的模式是内核还是用户模式
					kernelmode = hv::get_guest_cpl() == 0;

					unsigned __int64 newRIP = kernelmode ? 0 : eptWatchList[ID].LoopUserMode;

					if (newRIP)
					{
						hooked_page_info->isBp = true;
						//返回到guest后将阻塞中断，确保能成功执行完GUEST_RIP处的指令
						hv::vmwrite<unsigned __int64>(GUEST_INTERRUPTIBILITY_STATE, 1);  //blocking by STI 阻塞
					}
				}
			}

			RestoreEptPageProperties(hooked_page_info, ID);
			spinlock::unlock(&eptWatchList_lock);
			return true;
		}
		return false;
	}
}
#include "Driver.h"
#include "hypervisor_gateway.h"
#include "AsmCallset.h"
#include "vmcall_reason.h"
#include "ntapi.h"

namespace hvgt
{
	struct HookFunctionArgs
	{
		void* target_address;
		void* proxy_function;
		void** origin_function;
		unsigned __int64 current_cr3;
		volatile SHORT statuses;
	};

	struct UnHookFunctionArgs
	{
		bool unhook_all_functions;
		void* function_to_unhook;
		unsigned __int64 current_cr3;
		volatile SHORT statuses;
	};

	void broadcast_vmoff(KDPC* Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		UNREFERENCED_PARAMETER(Dpc);
		UNREFERENCED_PARAMETER(DeferredContext);

		__vm_call(VMCALL_VMXOFF, 0, 0, 0);
		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	void broadcast_invept_all_contexts(KDPC* Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		UNREFERENCED_PARAMETER(Dpc);
		UNREFERENCED_PARAMETER(DeferredContext);

		__vm_call(VMCALL_INVEPT_CONTEXT, true, 0, 0);
		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	void broadcast_invept_single_context(KDPC* Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		UNREFERENCED_PARAMETER(Dpc);
		UNREFERENCED_PARAMETER(DeferredContext);

		__vm_call(VMCALL_INVEPT_CONTEXT, false, 0, 0);
		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	void broadcast_get_hide_software_breakpoint(KDPC*, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		const auto args = reinterpret_cast<HookFunctionArgs*>(DeferredContext);

		if (__vm_call_ex(VMCALL_READ_SOFTWARE_BREAKPOINT, (unsigned __int64)args->target_address,
			(unsigned __int64)args->proxy_function, (unsigned __int64)args->origin_function, args->current_cr3, 0, 0, 0, 0, 0))
		{
			InterlockedIncrement16(&args->statuses);
		}

		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	void broadcast_set_hide_software_breakpoint(KDPC*, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		const auto args = reinterpret_cast<HookFunctionArgs*>(DeferredContext);

		if (__vm_call_ex(VMCALL_HIDE_SOFTWARE_BREAKPOINT, (unsigned __int64)args->target_address,
			(unsigned __int64)args->proxy_function, (unsigned __int64)args->origin_function, args->current_cr3, 0, 0, 0, 0, 0))
		{
			InterlockedIncrement16(&args->statuses);
		}

		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	void broadcast_hook_function(KDPC*, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		const auto args = reinterpret_cast<HookFunctionArgs*>(DeferredContext);

		if (__vm_call_ex(VMCALL_EPT_CC_HOOK, (unsigned __int64)args->target_address,
			(unsigned __int64)args->proxy_function, (unsigned __int64)args->origin_function, args->current_cr3, 0, 0, 0, 0, 0))
		{
			InterlockedIncrement16(&args->statuses);
		}

		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	void broadcast_unhook_function(KDPC*, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		const auto args = reinterpret_cast<UnHookFunctionArgs*>(DeferredContext);

		if (__vm_call(VMCALL_EPT_UNHOOK_FUNCTION, args->unhook_all_functions,
			(unsigned __int64)args->function_to_unhook, args->current_cr3))
		{
			InterlockedIncrement16(&args->statuses);
		}

		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	void broadcast_test_vmcall(KDPC*, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
	{
		const auto statuses = reinterpret_cast<volatile SHORT*>(DeferredContext);

		if (__vm_call(VMCALL_TEST, 0, 0, 0))
		{
			InterlockedIncrement16(statuses);
		}

		KeSignalCallDpcSynchronize(SystemArgument2);
		KeSignalCallDpcDone(SystemArgument1);
	}

	/// <summary>
	/// Turn off virtual machine
	/// </summary>
	void vmoff(unsigned __int32 processor_count)
	{
		//KeGenericCallDpc(broadcast_vmoff, NULL);

		// we need to be running at an IRQL below DISPATCH_LEVEL so
		// that KeSetSystemAffinityThreadEx takes effect immediately
		NT_ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

		// virtualize every cpu
		for (unsigned int iter = 0; iter < processor_count; iter++)
		{
			// restrict execution to the specified cpu
			auto const orig_affinity = KeSetSystemAffinityThreadEx(1ull << iter);
			__try {
				__vm_call(VMCALL_VMXOFF, 0, 0, 0);
			}
			__except (1)
			{
			}
			KeRevertToUserAffinityThreadEx(orig_affinity);
		}
	}

	/// <summary>
	/// Invalidates mappings in the translation lookaside buffers (TLBs) 
	/// and paging-structure caches that were derived from extended page tables (EPT)
	/// </summary>
	/// <param name="invept_all"> If true invalidates all contexts otherway invalidate only single context (currently hv doesn't use more than 1 context)</param>
	void invept(bool invept_all)
	{
		if (invept_all == true) KeGenericCallDpc(broadcast_invept_all_contexts, NULL);
		else KeGenericCallDpc(broadcast_invept_single_context, NULL);
	}

	/// <summary>
	/// Set/Unset presence of hypervisor
	/// </summary>
	/// <param name="value"> If false, hypervisor is not visible via cpuid interface, If true, it become visible</param>
	void hypervisor_visible(bool value)
	{
		if (value == true)
			__vm_call(VMCALL_UNHIDE_HV_PRESENCE, 0, 0, 0);
		else
			__vm_call(VMCALL_HIDE_HV_PRESENCE, 0, 0, 0);
	}

	/// <summary>
	/// Unhook all functions and invalidate tlb
	/// 卸载全部hook 并刷新tlb
	/// </summary>
	/// <returns> status </returns>
	bool ept_unhook()
	{
		UnHookFunctionArgs args{ true, nullptr, __readcr3(), 0 };
		KeGenericCallDpc(broadcast_unhook_function, &args);

		return static_cast<ULONG>(args.statuses) == KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	}

	/// <summary>
	/// Unhook single function and invalidate tlb
	/// 卸载指定函数 并刷新tlb
	/// </summary>
	/// <param name="function_address"></param>
	/// <returns> status </returns>
	bool ept_unhook(void* function_address)
	{
		UnHookFunctionArgs args{ false, function_address, __readcr3(), 0 };
		KeGenericCallDpc(broadcast_unhook_function, &args);

		return static_cast<ULONG>(args.statuses) == KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	}

	//读取隐形软件断点
	bool get_hide_software_breakpoint(void* target_address, void* buffer, unsigned __int64 buffer_size)
	{
		HookFunctionArgs args{ target_address, buffer, (void**)buffer_size, __readcr3(), 0 };
		KeGenericCallDpc(broadcast_get_hide_software_breakpoint, &args);

		return static_cast<ULONG>(args.statuses) == KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	}

	/// <summary>
	/// Hook function via ept and invalidates mappings
	/// </summary>
	/// <param name="target_address">Address of function which we want to hook</param>
	/// <param name="hook_function">Address of function which is used to call original function</param>
	/// <param name="origin_function">Address of function which is used to call original function</param>
	/// <returns> status </returns>
	bool hook_function(void* target_address, void* proxy_function, void** origin_function)
	{
		HookFunctionArgs args{ target_address, proxy_function, origin_function, __readcr3(), 0 };
		KeGenericCallDpc(broadcast_hook_function, &args);

		return static_cast<ULONG>(args.statuses) == KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	}

	/// <summary>
	/// Check if we can communicate with hypervisor
	/// </summary>
	/// <returns> status </returns>
	bool test_vmcall()
	{
		volatile SHORT statuses{};
		KeGenericCallDpc(broadcast_test_vmcall, (PVOID)&statuses);

		return static_cast<ULONG>(statuses) == KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	}

	/// <summary>
	/// Send irp with information to allocate memory
	/// </summary>
	/// <returns> status </returns>
	bool perform_memory_allocation()
	{
		//PDEVICE_OBJECT airhv_device_object;
		//KEVENT event;
		//PIRP irp;
		//IO_STATUS_BLOCK io_status = { 0 };
		//UNICODE_STRING airhv_name;
		//PFILE_OBJECT file_object;

		//RtlInitUnicodeString(&airhv_name, L"\\Device\\airhv");

		//NTSTATUS status = IoGetDeviceObjectPointer(&airhv_name, 0, &file_object, &airhv_device_object);

		//ObReferenceObjectByPointer(airhv_device_object, FILE_ALL_ACCESS, NULL, KernelMode);

		//// We don't need this so we instantly dereference file object
		//ObDereferenceObject(file_object);

		//if (NT_SUCCESS(status) == false)
		//{
		//	LogError("Couldn't get hypervisor device object pointer");
		//	return false;
		//}

		//KeInitializeEvent(&event, NotificationEvent, 0);
		//irp = IoBuildDeviceIoControlRequest(IOCTL_POOL_MANAGER_ALLOCATE, airhv_device_object, 0, 0, 0, 0, 0, &event, &io_status);

		//if (irp == NULL)
		//{
		//	LogError("Couldn't create Irp");
		//	ObDereferenceObject(airhv_device_object);
		//	return false;
		//}

		//else
		//{
		//	status = IofCallDriver(airhv_device_object, irp);

		//	if (status == STATUS_PENDING)
		//		KeWaitForSingleObject(&event, Executive, KernelMode, 0, 0);

		//	ObDereferenceObject(airhv_device_object);
		return true;
		//}
	}
}
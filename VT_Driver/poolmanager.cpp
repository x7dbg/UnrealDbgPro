#include "Driver.h"
#include "poolmanager.h"
#include "Globals.h"
#include "mtrr.h"
#include "EPT.h"
#include "AllocateMem.h"


namespace pool_manager
{
    /// <summary>
    /// Allocate pools and add them to pool table
    /// 分配池并将其添加到池表中
    /// </summary>
    /// <param name="size">Size of pool</param>
    /// <param name="count">Number of pools to allocate</param>
    /// <param name="intention"></param>
    /// <returns></returns>
    bool allocate_pool(unsigned __int64 size, unsigned __int32 count, allocation_intention intention)
    {
        for (unsigned int i = 0; i < count; i++)
        {
            __pool_table* single_pool = ::allocate_pool<__pool_table>();
            if (single_pool == nullptr)
            {
                LogError("内存分配失败；原因：非分页内存池不足；解决方案：释放资源或降低预分配数量后重试。");
                return false;
            }
            RtlSecureZeroMemory(single_pool, sizeof(__pool_table));

            single_pool->address = ::allocate_pool<void*>(size);

            if (single_pool->address == nullptr)
            {
                LogError("内存分配失败；原因：非分页内存池不足；解决方案：释放资源或降低预分配数量后重试。");
                return false;
            }
            RtlSecureZeroMemory(single_pool->address, size);

            single_pool->intention = intention;
            single_pool->is_busy = false;  //将内存标识为未使用
            single_pool->size = size;

            InsertTailList(g_vmm_context.pool_manager->list_of_allocated_pools, &(single_pool->pool_list));
        }

        return true;
    }

    /// <summary>
    /// Request allocation
    /// </summary>
    /// <param name="size">Size of pool</param>
    /// <param name="count">Number of pools to allocate</param>
    /// <param name="intention"></param>
    /// <returns></returns>
    bool request_allocation(unsigned __int64 size, unsigned __int32 count, allocation_intention intention)
    {
        spinlock::lock(&g_vmm_context.pool_manager->lock_for_request_allocation);

        for (unsigned __int64 i = 0; i < 10; i++)
        {
            if (g_vmm_context.pool_manager->allocation_requests->size[i] == 0)
            {
                g_vmm_context.pool_manager->allocation_requests->count[i] = count;
                g_vmm_context.pool_manager->allocation_requests->size[i] = size;
                g_vmm_context.pool_manager->allocation_requests->intention[i] = intention;
                g_vmm_context.pool_manager->is_request_for_allocation_recived = true;
                break;
            }
        }

        spinlock::unlock(&g_vmm_context.pool_manager->lock_for_request_allocation);
        return g_vmm_context.pool_manager->is_request_for_allocation_recived;
    }

    /// <summary>
    /// 分配所有请求的池
    /// </summary>
    /// <returns></returns>
    bool perform_allocation()
    {
        bool status = true;

        if (g_vmm_context.pool_manager->is_request_for_allocation_recived == false)
        {
            LogInfo("当前没有待处理的内存分配请求。");
            return status;
        }

        for (unsigned __int64 i = 0; i < 10; i++)
        {
            if (g_vmm_context.pool_manager->allocation_requests->size[i] != 0)
            {
                status = allocate_pool
                (
                    g_vmm_context.pool_manager->allocation_requests->size[i],
                    g_vmm_context.pool_manager->allocation_requests->count[i],
                    g_vmm_context.pool_manager->allocation_requests->intention[i]
                );

                if (status == false)
                {
                    LogError("内存池管理器分配并登记失败；原因：内存池不足；解决方案：释放资源后重试。");
                    break;
                }

                g_vmm_context.pool_manager->allocation_requests->size[i] = 0;
                g_vmm_context.pool_manager->allocation_requests->count[i] = 0;
                g_vmm_context.pool_manager->allocation_requests->intention[i] = INTENTION_NONE;

                LogInfo("内存池分配成功。");
            }
        }

        g_vmm_context.pool_manager->is_request_for_allocation_recived = false;

        return status;
    }

    /// <summary>
    /// Initalize pool manager struct and preallocate pools
    /// 初始化池管理器结构并预分配池
    /// </summary>
    /// <returns> status </returns>
    bool initialize()
    {
        g_vmm_context.pool_manager = ::allocate_pool<__pool_manager>();
        if (g_vmm_context.pool_manager == nullptr)
        {
            LogError("内存池管理器结构分配失败；原因：非分页内存池不足；解决方案：释放资源后重试。");
            return false;
        }
        RtlSecureZeroMemory(g_vmm_context.pool_manager, sizeof(__pool_manager));

        g_vmm_context.pool_manager->allocation_requests = ::allocate_pool<__request_new_allocation>();
        if (g_vmm_context.pool_manager->allocation_requests == nullptr)
        {
            LogError("分配请求表分配失败；原因：非分页内存池不足；解决方案：释放资源后重试。");
            return false;
        }
        RtlSecureZeroMemory(g_vmm_context.pool_manager->allocation_requests, sizeof(__request_new_allocation));

        g_vmm_context.pool_manager->list_of_allocated_pools = ::allocate_pool<LIST_ENTRY>();
        if (g_vmm_context.pool_manager->list_of_allocated_pools == nullptr)
        {
            LogError("已分配内存池链表分配失败；原因：非分页内存池不足；解决方案：释放资源后重试。");
            return false;
        }
        RtlSecureZeroMemory(g_vmm_context.pool_manager->list_of_allocated_pools, sizeof(LIST_ENTRY));

        InitializeListHead(g_vmm_context.pool_manager->list_of_allocated_pools);

        unsigned __int64 buffer_count = g_vmm_context.processor_count * 100;

        if (request_allocation(sizeof(__ept_dynamic_split), buffer_count, INTENTION_SPLIT_PML2) == false)
        {
            LogError("内存池管理器提交分配请求失败；原因：请求表已满或内存参数无效；解决方案：减少预分配数量并重试。");
            return false;
        }

        if (request_allocation(sizeof(__ept_hooked_page_info), buffer_count, INTENTION_TRACK_HOOKED_PAGES) == false)
        {
            LogError("内存池管理器提交分配请求失败；原因：请求表已满或内存参数无效；解决方案：减少预分配数量并重试。");
            return false;
        }

        if (request_allocation(100, buffer_count, INTENTION_EXEC_TRAMPOLINE) == false)
        {
            LogError("内存池管理器提交分配请求失败；原因：请求表已满或内存参数无效；解决方案：减少预分配数量并重试。");
            return false;
        }

        if (request_allocation(sizeof(__ept_hooked_function_info), buffer_count, INTENTION_TRACK_HOOKED_FUNCTIONS) == false)
        {
            LogError("内存池管理器提交分配请求失败；原因：请求表已满或内存参数无效；解决方案：减少预分配数量并重试。");
            return false;
        }

        return perform_allocation();
    }
    /// <summary>
    /// Free all allocted pools
    /// 释放所有已分配的池
    /// </summary>
    void uninitialize()
    {
        PLIST_ENTRY ListHead, NextEntry, DelEntry;

        if (g_vmm_context.pool_manager->list_of_allocated_pools != nullptr)
        {
            ListHead = g_vmm_context.pool_manager->list_of_allocated_pools;
            NextEntry = ListHead->Flink;

            while (ListHead != NextEntry)
            {
                // Get the head of the record
                __pool_table* pool_table = (__pool_table*)CONTAINING_RECORD(NextEntry, __pool_table, pool_list);

                // Free the alloocated buffer
                free_pool(pool_table->address);

                DelEntry = NextEntry;
                /* Move to the next entry */
                NextEntry = NextEntry->Flink;

                RemoveEntryList(DelEntry);
                // Free the record itself
                free_pool(pool_table);
            }

            free_pool(g_vmm_context.pool_manager->list_of_allocated_pools);
        }

        if (g_vmm_context.pool_manager->allocation_requests != nullptr)
        {
            free_pool(g_vmm_context.pool_manager->allocation_requests);
        }
    }

    /// <summary>
    /// Set information that pool is no longer used anymore
    /// </summary>
    /// <param name="address"></param>
    void release_pool(void* address)
    {
        PLIST_ENTRY current = 0;
        current = g_vmm_context.pool_manager->list_of_allocated_pools;

        spinlock::lock(&g_vmm_context.pool_manager->lock_for_reading_pool);
        while (g_vmm_context.pool_manager->list_of_allocated_pools != current->Flink)
        {
            current = current->Flink;

            // Get the head of the record
            __pool_table* pool_table = (__pool_table*)CONTAINING_RECORD(current, __pool_table, pool_list);

            if (address == pool_table->address)
            {
                RtlSecureZeroMemory(address, pool_table->size);
                pool_table->is_busy = false;
                pool_table->recycled = true;
                break;
            }
        }

        spinlock::unlock(&g_vmm_context.pool_manager->lock_for_reading_pool);
    }

    inline const char* intention_to_string(allocation_intention intention)
    {
        switch (intention)
        {
        case INTENTION_NONE:   return "无用途";
        case INTENTION_TRACK_HOOKED_PAGES:   return "跟踪已挂钩页面";
        case INTENTION_EXEC_TRAMPOLINE: return "执行跳板";
        case INTENTION_SPLIT_PML2: return "拆分 PML2";
        case INTENTION_TRACK_HOOKED_FUNCTIONS: return "跟踪已挂钩函数";
        default:      return "未知用途";
        }
    }

    /// <summary>
    /// Writes all information about allocated pools
    /// </summary>
    void dump_pools_info()
    {
        PLIST_ENTRY current = g_vmm_context.pool_manager->list_of_allocated_pools;

        spinlock::lock(&g_vmm_context.pool_manager->lock_for_reading_pool);

        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "-----------------------------------内存池管理器转储-----------------------------------\r\n");

        while (g_vmm_context.pool_manager->list_of_allocated_pools != current->Flink)
        {
            current = current->Flink;

            // Get the head of the record
            __pool_table* pool_table = (__pool_table*)CONTAINING_RECORD(current, __pool_table, pool_list);

            LogDump("地址：0x%X    大小：%llu    用途：%s    使用中：%s    已回收：%s",
                pool_table->address, pool_table->size, intention_to_string(pool_table->intention), pool_table->is_busy ? "Yes" : "No",
                pool_table->recycled ? "Yes" : "No");
        }

        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "-----------------------------------内存池管理器转储-----------------------------------\r\n");

        spinlock::unlock(&g_vmm_context.pool_manager->lock_for_reading_pool);
    }
}

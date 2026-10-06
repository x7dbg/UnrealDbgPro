#pragma once

#ifndef _ALLOCATEMEM_H
#define _ALLOCATEMEM_H

#define NPPM_TAG 'nppm'

template <typename T>
inline T allocate_pool(unsigned __int64 size)
{
    PVOID pAddr = ExAllocatePoolWithTag(NonPagedPool, size, NPPM_TAG);
    RtlSecureZeroMemory(pAddr, size);
    return (T)pAddr;
}

template <typename T>
inline T* allocate_pool()
{
    PVOID pAddr = ExAllocatePoolWithTag(NonPagedPool, sizeof(T), NPPM_TAG);
    RtlSecureZeroMemory(pAddr, sizeof(T));
    return (T*)pAddr;
}

template <typename T>
inline T* allocate_contignous_memory()
{
    PHYSICAL_ADDRESS a;
    a.QuadPart = 0ULL - 1;
    return (T*)MmAllocateContiguousMemory(sizeof(T), a);
}

template <typename T>
inline T allocate_contignous_memory(unsigned __int64 size)
{
    PHYSICAL_ADDRESS a;
    a.QuadPart = 0ULL - 1;
    return (T)MmAllocateContiguousMemory(size, a);
}

inline void free_pool(void* pool_address)
{
    if (pool_address)
    {
        ExFreePoolWithTag(pool_address, NPPM_TAG);
    }    
}

//释放连续内存
inline void free_contignous_memory(void* memory_address)
{
    MmFreeContiguousMemory(memory_address);
}

#endif // !_ALLOCATEMEM_H
